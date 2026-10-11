#include "postgres_events_private.h"
#include <cpkt/postgres.h>

#include <libpq-events.h>
#include <pthread.h>
#include <stdlib.h>

/*
 * libpq identifies registrations by native function address.  A single
 * dispatcher owns a list of facade registrations.  Result entries freeze that
 * list when libpq attaches its event array to a result.  Every entry has its
 * own initialization flag, as libpq's independent native callbacks do.
 */
typedef struct cpkt_postgres_event_owner cpkt_postgres_event_owner;
typedef struct cpkt_postgres_event_result cpkt_postgres_event_result;
typedef struct cpkt_postgres_event_state cpkt_postgres_event_state;

struct cpkt_postgres_event {
  cpkt_postgres_event_callback callback;
  void *context;
  void *data;
  int registered;
  cpkt_postgres_event_owner *owner;
  cpkt_postgres_event *next;
};

struct cpkt_postgres_event_state {
  cpkt_postgres_event *event;
  void *data;
  int initialized;
  cpkt_postgres_event_state *next;
};

struct cpkt_postgres_event_result {
  PGresult *native;
  cpkt_postgres_event_owner *owner;
  cpkt_postgres_event_state *states;
  unsigned long create_fired_count;
  int last_create_complete;
  int native_initialized;
  cpkt_postgres_event_result *next;
};

struct cpkt_postgres_event_owner {
  PGconn *native;
  cpkt_postgres_event *head;
  cpkt_postgres_event *tail;
  cpkt_postgres_event *retired;
  unsigned int result_count;
  int closed;
  int native_registered;
  cpkt_postgres_event_owner *next;
};

static pthread_mutex_t cpkt_postgres_events_mutex = PTHREAD_MUTEX_INITIALIZER;
static cpkt_postgres_event_owner *cpkt_postgres_event_owners;
static cpkt_postgres_event_result *cpkt_postgres_event_results;

static cpkt_postgres_event_owner *
cpkt_postgres_event_find_owner(const PGconn *connection) {
  cpkt_postgres_event_owner *owner;
  for (owner = cpkt_postgres_event_owners; owner != NULL; owner = owner->next) {
    if (owner->native == connection && !owner->closed)
      return owner;
  }
  return NULL;
}

static cpkt_postgres_event_result *
cpkt_postgres_event_find_result(const PGresult *result) {
  cpkt_postgres_event_result *entry;
  for (entry = cpkt_postgres_event_results; entry != NULL;
       entry = entry->next) {
    if (entry->native == result)
      return entry;
  }
  return NULL;
}

static int cpkt_postgres_event_contains(cpkt_postgres_event_owner *owner,
                                        const cpkt_postgres_event *event) {
  cpkt_postgres_event *current;
  for (current = owner->head; current != NULL; current = current->next) {
    if (current == event)
      return 1;
  }
  return 0;
}

static void
cpkt_postgres_event_dispose_owner(cpkt_postgres_event_owner *owner) {
  cpkt_postgres_event *event;
  cpkt_postgres_event *next;
  for (event = owner->head; event != NULL; event = next) {
    next = event->next;
    free(event);
  }
  for (event = owner->retired; event != NULL; event = next) {
    next = event->next;
    free(event);
  }
  free(owner);
}

/* Called while holding the events mutex, in the same critical section that
 * relinquishes the connection or result role.  Exactly one caller claims the
 * disposal; a caller must not dereference owner after relinquishing its role.
 */
static int
cpkt_postgres_event_claim_disposal_locked(cpkt_postgres_event_owner *owner) {
  cpkt_postgres_event_owner **slot;
  if (!owner->closed || owner->result_count != 0U)
    return 0;
  slot = &cpkt_postgres_event_owners;
  while (*slot != owner)
    slot = &(*slot)->next;
  *slot = owner->next;
  return 1;
}

static cpkt_postgres_event_state *
cpkt_postgres_event_find_state(cpkt_postgres_event_result *result,
                               const cpkt_postgres_event *event) {
  cpkt_postgres_event_state *state;
  for (state = result->states; state != NULL; state = state->next) {
    if (state->event == event)
      return state;
  }
  return NULL;
}

static cpkt_postgres_event_result *
cpkt_postgres_event_make_result(cpkt_postgres_event_owner *owner,
                                PGresult *native,
                                const cpkt_postgres_event_result *source) {
  cpkt_postgres_event_result *result;
  cpkt_postgres_event_state **slot;
  cpkt_postgres_event_state *state;
  const cpkt_postgres_event_state *source_state;
  cpkt_postgres_event *event;
  result = (cpkt_postgres_event_result *)cpkt_postgres_event_allocate(
      sizeof(*result));
  if (result == NULL)
    return NULL;
  result->native = native;
  result->owner = owner;
  slot = &result->states;
  source_state = source == NULL ? NULL : source->states;
  event = source == NULL ? owner->head : NULL;
  while (source_state != NULL || event != NULL) {
    /* REGISTER may create a result before accepting its registration. The
     * result freezes only callbacks that have already accepted; a rejected
     * callback's context can be released when register returns NULL. */
    if (source == NULL && !event->registered) {
      event = event->next;
      continue;
    }
    state = (cpkt_postgres_event_state *)cpkt_postgres_event_allocate(
        sizeof(*state));
    if (state == NULL) {
      while (result->states != NULL) {
        state = result->states;
        result->states = state->next;
        free(state);
      }
      free(result);
      return NULL;
    }
    state->event = source == NULL ? event : source_state->event;
    *slot = state;
    slot = &state->next;
    if (source == NULL)
      event = event->next;
    else
      source_state = source_state->next;
  }
  (void)pthread_mutex_lock(&cpkt_postgres_events_mutex);
  result->next = cpkt_postgres_event_results;
  cpkt_postgres_event_results = result;
  ++owner->result_count;
  (void)pthread_mutex_unlock(&cpkt_postgres_events_mutex);
  return result;
}

static int cpkt_postgres_event_call(cpkt_postgres_event *event,
                                    cpkt_postgres_event_id id,
                                    cpkt_postgres_event_info *info) {
  info->event = event;
  return event->callback(id, info, event->context);
}

static int cpkt_postgres_event_create(cpkt_postgres_event_result *result,
                                      PGconn *connection) {
  cpkt_postgres_event_state *state;
  cpkt_postgres_event_info info;
  int complete;
  info.connection = (cpkt_postgres_connection *)connection;
  info.result = (cpkt_postgres_result *)result->native;
  info.source = NULL;
  info.destination = NULL;
  complete = 1;
  for (state = result->states; state != NULL; state = state->next) {
    if (!state->initialized) {
      if (cpkt_postgres_event_call(state->event,
                                   CPKT_POSTGRES_EVENT_RESULT_CREATE, &info))
        state->initialized = 1;
      else
        complete = 0;
    }
  }
  return complete;
}

static int cpkt_postgres_native_event(PGEventId id, void *native_info,
                                      void *context) {
  cpkt_postgres_event_owner *owner;
  cpkt_postgres_event_result *result;
  cpkt_postgres_event_result *source;
  cpkt_postgres_event_result **slot;
  cpkt_postgres_event_state *state;
  cpkt_postgres_event_state *source_state;
  cpkt_postgres_event *event;
  cpkt_postgres_event *last;
  cpkt_postgres_event_info info;
  cpkt_postgres_callback_result notice_scope;
  PGresult *native_result;
  int dispose;

  owner = (cpkt_postgres_event_owner *)context;
  info.event = NULL;
  info.connection = NULL;
  info.result = NULL;
  info.source = NULL;
  info.destination = NULL;
  /* Register the dispatcher before invoking any application callback.
   * Nested registrations then survive even if the outer callback declines. */
  if (id == PGEVT_REGISTER)
    return 1;
  if (id == PGEVT_CONNRESET || id == PGEVT_CONNDESTROY) {
    if (id == PGEVT_CONNRESET)
      info.connection =
          (cpkt_postgres_connection *)((PGEventConnReset *)native_info)->conn;
    else
      info.connection =
          (cpkt_postgres_connection *)((PGEventConnDestroy *)native_info)->conn;
    last = owner->tail;
    for (event = owner->head; event != NULL; event = event->next) {
      if (event->registered)
        (void)cpkt_postgres_event_call(event, (cpkt_postgres_event_id)id,
                                       &info);
      if (event == last)
        break;
    }
    if (id == PGEVT_CONNDESTROY) {
      (void)pthread_mutex_lock(&cpkt_postgres_events_mutex);
      owner->closed = 1;
      owner->native = NULL;
      dispose = cpkt_postgres_event_claim_disposal_locked(owner);
      (void)pthread_mutex_unlock(&cpkt_postgres_events_mutex);
      if (dispose)
        cpkt_postgres_event_dispose_owner(owner);
    }
    return 1;
  }
  if (id == PGEVT_RESULTCREATE) {
    PGEventResultCreate *native;
    native = (PGEventResultCreate *)native_info;
    (void)pthread_mutex_lock(&cpkt_postgres_events_mutex);
    result = cpkt_postgres_event_find_result(native->result);
    (void)pthread_mutex_unlock(&cpkt_postgres_events_mutex);
    if (result == NULL)
      result = cpkt_postgres_event_make_result(owner, native->result, NULL);
    if (result == NULL)
      return 0;
    cpkt_postgres_notice_callback_begin(native->conn, NULL, native->result,
                                        &notice_scope);
    /* The native dispatcher stays initialized so each successful facade
     * callback still receives its own destroy event after a peer declines. */
    result->last_create_complete =
        cpkt_postgres_event_create(result, native->conn);
    cpkt_postgres_notice_callback_end(&notice_scope);
    ++result->create_fired_count;
    result->native_initialized = 1;
    return 1;
  }
  if (id == PGEVT_RESULTCOPY) {
    PGEventResultCopy *native;
    native = (PGEventResultCopy *)native_info;
    (void)pthread_mutex_lock(&cpkt_postgres_events_mutex);
    source = cpkt_postgres_event_find_result(native->src);
    (void)pthread_mutex_unlock(&cpkt_postgres_events_mutex);
    if (source == NULL)
      return 0;
    result = cpkt_postgres_event_make_result(owner, native->dest, source);
    if (result == NULL)
      return 0;
    cpkt_postgres_notice_callback_begin(NULL, native->src, native->dest,
                                        &notice_scope);
    info.source = (const cpkt_postgres_result *)native->src;
    info.destination = (cpkt_postgres_result *)native->dest;
    state = result->states;
    source_state = source->states;
    while (state != NULL) {
      if (source_state->initialized &&
          cpkt_postgres_event_call(state->event,
                                   CPKT_POSTGRES_EVENT_RESULT_COPY, &info))
        state->initialized = 1;
      state = state->next;
      source_state = source_state->next;
    }
    cpkt_postgres_notice_callback_end(&notice_scope);
    result->native_initialized = 1;
    return 1;
  }
  if (id != PGEVT_RESULTDESTROY)
    return 1;
  native_result = ((PGEventResultDestroy *)native_info)->result;
  (void)pthread_mutex_lock(&cpkt_postgres_events_mutex);
  result = cpkt_postgres_event_find_result(native_result);
  (void)pthread_mutex_unlock(&cpkt_postgres_events_mutex);
  if (result == NULL)
    return 1;
  cpkt_postgres_notice_callback_begin(NULL, native_result, native_result,
                                      &notice_scope);
  info.result = (cpkt_postgres_result *)native_result;
  for (state = result->states; state != NULL; state = state->next) {
    if (state->initialized)
      (void)cpkt_postgres_event_call(state->event,
                                     CPKT_POSTGRES_EVENT_RESULT_DESTROY, &info);
  }
  cpkt_postgres_notice_callback_end(&notice_scope);
  (void)pthread_mutex_lock(&cpkt_postgres_events_mutex);
  slot = &cpkt_postgres_event_results;
  while (*slot != result)
    slot = &(*slot)->next;
  *slot = result->next;
  --owner->result_count;
  dispose = cpkt_postgres_event_claim_disposal_locked(owner);
  (void)pthread_mutex_unlock(&cpkt_postgres_events_mutex);
  while (result->states != NULL) {
    state = result->states;
    result->states = state->next;
    free(state);
  }
  free(result);
  if (dispose)
    cpkt_postgres_event_dispose_owner(owner);
  return 1;
}

cpkt_postgres_event *
cpkt_postgres_event_register(cpkt_postgres_connection *connection,
                             cpkt_postgres_event_callback callback,
                             const char *name, void *context) {
  cpkt_postgres_event_owner *owner;
  cpkt_postgres_event *event;
  cpkt_postgres_event *cursor;
  cpkt_postgres_event_info info;
  int first;
  int accepted;
  if (connection == NULL || callback == NULL || name == NULL || *name == '\0')
    return NULL;
  (void)pthread_mutex_lock(&cpkt_postgres_events_mutex);
  owner = cpkt_postgres_event_find_owner((PGconn *)connection);
  if (owner != NULL) {
    for (cursor = owner->head; cursor != NULL; cursor = cursor->next) {
      if (cursor->callback == callback) {
        (void)pthread_mutex_unlock(&cpkt_postgres_events_mutex);
        return NULL;
      }
    }
  } else {
    owner = (cpkt_postgres_event_owner *)cpkt_postgres_event_allocate(
        sizeof(*owner));
    if (owner == NULL) {
      (void)pthread_mutex_unlock(&cpkt_postgres_events_mutex);
      return NULL;
    }
    owner->native = (PGconn *)connection;
    owner->next = cpkt_postgres_event_owners;
    cpkt_postgres_event_owners = owner;
  }
  event = (cpkt_postgres_event *)cpkt_postgres_event_allocate(sizeof(*event));
  if (event == NULL) {
    if (owner->head == NULL && !owner->native_registered) {
      cpkt_postgres_event_owner **slot;
      slot = &cpkt_postgres_event_owners;
      while (*slot != owner)
        slot = &(*slot)->next;
      *slot = owner->next;
      free(owner);
    }
    (void)pthread_mutex_unlock(&cpkt_postgres_events_mutex);
    return NULL;
  }
  event->callback = callback;
  event->context = context;
  event->owner = owner;
  if (owner->tail != NULL)
    owner->tail->next = event;
  else
    owner->head = event;
  owner->tail = event;
  first = !owner->native_registered;
  (void)pthread_mutex_unlock(&cpkt_postgres_events_mutex);
  if (first) {
    if (!PQregisterEventProc((PGconn *)connection, cpkt_postgres_native_event,
                             name, owner))
      goto registration_failed;
    owner->native_registered = 1;
  }
  info.event = event;
  info.connection = connection;
  info.result = NULL;
  info.source = NULL;
  info.destination = NULL;
  accepted =
      cpkt_postgres_event_call(event, CPKT_POSTGRES_EVENT_REGISTER, &info);
  if (accepted)
    event->registered = 1;
  if (accepted)
    return event;
registration_failed:
  (void)pthread_mutex_lock(&cpkt_postgres_events_mutex);
  cursor = owner->head;
  if (cursor == event)
    owner->head = event->next;
  else {
    while (cursor->next != event)
      cursor = cursor->next;
    cursor->next = event->next;
  }
  if (owner->tail == event)
    owner->tail = cursor == event ? NULL : cursor;
  (void)pthread_mutex_unlock(&cpkt_postgres_events_mutex);
  if (owner->native_registered) {
    event->next = owner->retired;
    owner->retired = event;
  } else {
    free(event);
  }
  if (owner->head == NULL && !owner->native_registered) {
    (void)pthread_mutex_lock(&cpkt_postgres_events_mutex);
    {
      cpkt_postgres_event_owner **slot;
      slot = &cpkt_postgres_event_owners;
      while (*slot != owner)
        slot = &(*slot)->next;
      *slot = owner->next;
    }
    (void)pthread_mutex_unlock(&cpkt_postgres_events_mutex);
    free(owner);
  }
  return NULL;
}

/** Store application data for this connection registration. */
int cpkt_postgres_event_set_connection_data(
    cpkt_postgres_connection *connection, cpkt_postgres_event *event,
    void *data) {
  cpkt_postgres_event_owner *owner;
  int found;
  (void)pthread_mutex_lock(&cpkt_postgres_events_mutex);
  owner = cpkt_postgres_event_find_owner((PGconn *)connection);
  found = owner != NULL && cpkt_postgres_event_contains(owner, event);
  if (found)
    event->data = data;
  (void)pthread_mutex_unlock(&cpkt_postgres_events_mutex);
  return found;
}

void *
cpkt_postgres_event_connection_data(const cpkt_postgres_connection *connection,
                                    const cpkt_postgres_event *event) {
  cpkt_postgres_event_owner *owner;
  void *data;
  (void)pthread_mutex_lock(&cpkt_postgres_events_mutex);
  owner = cpkt_postgres_event_find_owner((const PGconn *)connection);
  data = owner != NULL && cpkt_postgres_event_contains(owner, event)
             ? event->data
             : NULL;
  (void)pthread_mutex_unlock(&cpkt_postgres_events_mutex);
  return data;
}

/** Store application data for one result registration. */
int cpkt_postgres_event_set_result_data(cpkt_postgres_result *result,
                                        cpkt_postgres_event *event,
                                        void *data) {
  cpkt_postgres_event_result *entry;
  cpkt_postgres_event_state *state;
  (void)pthread_mutex_lock(&cpkt_postgres_events_mutex);
  entry = cpkt_postgres_event_find_result((PGresult *)result);
  state = entry == NULL ? NULL : cpkt_postgres_event_find_state(entry, event);
  if (state != NULL)
    state->data = data;
  (void)pthread_mutex_unlock(&cpkt_postgres_events_mutex);
  return state != NULL;
}

/** Read application data while the result registration is live. */
void *cpkt_postgres_event_result_data(const cpkt_postgres_result *result,
                                      const cpkt_postgres_event *event) {
  cpkt_postgres_event_result *entry;
  cpkt_postgres_event_state *state;
  void *data;
  (void)pthread_mutex_lock(&cpkt_postgres_events_mutex);
  entry = cpkt_postgres_event_find_result((const PGresult *)result);
  state = entry == NULL ? NULL : cpkt_postgres_event_find_state(entry, event);
  data = state == NULL ? NULL : state->data;
  (void)pthread_mutex_unlock(&cpkt_postgres_events_mutex);
  return data;
}

/** Ask native libpq to fire, and return facade per-registration status. */
int cpkt_postgres_event_fire_result_create(cpkt_postgres_connection *connection,
                                           cpkt_postgres_result *result) {
  cpkt_postgres_event_result *entry;
  unsigned long before;
  (void)pthread_mutex_lock(&cpkt_postgres_events_mutex);
  entry = cpkt_postgres_event_find_result((PGresult *)result);
  before = entry == NULL ? 0 : entry->create_fired_count;
  (void)pthread_mutex_unlock(&cpkt_postgres_events_mutex);
  if (!PQfireResultCreateEvents((PGconn *)connection, (PGresult *)result))
    return 0;
  (void)pthread_mutex_lock(&cpkt_postgres_events_mutex);
  entry = cpkt_postgres_event_find_result((PGresult *)result);
  (void)pthread_mutex_unlock(&cpkt_postgres_events_mutex);
  if (entry == NULL)
    return 1;
  if (entry->create_fired_count != before)
    return entry->last_create_complete;
  return cpkt_postgres_event_create(entry, (PGconn *)connection);
}

/** Attach an owner role before returning a newly created result. */
int cpkt_postgres_event_prepare_result(PGconn *connection, PGresult *result) {
  cpkt_postgres_event_owner *owner;
  cpkt_postgres_event_result *existing;
  if (connection == NULL || result == NULL)
    return 1;
  (void)pthread_mutex_lock(&cpkt_postgres_events_mutex);
  owner = cpkt_postgres_event_find_owner(connection);
  existing = cpkt_postgres_event_find_result(result);
  (void)pthread_mutex_unlock(&cpkt_postgres_events_mutex);
  if (owner != NULL && existing == NULL)
    return cpkt_postgres_event_make_result(owner, result, NULL) != NULL;
  return 1;
}

/** Retain callback-local native notice event state while its receiver runs. */
int cpkt_postgres_event_prepare_notice(PGconn *connection,
                                       const PGresult *result) {
  /* libpq copies the connection's native event array into notices without
   * firing RESULTCREATE. The callback-local source freezes registrations;
   * surviving copies each acquire their own owner role. */
  return cpkt_postgres_event_prepare_result(connection, (PGresult *)result);
}

/** Release an uninitialized native notice after its receiver returns. */
void cpkt_postgres_event_release_notice(const PGresult *result) {
  /* PQclear skips RESULTDESTROY on ordinary notices. An explicit create
   * initializes libpq's native event and makes its later destroy responsible
   * for releasing the source role instead. */
  cpkt_postgres_event_release_uninitialized(result);
}

/** Release frozen metadata while the native address still belongs to this
 * result, but leave initialized dispatchers for libpq's valid DESTROY event. */
void cpkt_postgres_event_release_uninitialized(const PGresult *result) {
  cpkt_postgres_event_result *entry;
  int native_initialized;
  (void)pthread_mutex_lock(&cpkt_postgres_events_mutex);
  entry = cpkt_postgres_event_find_result(result);
  native_initialized = entry != NULL && entry->native_initialized;
  (void)pthread_mutex_unlock(&cpkt_postgres_events_mutex);
  if (!native_initialized)
    cpkt_postgres_event_result_release(result);
}

/** Freeze the source registration list for a native result copy. */
int cpkt_postgres_event_prepare_copy(const PGresult *source, PGresult *dest,
                                     int flags) {
  cpkt_postgres_event_result *source_entry;
  cpkt_postgres_event_result *dest_entry;
  if (source == NULL || dest == NULL || !(flags & PG_COPYRES_EVENTS))
    return 1;
  (void)pthread_mutex_lock(&cpkt_postgres_events_mutex);
  source_entry = cpkt_postgres_event_find_result(source);
  dest_entry = cpkt_postgres_event_find_result(dest);
  (void)pthread_mutex_unlock(&cpkt_postgres_events_mutex);
  if (source_entry != NULL && dest_entry == NULL)
    return cpkt_postgres_event_make_result(source_entry->owner, dest,
                                           source_entry) != NULL;
  return 1;
}

/** Drop bookkeeping when native result cleanup did not dispatch an event. */
void cpkt_postgres_event_result_release(const PGresult *native_result) {
  cpkt_postgres_event_result *result;
  cpkt_postgres_event_result **slot;
  cpkt_postgres_event_state *state;
  cpkt_postgres_event_owner *owner;
  int dispose;
  (void)pthread_mutex_lock(&cpkt_postgres_events_mutex);
  slot = &cpkt_postgres_event_results;
  while (*slot != NULL && (*slot)->native != native_result)
    slot = &(*slot)->next;
  result = *slot;
  if (result != NULL) {
    *slot = result->next;
    owner = result->owner;
    --owner->result_count;
    dispose = cpkt_postgres_event_claim_disposal_locked(owner);
  }
  (void)pthread_mutex_unlock(&cpkt_postgres_events_mutex);
  if (result == NULL)
    return;
  while (result->states != NULL) {
    state = result->states;
    result->states = state->next;
    free(state);
  }
  free(result);
  if (dispose)
    cpkt_postgres_event_dispose_owner(owner);
}
