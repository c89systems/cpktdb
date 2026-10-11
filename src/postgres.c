#include <cpkt/postgres.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "postgres_events_private.h"
#include <libpq-fe.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#endif

typedef char cpkt_postgres_oid_fits_public_type
    [(sizeof(Oid) <= sizeof(unsigned long)) ? 1 : -1];
typedef char
    cpkt_postgres_oid_is_32_bits[(sizeof(Oid) * CHAR_BIT == 32) ? 1 : -1];
typedef char
    cpkt_postgres_i64_is_64_bits[(sizeof(int64_t) * CHAR_BIT == 64) ? 1 : -1];

typedef struct cpkt_postgres_notice_snapshot {
  PGconn *connection;
  struct cpkt_postgres_notice_binding *owner;
  cpkt_postgres_notice_receiver receiver;
  void *receiver_context;
  cpkt_postgres_notice_processor processor;
  void *processor_context;
  struct cpkt_postgres_notice_snapshot *next;
} cpkt_postgres_notice_snapshot;

typedef struct cpkt_postgres_notice_binding {
  PGconn *connection;
  cpkt_postgres_diagnostic_sink diagnostic_callback;
  void *diagnostic_context;
  cpkt_postgres_trace_sink trace_callback;
  void *trace_context;
  cpkt_postgres_notice_snapshot *latest;
  cpkt_postgres_notice_snapshot *snapshots;
  size_t result_count;
  int closed;
  struct cpkt_postgres_notice_binding *next;
} cpkt_postgres_notice_binding;

typedef struct cpkt_postgres_result_binding {
  PGresult *result;
  cpkt_postgres_notice_binding *owner;
  struct cpkt_postgres_result_binding *next;
} cpkt_postgres_result_binding;

typedef struct cpkt_postgres_pending_clear {
  const PGresult *result;
  cpkt_postgres_notice_binding *owner;
#if defined(_WIN32)
  DWORD thread;
#else
  pthread_t thread;
#endif
  struct cpkt_postgres_pending_clear *next;
} cpkt_postgres_pending_clear;

typedef struct cpkt_postgres_oauth_request_state {
  cpkt_postgres_oauth_async async;
  cpkt_postgres_oauth_cleanup cleanup;
  void *user;
} cpkt_postgres_oauth_request_state;

static cpkt_postgres_notice_binding *cpkt_postgres_notice_bindings = NULL;
static cpkt_postgres_result_binding *cpkt_postgres_result_bindings = NULL;
static cpkt_postgres_callback_result *cpkt_postgres_callback_results = NULL;
static cpkt_postgres_pending_clear *cpkt_postgres_pending_clears = NULL;
static cpkt_postgres_thread_lock cpkt_postgres_thread_lock_callback = NULL;
static cpkt_postgres_ssl_key_password_hook
    cpkt_postgres_ssl_key_password_callback = NULL;
static cpkt_postgres_auth_data_hook cpkt_postgres_auth_data_callback = NULL;
static void *cpkt_postgres_auth_data_context = NULL;
static cpkt_postgres_diagnostic_sink cpkt_postgres_default_diagnostic_callback =
    NULL;
static void *cpkt_postgres_default_diagnostic_context = NULL;

#if defined(_WIN32)
static INIT_ONCE cpkt_postgres_hook_lock_once = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION cpkt_postgres_hook_lock;

static BOOL CALLBACK cpkt_postgres_initialize_hook_lock(PINIT_ONCE once,
                                                        PVOID parameter,
                                                        PVOID *context) {
  (void)once;
  (void)parameter;
  (void)context;
  InitializeCriticalSection(&cpkt_postgres_hook_lock);
  return TRUE;
}

static void cpkt_postgres_hook_lock_acquire(void) {
  InitOnceExecuteOnce(&cpkt_postgres_hook_lock_once,
                      cpkt_postgres_initialize_hook_lock, NULL, NULL);
  EnterCriticalSection(&cpkt_postgres_hook_lock);
}

static void cpkt_postgres_hook_lock_release(void) {
  LeaveCriticalSection(&cpkt_postgres_hook_lock);
}
#else
static pthread_mutex_t cpkt_postgres_hook_lock = PTHREAD_MUTEX_INITIALIZER;

static void cpkt_postgres_hook_lock_acquire(void) {
  (void)pthread_mutex_lock(&cpkt_postgres_hook_lock);
}

static void cpkt_postgres_hook_lock_release(void) {
  (void)pthread_mutex_unlock(&cpkt_postgres_hook_lock);
}
#endif

static cpkt_postgres_notice_binding *
cpkt_postgres_find_notice_binding(const PGconn *connection) {
  cpkt_postgres_notice_binding *binding;

  binding = cpkt_postgres_notice_bindings;
  while (binding != NULL && binding->connection != connection) {
    binding = binding->next;
  }
  return binding;
}

static void
cpkt_postgres_dispose_notice_binding(cpkt_postgres_notice_binding *binding);

/** Match only the thread executing the native clear callback. */
static int cpkt_postgres_pending_clear_is_current(
    const cpkt_postgres_pending_clear *entry) {
#if defined(_WIN32)
  return entry->thread == GetCurrentThreadId();
#else
  return pthread_equal(entry->thread, pthread_self()) != 0;
#endif
}

/* Transfer an already-held owner reference to a stack-local clear scope.
 * No allocation may be needed here: result tracking can fail before clear. */
static void
cpkt_postgres_pending_clear_begin_locked(const PGresult *result,
                                         cpkt_postgres_notice_binding *owner,
                                         cpkt_postgres_pending_clear *scope) {
  scope->result = result;
  scope->owner = owner;
  if (owner == NULL)
    return;
#if defined(_WIN32)
  scope->thread = GetCurrentThreadId();
#else
  scope->thread = pthread_self();
#endif
  scope->next = cpkt_postgres_pending_clears;
  cpkt_postgres_pending_clears = scope;
}

static void
cpkt_postgres_pending_clear_end(cpkt_postgres_pending_clear *scope) {
  cpkt_postgres_pending_clear **slot;
  cpkt_postgres_notice_binding *dispose;
  if (scope->owner == NULL)
    return;
  cpkt_postgres_hook_lock_acquire();
  slot = &cpkt_postgres_pending_clears;
  while (*slot != scope)
    slot = &(*slot)->next;
  *slot = scope->next;
  --scope->owner->result_count;
  dispose = scope->owner->closed && scope->owner->result_count == 0U
                ? scope->owner
                : NULL;
  cpkt_postgres_hook_lock_release();
  cpkt_postgres_dispose_notice_binding(dispose);
}

/** Find the notice owner of a lasting or callback-local result under lock. */
static cpkt_postgres_notice_binding *
cpkt_postgres_find_result_notice_owner_locked(const PGresult *source) {
  cpkt_postgres_result_binding *result;
  cpkt_postgres_callback_result *callback;
  cpkt_postgres_pending_clear *pending;
  for (result = cpkt_postgres_result_bindings; result != NULL;
       result = result->next) {
    if (result->result == source)
      return result->owner;
  }
  for (callback = cpkt_postgres_callback_results; callback != NULL;
       callback = callback->next) {
    if (callback->result == source)
      return callback->owner;
  }
  for (pending = cpkt_postgres_pending_clears; pending != NULL;
       pending = pending->next) {
    if (pending->result == source &&
        cpkt_postgres_pending_clear_is_current(pending))
      return pending->owner;
  }
  return NULL;
}

/** Pin a notice owner and expose an event callback's borrowed result. */
void cpkt_postgres_notice_callback_begin(const PGconn *connection,
                                         const PGresult *source,
                                         const PGresult *borrowed,
                                         cpkt_postgres_callback_result *scope) {
  cpkt_postgres_notice_binding *owner;
  scope->result = borrowed;
  scope->owner = NULL;
  scope->next = NULL;
  cpkt_postgres_hook_lock_acquire();
  owner =
      connection == NULL ? NULL : cpkt_postgres_find_notice_binding(connection);
  if (source != NULL)
    owner = cpkt_postgres_find_result_notice_owner_locked(source);
  if (owner != NULL) {
    scope->owner = owner;
    scope->next = cpkt_postgres_callback_results;
    cpkt_postgres_callback_results = scope;
    ++owner->result_count;
  }
  cpkt_postgres_hook_lock_release();
}

/** Release a callback-local notice borrow after all nested calls return. */
void cpkt_postgres_notice_callback_end(cpkt_postgres_callback_result *scope) {
  cpkt_postgres_callback_result **slot;
  cpkt_postgres_notice_binding *dispose;
  if (scope->owner == NULL)
    return;
  cpkt_postgres_hook_lock_acquire();
  slot = &cpkt_postgres_callback_results;
  while (*slot != scope)
    slot = &(*slot)->next;
  *slot = scope->next;
  --scope->owner->result_count;
  dispose = scope->owner->closed && scope->owner->result_count == 0U
                ? scope->owner
                : NULL;
  cpkt_postgres_hook_lock_release();
  cpkt_postgres_dispose_notice_binding(dispose);
}

static cpkt_postgres_notice_binding *
cpkt_postgres_ensure_notice_binding(PGconn *connection) {
  cpkt_postgres_notice_binding *binding;

  cpkt_postgres_hook_lock_acquire();
  binding = cpkt_postgres_find_notice_binding(connection);
  if (binding == NULL) {
    binding = (cpkt_postgres_notice_binding *)calloc(1, sizeof(*binding));
    if (binding != NULL) {
      binding->connection = connection;
      binding->next = cpkt_postgres_notice_bindings;
      cpkt_postgres_notice_bindings = binding;
    }
  }
  cpkt_postgres_hook_lock_release();
  return binding;
}

static void
cpkt_postgres_dispose_notice_binding(cpkt_postgres_notice_binding *binding) {
  cpkt_postgres_notice_snapshot *snapshot;
  cpkt_postgres_notice_snapshot *next;

  if (binding == NULL) {
    return;
  }
  snapshot = binding->snapshots;
  while (snapshot != NULL) {
    next = snapshot->next;
    free(snapshot);
    snapshot = next;
  }
  free(binding);
}

static cpkt_postgres_notice_binding *
cpkt_postgres_detach_notice_binding(PGconn *connection) {
  cpkt_postgres_notice_binding **slot;
  cpkt_postgres_notice_binding *binding;
  cpkt_postgres_notice_snapshot *snapshot;

  cpkt_postgres_hook_lock_acquire();
  slot = &cpkt_postgres_notice_bindings;
  while (*slot != NULL && (*slot)->connection != connection) {
    slot = &(*slot)->next;
  }
  binding = *slot;
  if (binding != NULL) {
    *slot = binding->next;
    binding->closed = 1;
    ++binding->result_count; /* Pin snapshots while PQfinish runs. */
    for (snapshot = binding->snapshots; snapshot != NULL;
         snapshot = snapshot->next) {
      snapshot->connection = NULL;
    }
  }
  cpkt_postgres_hook_lock_release();
  return binding;
}

static void cpkt_postgres_release_detached_notice_binding(
    cpkt_postgres_notice_binding *binding) {
  int dispose;
  if (binding == NULL) {
    return;
  }
  cpkt_postgres_hook_lock_acquire();
  --binding->result_count;
  dispose = binding->result_count == 0U;
  cpkt_postgres_hook_lock_release();
  if (dispose) {
    cpkt_postgres_dispose_notice_binding(binding);
  }
}

/* A PGresult copies libpq's notice arguments when it is created. Keep every
 * snapshot for a connection until that connection and its results are gone. */
static void
cpkt_postgres_record_retrieval_failure(cpkt_postgres_retrieval_error *error,
                                       cpkt_postgres_retrieval_error_code code,
                                       const PGresult *result) {
  const char *sqlstate;
  const char *message;
  size_t length;
  if (error == NULL)
    return;
  error->code = code;
  if (result == NULL)
    return;
  error->result_discarded = 1;
  error->discarded_status = (cpkt_postgres_result_status)PQresultStatus(result);
  sqlstate = PQresultErrorField(result, PG_DIAG_SQLSTATE);
  if (sqlstate != NULL) {
    length = strlen(sqlstate);
    if (length >= sizeof(error->sqlstate))
      length = sizeof(error->sqlstate) - 1U;
    memcpy(error->sqlstate, sqlstate, length);
    error->sqlstate[length] = '\0';
  }
  message = PQresultErrorMessage(result);
  if (message != NULL) {
    length = strlen(message);
    error->message_truncated = length >= sizeof(error->server_message);
    if (error->message_truncated)
      length = sizeof(error->server_message) - 1U;
    memcpy(error->server_message, message, length);
    error->server_message[length] = '\0';
  }
}

static cpkt_postgres_result *
cpkt_postgres_track_result_checked(PGresult *result, const PGconn *connection,
                                   const PGresult *source,
                                   cpkt_postgres_retrieval_error *error) {
  cpkt_postgres_notice_binding *binding;
  cpkt_postgres_result_binding *entry;
  cpkt_postgres_pending_clear pending;

  if (result == NULL) {
    return NULL;
  }
  cpkt_postgres_hook_lock_acquire();
  binding =
      connection == NULL ? NULL : cpkt_postgres_find_notice_binding(connection);
  if (source != NULL)
    binding = cpkt_postgres_find_result_notice_owner_locked(source);
  entry = NULL;
  if (binding != NULL) {
    entry =
        (cpkt_postgres_result_binding *)cpkt_postgres_result_binding_allocate(
            sizeof(*entry));
    if (entry != NULL) {
      entry->result = result;
      entry->owner = binding;
      entry->next = cpkt_postgres_result_bindings;
      cpkt_postgres_result_bindings = entry;
    } else {
      cpkt_postgres_pending_clear_begin_locked(result, binding, &pending);
    }
    ++binding->result_count;
  }
  cpkt_postgres_hook_lock_release();
  if (binding != NULL && entry == NULL) {
    cpkt_postgres_record_retrieval_failure(
        error, CPKT_POSTGRES_RETRIEVAL_ERROR_NOTICE_BINDING, result);
    cpkt_postgres_event_release_uninitialized(result);
    PQclear(result);
    cpkt_postgres_pending_clear_end(&pending);
    return NULL;
  }
  if (!cpkt_postgres_event_prepare_result((PGconn *)connection, result)) {
    cpkt_postgres_record_retrieval_failure(
        error, CPKT_POSTGRES_RETRIEVAL_ERROR_EVENT_PREPARATION, result);
    cpkt_postgres_result_free((cpkt_postgres_result *)result);
    return NULL;
  }
  return (cpkt_postgres_result *)result;
}

static cpkt_postgres_result *
cpkt_postgres_track_result(PGresult *result, const PGconn *connection,
                           const PGresult *source) {
  return cpkt_postgres_track_result_checked(result, connection, source, NULL);
}

static void cpkt_postgres_native_notice_receiver(void *argument,
                                                 const PGresult *result) {
  cpkt_postgres_notice_snapshot *snapshot;
  cpkt_postgres_callback_result callback_result;
  cpkt_postgres_callback_result **slot;
  cpkt_postgres_notice_binding *dispose;
  cpkt_postgres_notice_receiver callback;
  void *context;

  cpkt_postgres_hook_lock_acquire();
  snapshot = (cpkt_postgres_notice_snapshot *)argument;
  callback_result.result = result;
  callback_result.owner = snapshot->owner;
  callback_result.next = cpkt_postgres_callback_results;
  cpkt_postgres_callback_results = &callback_result;
  ++callback_result.owner->result_count;
  callback = snapshot->receiver;
  context = snapshot->receiver_context;
  argument = snapshot->connection;
  cpkt_postgres_hook_lock_release();
  /* Do not deliver a borrow that could be copied with native event context
   * but without a retained facade owner after bookkeeping allocation fails. */
  if (callback != NULL) {
    if (cpkt_postgres_event_prepare_notice((PGconn *)argument, result)) {
      callback(context, (cpkt_postgres_connection *)argument,
               (const cpkt_postgres_result *)result);
      cpkt_postgres_event_release_notice(result);
    }
  }
  cpkt_postgres_hook_lock_acquire();
  slot = &cpkt_postgres_callback_results;
  while (*slot != &callback_result)
    slot = &(*slot)->next;
  *slot = callback_result.next;
  --callback_result.owner->result_count;
  dispose =
      callback_result.owner->closed && callback_result.owner->result_count == 0U
          ? callback_result.owner
          : NULL;
  cpkt_postgres_hook_lock_release();
  cpkt_postgres_dispose_notice_binding(dispose);
}

static void cpkt_postgres_native_notice_processor(void *argument,
                                                  const char *message) {
  cpkt_postgres_notice_snapshot *snapshot;
  cpkt_postgres_notice_processor callback;
  void *context;

  cpkt_postgres_hook_lock_acquire();
  snapshot = (cpkt_postgres_notice_snapshot *)argument;
  callback = snapshot->processor;
  context = snapshot->processor_context;
  argument = snapshot->connection;
  cpkt_postgres_hook_lock_release();
  if (callback != NULL) {
    callback(context, (cpkt_postgres_connection *)argument, message);
  }
}

static void cpkt_postgres_native_default_diagnostic(void *argument,
                                                    PGconn *connection,
                                                    int level,
                                                    const char *message) {
  cpkt_postgres_diagnostic_sink callback;
  void *context;

  (void)argument;
  cpkt_postgres_hook_lock_acquire();
  callback = cpkt_postgres_default_diagnostic_callback;
  context = cpkt_postgres_default_diagnostic_context;
  cpkt_postgres_hook_lock_release();
  if (callback != NULL) {
    callback(context, (cpkt_postgres_connection *)connection, level, message);
  }
}

static void cpkt_postgres_native_connection_diagnostic(void *argument,
                                                       PGconn *connection,
                                                       int level,
                                                       const char *message) {
  cpkt_postgres_notice_binding *binding;
  cpkt_postgres_diagnostic_sink callback;
  void *context;

  binding = (cpkt_postgres_notice_binding *)argument;
  cpkt_postgres_hook_lock_acquire();
  callback = binding->diagnostic_callback;
  context = binding->diagnostic_context;
  cpkt_postgres_hook_lock_release();
  if (callback != NULL) {
    callback(context, (cpkt_postgres_connection *)connection, level, message);
  }
}

static void cpkt_postgres_native_trace(void *argument, PGconn *connection,
                                       const char *record, size_t length) {
  cpkt_postgres_notice_binding *binding;
  cpkt_postgres_trace_sink callback;
  void *context;

  binding = (cpkt_postgres_notice_binding *)argument;
  cpkt_postgres_hook_lock_acquire();
  callback = binding->trace_callback;
  context = binding->trace_context;
  cpkt_postgres_hook_lock_release();
  if (callback != NULL) {
    callback(context, (cpkt_postgres_connection *)connection, record, length);
  }
}

static void cpkt_postgres_native_thread_lock(int acquire) {
  cpkt_postgres_thread_lock callback;

  cpkt_postgres_hook_lock_acquire();
  callback = cpkt_postgres_thread_lock_callback;
  cpkt_postgres_hook_lock_release();
  if (callback != NULL) {
    callback(acquire);
  }
}

static int cpkt_postgres_native_ssl_key_password_hook(char *buffer,
                                                      int buffer_size,
                                                      PGconn *connection) {
  cpkt_postgres_ssl_key_password_hook callback;

  cpkt_postgres_hook_lock_acquire();
  callback = cpkt_postgres_ssl_key_password_callback;
  cpkt_postgres_hook_lock_release();
  if (callback == NULL) {
    return PQdefaultSSLKeyPassHook_OpenSSL(buffer, buffer_size, connection);
  }
  return callback(buffer, buffer_size, (cpkt_postgres_connection *)connection);
}

#if defined(_WIN32)
static cpkt_postgres_async_socket cpkt_postgres_public_socket(SOCKET value) {
  uint64_t bits;
  cpkt_postgres_async_socket result;

  bits = (uint64_t)value;
  result.high = (unsigned long)((bits >> 32) & 4294967295UL);
  result.low = (unsigned long)(bits & 4294967295UL);
  return result;
}

static SOCKET cpkt_postgres_native_socket(cpkt_postgres_async_socket value) {
  uint64_t bits;

  bits = ((uint64_t)(value.high & 4294967295UL) << 32) |
         (uint64_t)(value.low & 4294967295UL);
  return (SOCKET)bits;
}
#else
static cpkt_postgres_async_socket cpkt_postgres_public_socket(int value) {
  cpkt_postgres_async_socket result;

  result.high = 0;
  result.low = (unsigned long)value;
  return result;
}

static int cpkt_postgres_native_socket(cpkt_postgres_async_socket value) {
  return (int)value.low;
}
#endif

static void cpkt_postgres_copy_oauth_request_from_native(
    cpkt_postgres_oauth_bearer_request *destination,
    const PGoauthBearerRequest *source,
    cpkt_postgres_oauth_request_state *state) {
  destination->openid_configuration = source->openid_configuration;
  destination->scope = source->scope;
  destination->async = state == NULL ? NULL : state->async;
  destination->cleanup = state == NULL ? NULL : state->cleanup;
  destination->token = source->token;
  destination->user = state == NULL ? NULL : state->user;
}

static void cpkt_postgres_copy_oauth_request_to_native(
    PGoauthBearerRequest *destination,
    const cpkt_postgres_oauth_bearer_request *source,
    cpkt_postgres_oauth_request_state *state) {
  state->async = source->async;
  state->cleanup = source->cleanup;
  state->user = source->user;
  destination->token = source->token;
}

static PostgresPollingStatusType
cpkt_postgres_native_oauth_async(PGconn *connection,
                                 PGoauthBearerRequest *native_request,
#if defined(_WIN32)
                                 SOCKET *native_socket
#else
                                 int *native_socket
#endif
) {
  cpkt_postgres_oauth_request_state *state;
  cpkt_postgres_oauth_bearer_request request;
  cpkt_postgres_async_socket socket;
  cpkt_postgres_poll_status status;

  state = (cpkt_postgres_oauth_request_state *)native_request->user;
  if (state == NULL || state->async == NULL) {
    return PGRES_POLLING_FAILED;
  }
  cpkt_postgres_copy_oauth_request_from_native(&request, native_request, state);
  socket = cpkt_postgres_public_socket(*native_socket);
  status =
      state->async((cpkt_postgres_connection *)connection, &request, &socket);
  cpkt_postgres_copy_oauth_request_to_native(native_request, &request, state);
  *native_socket = cpkt_postgres_native_socket(socket);
  return (PostgresPollingStatusType)status;
}

static void
cpkt_postgres_native_oauth_cleanup(PGconn *connection,
                                   PGoauthBearerRequest *native_request) {
  cpkt_postgres_oauth_request_state *state;
  cpkt_postgres_oauth_bearer_request request;

  state = (cpkt_postgres_oauth_request_state *)native_request->user;
  if (state == NULL) {
    return;
  }
  cpkt_postgres_copy_oauth_request_from_native(&request, native_request, state);
  if (state->cleanup != NULL) {
    state->cleanup((cpkt_postgres_connection *)connection, &request);
  }
  native_request->user = NULL;
  free(state);
}

static int cpkt_postgres_native_auth_data_hook(PGauthData kind,
                                               PGconn *connection, void *data) {
  cpkt_postgres_auth_data_hook callback;
  void *context;
  int status;

  cpkt_postgres_hook_lock_acquire();
  callback = cpkt_postgres_auth_data_callback;
  context = cpkt_postgres_auth_data_context;
  cpkt_postgres_hook_lock_release();
  if (callback == NULL) {
    return PQdefaultAuthDataHook(kind, connection, data);
  }
  if (kind == PQAUTHDATA_PROMPT_OAUTH_DEVICE) {
    PGpromptOAuthDevice *native_prompt;
    cpkt_postgres_oauth_device_prompt prompt;

    native_prompt = (PGpromptOAuthDevice *)data;
    prompt.verification_uri = native_prompt->verification_uri;
    prompt.user_code = native_prompt->user_code;
    prompt.verification_uri_complete = native_prompt->verification_uri_complete;
    prompt.expires_in = native_prompt->expires_in;
    status = callback(CPKT_POSTGRES_AUTH_DATA_PROMPT_OAUTH_DEVICE,
                      (cpkt_postgres_connection *)connection, &prompt, context);
  } else if (kind == PQAUTHDATA_OAUTH_BEARER_TOKEN) {
    PGoauthBearerRequest *native_request;
    cpkt_postgres_oauth_bearer_request request;
    cpkt_postgres_oauth_request_state *state;

    native_request = (PGoauthBearerRequest *)data;
    cpkt_postgres_copy_oauth_request_from_native(&request, native_request,
                                                 NULL);
    status =
        callback(CPKT_POSTGRES_AUTH_DATA_OAUTH_BEARER_TOKEN,
                 (cpkt_postgres_connection *)connection, &request, context);
    if (status > 0) {
      state = (cpkt_postgres_oauth_request_state *)calloc(1, sizeof(*state));
      if (state == NULL) {
        if (request.cleanup != NULL)
          request.cleanup((cpkt_postgres_connection *)connection, &request);
        return -1;
      }
      cpkt_postgres_copy_oauth_request_to_native(native_request, &request,
                                                 state);
      native_request->async =
          request.async == NULL ? NULL : cpkt_postgres_native_oauth_async;
      native_request->cleanup = cpkt_postgres_native_oauth_cleanup;
      native_request->user = state;
    } else if (status < 0 && request.cleanup != NULL) {
      request.cleanup((cpkt_postgres_connection *)connection, &request);
    }
  } else {
    return PQdefaultAuthDataHook(kind, connection, data);
  }
  if (status == 0) {
    return PQdefaultAuthDataHook(kind, connection, data);
  }
  return status;
}

static PGconn *
cpkt_postgres_native_connection(cpkt_postgres_connection *connection) {
  return (PGconn *)connection;
}

static const PGconn *cpkt_postgres_native_connection_const(
    const cpkt_postgres_connection *connection) {
  return (const PGconn *)connection;
}

static PGresult *cpkt_postgres_native_result(cpkt_postgres_result *result) {
  return (PGresult *)result;
}

static const PGresult *
cpkt_postgres_native_result_const(const cpkt_postgres_result *result) {
  return (const PGresult *)result;
}

static Oid *cpkt_postgres_native_oids(const cpkt_postgres_oid *oids,
                                      int count) {
  Oid *native_oids;
  int index;

  if (oids == NULL || count <= 0) {
    return NULL;
  }
  native_oids = (Oid *)malloc((size_t)count * sizeof(*native_oids));
  if (native_oids == NULL) {
    return NULL;
  }
  for (index = 0; index < count; ++index) {
    if (oids[index] > 4294967295UL) {
      free(native_oids);
      return NULL;
    }
    native_oids[index] = (Oid)oids[index];
  }
  return native_oids;
}

static int cpkt_postgres_native_oid(cpkt_postgres_oid value, Oid *out) {
  if (value > 4294967295UL) {
    return 0;
  }
  *out = (Oid)value;
  return 1;
}

static int64_t cpkt_postgres_native_i64(cpkt_postgres_i64 value) {
  uint64_t bits;

  bits = ((uint64_t)(value.high & 4294967295UL) << 32) |
         (uint64_t)(value.low & 4294967295UL);
  return (int64_t)bits;
}

static cpkt_postgres_i64 cpkt_postgres_public_i64(int64_t value) {
  uint64_t bits;
  cpkt_postgres_i64 result;

  bits = (uint64_t)value;
  result.high = (unsigned long)((bits >> 32) & 4294967295UL);
  result.low = (unsigned long)(bits & 4294967295UL);
  return result;
}

static char *cpkt_postgres_copy_text(const char *text) {
  char *copy;
  size_t length;

  if (text == NULL) {
    return NULL;
  }
  length = strlen(text) + 1;
  copy = (char *)malloc(length);
  if (copy != NULL) {
    memcpy(copy, text, length);
  }
  return copy;
}

static cpkt_postgres_result *cpkt_postgres_receiver_tx(cpkt_postgres *self,
                                                       const char *query) {
  return cpkt_postgres_execute(self->connection, query);
}

static cpkt_postgres_result *cpkt_postgres_receiver_tx_params(
    cpkt_postgres *self, const char *command, int parameter_count,
    const cpkt_postgres_oid *parameter_types,
    const char *const *parameter_values, const int *parameter_lengths,
    const int *parameter_formats, int result_format) {
  return cpkt_postgres_execute_params(
      self->connection, command, parameter_count, parameter_types,
      parameter_values, parameter_lengths, parameter_formats, result_format);
}

static int cpkt_postgres_receiver_send(cpkt_postgres *self, const char *query) {
  return cpkt_postgres_send_query(self->connection, query);
}

static int cpkt_postgres_receiver_send_params(
    cpkt_postgres *self, const char *command, int parameter_count,
    const cpkt_postgres_oid *parameter_types,
    const char *const *parameter_values, const int *parameter_lengths,
    const int *parameter_formats, int result_format) {
  return cpkt_postgres_send_query_params(
      self->connection, command, parameter_count, parameter_types,
      parameter_values, parameter_lengths, parameter_formats, result_format);
}

static cpkt_postgres_result *
cpkt_postgres_receiver_receive(cpkt_postgres *self) {
  return cpkt_postgres_get_result(self->connection);
}

static int cpkt_postgres_receiver_consume(cpkt_postgres *self) {
  return cpkt_postgres_consume_input(self->connection);
}

static int cpkt_postgres_receiver_busy(cpkt_postgres *self) {
  return cpkt_postgres_is_busy(self->connection);
}

static int cpkt_postgres_receiver_flush(cpkt_postgres *self) {
  return cpkt_postgres_flush(self->connection);
}

static int cpkt_postgres_receiver_set_nonblocking(cpkt_postgres *self,
                                                  int enabled) {
  return cpkt_postgres_set_nonblocking(self->connection, enabled);
}

static int cpkt_postgres_receiver_is_nonblocking(const cpkt_postgres *self) {
  return cpkt_postgres_is_nonblocking(self->connection);
}

static int cpkt_postgres_receiver_begin_pipeline(cpkt_postgres *self) {
  return cpkt_postgres_enter_pipeline_mode(self->connection);
}

static int cpkt_postgres_receiver_end_pipeline(cpkt_postgres *self) {
  return cpkt_postgres_exit_pipeline_mode(self->connection);
}

static int cpkt_postgres_receiver_pipeline_sync(cpkt_postgres *self) {
  return cpkt_postgres_pipeline_sync(self->connection);
}

static int cpkt_postgres_receiver_copy_write(cpkt_postgres *self,
                                             const char *bytes,
                                             int byte_count) {
  return cpkt_postgres_put_copy_data(self->connection, bytes, byte_count);
}

static int cpkt_postgres_receiver_copy_finish(cpkt_postgres *self,
                                              const char *error_message) {
  return cpkt_postgres_put_copy_end(self->connection, error_message);
}

static int cpkt_postgres_receiver_copy_read(cpkt_postgres *self,
                                            char **bytes_out,
                                            int asynchronous) {
  return cpkt_postgres_get_copy_data(self->connection, bytes_out, asynchronous);
}

static int cpkt_postgres_receiver_reset_start(cpkt_postgres *self) {
  return cpkt_postgres_reset_start(self->connection);
}

static cpkt_postgres_poll_status
cpkt_postgres_receiver_reset_poll(cpkt_postgres *self) {
  return cpkt_postgres_reset_poll(self->connection);
}

static void cpkt_postgres_receiver_reset(cpkt_postgres *self) {
  cpkt_postgres_reset(self->connection);
}

static cpkt_postgres_connection_status
cpkt_postgres_receiver_status(const cpkt_postgres *self) {
  return cpkt_postgres_connection_status_get(self->connection);
}

static char *cpkt_postgres_receiver_error(const cpkt_postgres *self) {
  return cpkt_postgres_error_message(self->connection);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_close. */
void cpkt_postgres_close(cpkt_postgres *self) {
  if (self == NULL) {
    return;
  }
  if (self->connection != NULL) {
    cpkt_postgres_connection_free(self->connection);
    self->connection = NULL;
  }
  free(self);
}

static cpkt_postgres *
cpkt_postgres_receiver_new(cpkt_postgres_connection *connection) {
  cpkt_postgres *self;

  if (connection == NULL) {
    return NULL;
  }
  self = (cpkt_postgres *)calloc(1, sizeof(*self));
  if (self == NULL) {
    cpkt_postgres_connection_free(connection);
    return NULL;
  }
  self->tx = cpkt_postgres_receiver_tx;
  self->tx_params = cpkt_postgres_receiver_tx_params;
  self->send = cpkt_postgres_receiver_send;
  self->send_params = cpkt_postgres_receiver_send_params;
  self->receive = cpkt_postgres_receiver_receive;
  self->consume = cpkt_postgres_receiver_consume;
  self->busy = cpkt_postgres_receiver_busy;
  self->flush = cpkt_postgres_receiver_flush;
  self->set_nonblocking = cpkt_postgres_receiver_set_nonblocking;
  self->is_nonblocking = cpkt_postgres_receiver_is_nonblocking;
  self->begin_pipeline = cpkt_postgres_receiver_begin_pipeline;
  self->end_pipeline = cpkt_postgres_receiver_end_pipeline;
  self->pipeline_sync = cpkt_postgres_receiver_pipeline_sync;
  self->copy_write = cpkt_postgres_receiver_copy_write;
  self->copy_finish = cpkt_postgres_receiver_copy_finish;
  self->copy_read = cpkt_postgres_receiver_copy_read;
  self->reset_start = cpkt_postgres_receiver_reset_start;
  self->reset_poll = cpkt_postgres_receiver_reset_poll;
  self->reset = cpkt_postgres_receiver_reset;
  self->status = cpkt_postgres_receiver_status;
  self->error = cpkt_postgres_receiver_error;
  self->close = cpkt_postgres_close;
  self->connection = connection;
  return self;
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_new. */
cpkt_postgres *cpkt_postgres_new(const char *connection_info) {
  return cpkt_postgres_receiver_new(cpkt_postgres_connect(connection_info));
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_new_params. */
cpkt_postgres *cpkt_postgres_new_params(const char *const *keywords,
                                        const char *const *values,
                                        int expand_database_name) {
  return cpkt_postgres_receiver_new(
      cpkt_postgres_connect_params(keywords, values, expand_database_name));
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_connect_start. */
cpkt_postgres_connection *
cpkt_postgres_connect_start(const char *connection_info) {
  return (cpkt_postgres_connection *)PQconnectStart(connection_info);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_connect_start_params. */
cpkt_postgres_connection *
cpkt_postgres_connect_start_params(const char *const *keywords,
                                   const char *const *values,
                                   int expand_database_name) {
  return (cpkt_postgres_connection *)PQconnectStartParams(keywords, values,
                                                          expand_database_name);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_connect_poll. */
cpkt_postgres_poll_status
cpkt_postgres_connect_poll(cpkt_postgres_connection *connection) {
  return (cpkt_postgres_poll_status)PQconnectPoll(
      cpkt_postgres_native_connection(connection));
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_connect. */
cpkt_postgres_connection *cpkt_postgres_connect(const char *connection_info) {
  return (cpkt_postgres_connection *)PQconnectdb(connection_info);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_connect_params. */
cpkt_postgres_connection *
cpkt_postgres_connect_params(const char *const *keywords,
                             const char *const *values,
                             int expand_database_name) {
  return (cpkt_postgres_connection *)PQconnectdbParams(keywords, values,
                                                       expand_database_name);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_connect_login. */
cpkt_postgres_connection *cpkt_postgres_connect_login(
    const char *host, const char *port, const char *options, const char *tty,
    const char *database, const char *user, const char *password) {
  return (cpkt_postgres_connection *)PQsetdbLogin(host, port, options, tty,
                                                  database, user, password);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_connection_free. */
void cpkt_postgres_connection_free(cpkt_postgres_connection *connection) {
  PGconn *native_connection;
  cpkt_postgres_notice_binding *binding;

  if (connection == NULL) {
    return;
  }
  native_connection = cpkt_postgres_native_connection(connection);
  binding = cpkt_postgres_detach_notice_binding(native_connection);
  PQfinish(native_connection);
  cpkt_postgres_release_detached_notice_binding(binding);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_connection_defaults. */
cpkt_postgres_connection_option *cpkt_postgres_connection_defaults(void) {
  return (cpkt_postgres_connection_option *)PQconndefaults();
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_connection_parse. */
cpkt_postgres_connection_option *
cpkt_postgres_connection_parse(const char *connection_info,
                               char **error_message) {
  return (cpkt_postgres_connection_option *)PQconninfoParse(connection_info,
                                                            error_message);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_connection_options. */
cpkt_postgres_connection_option *
cpkt_postgres_connection_options(cpkt_postgres_connection *connection) {
  return (cpkt_postgres_connection_option *)PQconninfo(
      cpkt_postgres_native_connection(connection));
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_connection_options_free. */
void cpkt_postgres_connection_options_free(
    cpkt_postgres_connection_option *options) {
  PQconninfoFree((PQconninfoOption *)options);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_reset_start. */
int cpkt_postgres_reset_start(cpkt_postgres_connection *connection) {
  return PQresetStart(cpkt_postgres_native_connection(connection));
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_reset_poll. */
cpkt_postgres_poll_status
cpkt_postgres_reset_poll(cpkt_postgres_connection *connection) {
  return (cpkt_postgres_poll_status)PQresetPoll(
      cpkt_postgres_native_connection(connection));
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_reset. */
void cpkt_postgres_reset(cpkt_postgres_connection *connection) {
  PQreset(cpkt_postgres_native_connection(connection));
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_cancel_connection_create. */
cpkt_postgres_cancel_connection *
cpkt_postgres_cancel_connection_create(cpkt_postgres_connection *connection) {
  return (cpkt_postgres_cancel_connection *)PQcancelCreate(
      cpkt_postgres_native_connection(connection));
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_cancel_start. */
int cpkt_postgres_cancel_start(
    cpkt_postgres_cancel_connection *cancel_connection) {
  return PQcancelStart((PGcancelConn *)cancel_connection);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_cancel_blocking. */
int cpkt_postgres_cancel_blocking(
    cpkt_postgres_cancel_connection *cancel_connection) {
  return PQcancelBlocking((PGcancelConn *)cancel_connection);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_cancel_poll. */
cpkt_postgres_poll_status
cpkt_postgres_cancel_poll(cpkt_postgres_cancel_connection *cancel_connection) {
  return (cpkt_postgres_poll_status)PQcancelPoll(
      (PGcancelConn *)cancel_connection);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_cancel_status. */
cpkt_postgres_connection_status cpkt_postgres_cancel_status(
    const cpkt_postgres_cancel_connection *cancel_connection) {
  return (cpkt_postgres_connection_status)PQcancelStatus(
      (const PGcancelConn *)cancel_connection);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_cancel_socket. */
int cpkt_postgres_cancel_socket(
    const cpkt_postgres_cancel_connection *cancel_connection) {
  return PQcancelSocket((const PGcancelConn *)cancel_connection);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_cancel_error_message. */
char *cpkt_postgres_cancel_error_message(
    cpkt_postgres_cancel_connection *cancel_connection) {
  return PQcancelErrorMessage((const PGcancelConn *)cancel_connection);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_cancel_reset. */
void cpkt_postgres_cancel_reset(
    cpkt_postgres_cancel_connection *cancel_connection) {
  PQcancelReset((PGcancelConn *)cancel_connection);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_cancel_connection_free. */
void cpkt_postgres_cancel_connection_free(
    cpkt_postgres_cancel_connection *cancel_connection) {
  PQcancelFinish((PGcancelConn *)cancel_connection);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_cancel_create. */
cpkt_postgres_cancel *
cpkt_postgres_cancel_create(cpkt_postgres_connection *connection) {
  return (cpkt_postgres_cancel *)PQgetCancel(
      cpkt_postgres_native_connection(connection));
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_cancel_free. */
void cpkt_postgres_cancel_free(cpkt_postgres_cancel *cancel) {
  PQfreeCancel((PGcancel *)cancel);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_cancel_request. */
int cpkt_postgres_cancel_request(cpkt_postgres_cancel *cancel,
                                 char *error_buffer, int error_buffer_size) {
  return PQcancel((PGcancel *)cancel, error_buffer, error_buffer_size);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_request_cancel. */
int cpkt_postgres_request_cancel(cpkt_postgres_connection *connection) {
  return PQrequestCancel(cpkt_postgres_native_connection(connection));
}

#define CPKT_POSTGRES_CONNECTION_TEXT_WRAPPER(name, native_name)               \
  char *name(const cpkt_postgres_connection *connection) {                     \
    return native_name(cpkt_postgres_native_connection_const(connection));     \
  }

CPKT_POSTGRES_CONNECTION_TEXT_WRAPPER(cpkt_postgres_database, PQdb)
CPKT_POSTGRES_CONNECTION_TEXT_WRAPPER(cpkt_postgres_user, PQuser)
CPKT_POSTGRES_CONNECTION_TEXT_WRAPPER(cpkt_postgres_password, PQpass)
CPKT_POSTGRES_CONNECTION_TEXT_WRAPPER(cpkt_postgres_host, PQhost)
CPKT_POSTGRES_CONNECTION_TEXT_WRAPPER(cpkt_postgres_host_address, PQhostaddr)
CPKT_POSTGRES_CONNECTION_TEXT_WRAPPER(cpkt_postgres_port, PQport)
CPKT_POSTGRES_CONNECTION_TEXT_WRAPPER(cpkt_postgres_tty, PQtty)
CPKT_POSTGRES_CONNECTION_TEXT_WRAPPER(cpkt_postgres_options, PQoptions)
CPKT_POSTGRES_CONNECTION_TEXT_WRAPPER(cpkt_postgres_error_message,
                                      PQerrorMessage)

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_connection_status_get. */
cpkt_postgres_connection_status cpkt_postgres_connection_status_get(
    const cpkt_postgres_connection *connection) {
  return (cpkt_postgres_connection_status)PQstatus(
      cpkt_postgres_native_connection_const(connection));
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_transaction_status_get. */
cpkt_postgres_transaction_status cpkt_postgres_transaction_status_get(
    const cpkt_postgres_connection *connection) {
  return (cpkt_postgres_transaction_status)PQtransactionStatus(
      cpkt_postgres_native_connection_const(connection));
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_parameter_status. */
const char *
cpkt_postgres_parameter_status(const cpkt_postgres_connection *connection,
                               const char *name) {
  return PQparameterStatus(cpkt_postgres_native_connection_const(connection),
                           name);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_protocol_version. */
int cpkt_postgres_protocol_version(const cpkt_postgres_connection *connection) {
  return PQprotocolVersion(cpkt_postgres_native_connection_const(connection));
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_full_protocol_version. */
int cpkt_postgres_full_protocol_version(
    const cpkt_postgres_connection *connection) {
  return PQfullProtocolVersion(
      cpkt_postgres_native_connection_const(connection));
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_server_version. */
int cpkt_postgres_server_version(const cpkt_postgres_connection *connection) {
  return PQserverVersion(cpkt_postgres_native_connection_const(connection));
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_socket. */
int cpkt_postgres_socket(const cpkt_postgres_connection *connection) {
  return PQsocket(cpkt_postgres_native_connection_const(connection));
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_backend_pid. */
int cpkt_postgres_backend_pid(const cpkt_postgres_connection *connection) {
  return PQbackendPID(cpkt_postgres_native_connection_const(connection));
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_pipeline_status_get. */
cpkt_postgres_pipeline_status
cpkt_postgres_pipeline_status_get(const cpkt_postgres_connection *connection) {
  return (cpkt_postgres_pipeline_status)PQpipelineStatus(
      cpkt_postgres_native_connection_const(connection));
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_connection_needs_password. */
int cpkt_postgres_connection_needs_password(
    const cpkt_postgres_connection *connection) {
  return PQconnectionNeedsPassword(
      cpkt_postgres_native_connection_const(connection));
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_connection_used_password. */
int cpkt_postgres_connection_used_password(
    const cpkt_postgres_connection *connection) {
  return PQconnectionUsedPassword(
      cpkt_postgres_native_connection_const(connection));
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_connection_used_gssapi. */
int cpkt_postgres_connection_used_gssapi(
    const cpkt_postgres_connection *connection) {
  return PQconnectionUsedGSSAPI(
      cpkt_postgres_native_connection_const(connection));
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_client_encoding. */
int cpkt_postgres_client_encoding(const cpkt_postgres_connection *connection) {
  return PQclientEncoding(cpkt_postgres_native_connection_const(connection));
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_set_client_encoding. */
int cpkt_postgres_set_client_encoding(cpkt_postgres_connection *connection,
                                      const char *encoding) {
  return PQsetClientEncoding(cpkt_postgres_native_connection(connection),
                             encoding);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_tls_in_use. */
int cpkt_postgres_tls_in_use(cpkt_postgres_connection *connection) {
  return PQsslInUse(cpkt_postgres_native_connection(connection));
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_tls_object_get. */
cpkt_postgres_tls_object *
cpkt_postgres_tls_object_get(cpkt_postgres_connection *connection,
                             const char *object_name) {
  return (cpkt_postgres_tls_object *)PQsslStruct(
      cpkt_postgres_native_connection(connection), object_name);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_tls_attribute. */
const char *cpkt_postgres_tls_attribute(cpkt_postgres_connection *connection,
                                        const char *attribute_name) {
  return PQsslAttribute(cpkt_postgres_native_connection(connection),
                        attribute_name);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_tls_attribute_names. */
const char *const *
cpkt_postgres_tls_attribute_names(cpkt_postgres_connection *connection) {
  return PQsslAttributeNames(cpkt_postgres_native_connection(connection));
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_openssl_get. */
cpkt_postgres_tls_object *
cpkt_postgres_openssl_get(cpkt_postgres_connection *connection) {
  return (cpkt_postgres_tls_object *)PQgetssl(
      cpkt_postgres_native_connection(connection));
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_init_tls. */
void cpkt_postgres_init_tls(int initialize) { PQinitSSL(initialize); }
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_init_openssl. */
void cpkt_postgres_init_openssl(int initialize_tls, int initialize_crypto) {
  PQinitOpenSSL(initialize_tls, initialize_crypto);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_gss_encryption_in_use. */
int cpkt_postgres_gss_encryption_in_use(cpkt_postgres_connection *connection) {
  return PQgssEncInUse(cpkt_postgres_native_connection(connection));
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_gss_context_get. */
cpkt_postgres_gss_context *
cpkt_postgres_gss_context_get(cpkt_postgres_connection *connection) {
  return (cpkt_postgres_gss_context *)PQgetgssctx(
      cpkt_postgres_native_connection(connection));
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_set_error_verbosity. */
cpkt_postgres_verbosity
cpkt_postgres_set_error_verbosity(cpkt_postgres_connection *connection,
                                  cpkt_postgres_verbosity verbosity) {
  return (cpkt_postgres_verbosity)PQsetErrorVerbosity(
      cpkt_postgres_native_connection(connection), (PGVerbosity)verbosity);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_set_error_context_visibility. */
cpkt_postgres_context_visibility cpkt_postgres_set_error_context_visibility(
    cpkt_postgres_connection *connection,
    cpkt_postgres_context_visibility visibility) {
  return (cpkt_postgres_context_visibility)PQsetErrorContextVisibility(
      cpkt_postgres_native_connection(connection),
      (PGContextVisibility)visibility);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_set_notice_receiver. */
int cpkt_postgres_set_notice_receiver(
    cpkt_postgres_connection *connection,
    cpkt_postgres_notice_receiver callback, void *context,
    cpkt_postgres_notice_receiver *old_callback_out, void **old_context_out) {
  PGconn *native_connection;
  cpkt_postgres_notice_binding *binding;
  cpkt_postgres_notice_snapshot *snapshot;
  cpkt_postgres_notice_snapshot *old;

  if (old_callback_out != NULL) {
    *old_callback_out = NULL;
  }
  if (old_context_out != NULL) {
    *old_context_out = NULL;
  }
  if (connection == NULL) {
    return 0;
  }
  native_connection = cpkt_postgres_native_connection(connection);
  binding = cpkt_postgres_ensure_notice_binding(native_connection);
  if (binding == NULL) {
    return 0;
  }
  cpkt_postgres_hook_lock_acquire();
  old = binding->latest;
  if (old_callback_out != NULL) {
    *old_callback_out = old == NULL ? NULL : old->receiver;
  }
  if (old_context_out != NULL) {
    *old_context_out = old == NULL ? NULL : old->receiver_context;
  }
  snapshot = (cpkt_postgres_notice_snapshot *)calloc(1, sizeof(*snapshot));
  if (snapshot != NULL) {
    if (old != NULL) {
      snapshot->processor = old->processor;
      snapshot->processor_context = old->processor_context;
    }
    snapshot->connection = native_connection;
    snapshot->owner = binding;
    snapshot->receiver = callback;
    snapshot->receiver_context = context;
    snapshot->next = binding->snapshots;
    binding->snapshots = snapshot;
    binding->latest = snapshot;
  }
  cpkt_postgres_hook_lock_release();
  if (snapshot != NULL) {
    PQsetNoticeReceiver(native_connection, cpkt_postgres_native_notice_receiver,
                        snapshot);
  }
  return snapshot != NULL;
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_set_notice_processor. */
int cpkt_postgres_set_notice_processor(
    cpkt_postgres_connection *connection,
    cpkt_postgres_notice_processor callback, void *context,
    cpkt_postgres_notice_processor *old_callback_out, void **old_context_out) {
  PGconn *native_connection;
  cpkt_postgres_notice_binding *binding;
  cpkt_postgres_notice_snapshot *snapshot;
  cpkt_postgres_notice_snapshot *old;

  if (old_callback_out != NULL) {
    *old_callback_out = NULL;
  }
  if (old_context_out != NULL) {
    *old_context_out = NULL;
  }
  if (connection == NULL) {
    return 0;
  }
  native_connection = cpkt_postgres_native_connection(connection);
  binding = cpkt_postgres_ensure_notice_binding(native_connection);
  if (binding == NULL) {
    return 0;
  }
  cpkt_postgres_hook_lock_acquire();
  old = binding->latest;
  if (old_callback_out != NULL) {
    *old_callback_out = old == NULL ? NULL : old->processor;
  }
  if (old_context_out != NULL) {
    *old_context_out = old == NULL ? NULL : old->processor_context;
  }
  snapshot = (cpkt_postgres_notice_snapshot *)calloc(1, sizeof(*snapshot));
  if (snapshot != NULL) {
    if (old != NULL) {
      snapshot->receiver = old->receiver;
      snapshot->receiver_context = old->receiver_context;
    }
    snapshot->connection = native_connection;
    snapshot->owner = binding;
    snapshot->processor = callback;
    snapshot->processor_context = context;
    snapshot->next = binding->snapshots;
    binding->snapshots = snapshot;
    binding->latest = snapshot;
  }
  cpkt_postgres_hook_lock_release();
  if (snapshot != NULL) {
    PQsetNoticeProcessor(native_connection,
                         cpkt_postgres_native_notice_processor, snapshot);
  }
  return snapshot != NULL;
}

/** Installs the process default before connection option parsing. */
void cpkt_postgres_set_default_diagnostic_sink(
    cpkt_postgres_diagnostic_sink callback, void *context) {
  cpkt_postgres_hook_lock_acquire();
  cpkt_postgres_default_diagnostic_callback = callback;
  cpkt_postgres_default_diagnostic_context = context;
  cpkt_postgres_hook_lock_release();
  PQsetDefaultDiagnosticProcessor(
      callback != NULL ? cpkt_postgres_native_default_diagnostic : NULL, NULL);
}

/** Installs a connection-specific client diagnostic sink. */
int cpkt_postgres_set_diagnostic_sink(cpkt_postgres_connection *connection,
                                      cpkt_postgres_diagnostic_sink callback,
                                      void *context) {
  PGconn *native_connection;
  cpkt_postgres_notice_binding *binding;

  if (connection == NULL) {
    return 0;
  }
  native_connection = cpkt_postgres_native_connection(connection);
  binding = NULL;
  if (callback != NULL) {
    binding = cpkt_postgres_ensure_notice_binding(native_connection);
    if (binding == NULL) {
      return 0;
    }
  } else {
    cpkt_postgres_hook_lock_acquire();
    binding = cpkt_postgres_find_notice_binding(native_connection);
    cpkt_postgres_hook_lock_release();
  }
  if (binding != NULL) {
    cpkt_postgres_hook_lock_acquire();
    binding->diagnostic_callback = callback;
    binding->diagnostic_context = context;
    cpkt_postgres_hook_lock_release();
  }
  PQsetDiagnosticProcessor(
      native_connection,
      callback != NULL ? cpkt_postgres_native_connection_diagnostic : NULL,
      binding);
  return 1;
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_register_thread_lock. */
cpkt_postgres_thread_lock
cpkt_postgres_register_thread_lock(cpkt_postgres_thread_lock callback) {
  cpkt_postgres_thread_lock old_callback;

  cpkt_postgres_hook_lock_acquire();
  old_callback = cpkt_postgres_thread_lock_callback;
  cpkt_postgres_thread_lock_callback = callback;
  cpkt_postgres_hook_lock_release();
  PQregisterThreadLock(callback == NULL ? NULL
                                        : cpkt_postgres_native_thread_lock);
  return old_callback;
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_set_ssl_key_password_hook. */
cpkt_postgres_ssl_key_password_hook cpkt_postgres_set_ssl_key_password_hook(
    cpkt_postgres_ssl_key_password_hook callback) {
  cpkt_postgres_ssl_key_password_hook old_callback;

  cpkt_postgres_hook_lock_acquire();
  old_callback = cpkt_postgres_ssl_key_password_callback;
  cpkt_postgres_ssl_key_password_callback = callback;
  cpkt_postgres_hook_lock_release();
  PQsetSSLKeyPassHook_OpenSSL(
      callback == NULL ? NULL : cpkt_postgres_native_ssl_key_password_hook);
  return old_callback;
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_get_ssl_key_password_hook. */
cpkt_postgres_ssl_key_password_hook
cpkt_postgres_get_ssl_key_password_hook(void) {
  cpkt_postgres_ssl_key_password_hook callback;
  PQsslKeyPassHook_OpenSSL_type native_callback;

  native_callback = PQgetSSLKeyPassHook_OpenSSL();
  cpkt_postgres_hook_lock_acquire();
  callback = native_callback == cpkt_postgres_native_ssl_key_password_hook
                 ? cpkt_postgres_ssl_key_password_callback
                 : NULL;
  cpkt_postgres_hook_lock_release();
  return callback;
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_default_ssl_key_password_hook. */
int cpkt_postgres_default_ssl_key_password_hook(
    char *buffer, int buffer_size, cpkt_postgres_connection *connection) {
  return PQdefaultSSLKeyPassHook_OpenSSL(
      buffer, buffer_size, cpkt_postgres_native_connection(connection));
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_set_auth_data_hook. */
void cpkt_postgres_set_auth_data_hook(cpkt_postgres_auth_data_hook callback,
                                      void *context) {
  cpkt_postgres_hook_lock_acquire();
  cpkt_postgres_auth_data_callback = callback;
  cpkt_postgres_auth_data_context = context;
  cpkt_postgres_hook_lock_release();
  PQsetAuthDataHook(callback == NULL ? NULL
                                     : cpkt_postgres_native_auth_data_hook);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_get_auth_data_hook. */
cpkt_postgres_auth_data_hook
cpkt_postgres_get_auth_data_hook(void **context_out) {
  cpkt_postgres_auth_data_hook callback;
  PQauthDataHook_type native_callback;

  native_callback = PQgetAuthDataHook();
  cpkt_postgres_hook_lock_acquire();
  callback = native_callback == cpkt_postgres_native_auth_data_hook
                 ? cpkt_postgres_auth_data_callback
                 : NULL;
  if (context_out != NULL) {
    *context_out = callback == NULL ? NULL : cpkt_postgres_auth_data_context;
  }
  cpkt_postgres_hook_lock_release();
  return callback;
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_trace. */
void cpkt_postgres_trace(cpkt_postgres_connection *connection, FILE *stream) {
  PGconn *native_connection;
  cpkt_postgres_notice_binding *binding;

  native_connection = cpkt_postgres_native_connection(connection);
  cpkt_postgres_hook_lock_acquire();
  binding = cpkt_postgres_find_notice_binding(native_connection);
  if (binding != NULL) {
    binding->trace_callback = NULL;
    binding->trace_context = NULL;
  }
  cpkt_postgres_hook_lock_release();
  PQtrace(native_connection, stream);
}

/** Selects the formatted protocol trace callback destination. */
int cpkt_postgres_set_trace_sink(cpkt_postgres_connection *connection,
                                 cpkt_postgres_trace_sink callback,
                                 void *context) {
  PGconn *native_connection;
  cpkt_postgres_notice_binding *binding;

  if (connection == NULL) {
    return 0;
  }
  native_connection = cpkt_postgres_native_connection(connection);
  binding = NULL;
  if (callback != NULL) {
    binding = cpkt_postgres_ensure_notice_binding(native_connection);
    if (binding == NULL) {
      return 0;
    }
  } else {
    cpkt_postgres_hook_lock_acquire();
    binding = cpkt_postgres_find_notice_binding(native_connection);
    cpkt_postgres_hook_lock_release();
  }
  if (binding != NULL) {
    cpkt_postgres_hook_lock_acquire();
    binding->trace_callback = callback;
    binding->trace_context = context;
    cpkt_postgres_hook_lock_release();
  }
  PQsetTraceProcessor(native_connection,
                      callback != NULL ? cpkt_postgres_native_trace : NULL,
                      binding);
  return 1;
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_untrace. */
void cpkt_postgres_untrace(cpkt_postgres_connection *connection) {
  PGconn *native_connection;
  cpkt_postgres_notice_binding *binding;

  native_connection = cpkt_postgres_native_connection(connection);
  cpkt_postgres_hook_lock_acquire();
  binding = cpkt_postgres_find_notice_binding(native_connection);
  if (binding != NULL) {
    binding->trace_callback = NULL;
    binding->trace_context = NULL;
  }
  cpkt_postgres_hook_lock_release();
  PQuntrace(native_connection);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_set_trace_flags. */
void cpkt_postgres_set_trace_flags(cpkt_postgres_connection *connection,
                                   int flags) {
  PQsetTraceFlags(cpkt_postgres_native_connection(connection), flags);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_execute. */
cpkt_postgres_result *
cpkt_postgres_execute(cpkt_postgres_connection *connection, const char *query) {
  PGconn *native_connection;
  native_connection = cpkt_postgres_native_connection(connection);
  return cpkt_postgres_track_result(PQexec(native_connection, query),
                                    native_connection, NULL);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_execute_params. */
cpkt_postgres_result *cpkt_postgres_execute_params(
    cpkt_postgres_connection *connection, const char *command,
    int parameter_count, const cpkt_postgres_oid *parameter_types,
    const char *const *parameter_values, const int *parameter_lengths,
    const int *parameter_formats, int result_format) {
  Oid *native_types;
  PGresult *result;

  native_types = cpkt_postgres_native_oids(parameter_types, parameter_count);
  if (parameter_types != NULL && parameter_count > 0 && native_types == NULL) {
    return NULL;
  }
  result = PQexecParams(cpkt_postgres_native_connection(connection), command,
                        parameter_count, native_types, parameter_values,
                        parameter_lengths, parameter_formats, result_format);
  free(native_types);
  return cpkt_postgres_track_result(
      result, cpkt_postgres_native_connection(connection), NULL);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_prepare. */
cpkt_postgres_result *
cpkt_postgres_prepare(cpkt_postgres_connection *connection,
                      const char *statement_name, const char *query,
                      int parameter_count,
                      const cpkt_postgres_oid *parameter_types) {
  Oid *native_types;
  PGresult *result;

  native_types = cpkt_postgres_native_oids(parameter_types, parameter_count);
  if (parameter_types != NULL && parameter_count > 0 && native_types == NULL) {
    return NULL;
  }
  result = PQprepare(cpkt_postgres_native_connection(connection),
                     statement_name, query, parameter_count, native_types);
  free(native_types);
  return cpkt_postgres_track_result(
      result, cpkt_postgres_native_connection(connection), NULL);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_execute_prepared. */
cpkt_postgres_result *cpkt_postgres_execute_prepared(
    cpkt_postgres_connection *connection, const char *statement_name,
    int parameter_count, const char *const *parameter_values,
    const int *parameter_lengths, const int *parameter_formats,
    int result_format) {
  PGconn *native_connection;
  native_connection = cpkt_postgres_native_connection(connection);
  return cpkt_postgres_track_result(
      PQexecPrepared(native_connection, statement_name, parameter_count,
                     parameter_values, parameter_lengths, parameter_formats,
                     result_format),
      native_connection, NULL);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_send_query. */
int cpkt_postgres_send_query(cpkt_postgres_connection *connection,
                             const char *query) {
  return PQsendQuery(cpkt_postgres_native_connection(connection), query);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_send_query_params. */
int cpkt_postgres_send_query_params(cpkt_postgres_connection *connection,
                                    const char *command, int parameter_count,
                                    const cpkt_postgres_oid *parameter_types,
                                    const char *const *parameter_values,
                                    const int *parameter_lengths,
                                    const int *parameter_formats,
                                    int result_format) {
  Oid *native_types;
  int status;

  native_types = cpkt_postgres_native_oids(parameter_types, parameter_count);
  if (parameter_types != NULL && parameter_count > 0 && native_types == NULL) {
    return 0;
  }
  status =
      PQsendQueryParams(cpkt_postgres_native_connection(connection), command,
                        parameter_count, native_types, parameter_values,
                        parameter_lengths, parameter_formats, result_format);
  free(native_types);
  return status;
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_send_prepare. */
int cpkt_postgres_send_prepare(cpkt_postgres_connection *connection,
                               const char *statement_name, const char *query,
                               int parameter_count,
                               const cpkt_postgres_oid *parameter_types) {
  Oid *native_types;
  int status;

  native_types = cpkt_postgres_native_oids(parameter_types, parameter_count);
  if (parameter_types != NULL && parameter_count > 0 && native_types == NULL) {
    return 0;
  }
  status = PQsendPrepare(cpkt_postgres_native_connection(connection),
                         statement_name, query, parameter_count, native_types);
  free(native_types);
  return status;
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_send_query_prepared. */
int cpkt_postgres_send_query_prepared(cpkt_postgres_connection *connection,
                                      const char *statement_name,
                                      int parameter_count,
                                      const char *const *parameter_values,
                                      const int *parameter_lengths,
                                      const int *parameter_formats,
                                      int result_format) {
  return PQsendQueryPrepared(cpkt_postgres_native_connection(connection),
                             statement_name, parameter_count, parameter_values,
                             parameter_lengths, parameter_formats,
                             result_format);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_set_single_row_mode. */
int cpkt_postgres_set_single_row_mode(cpkt_postgres_connection *connection) {
  return PQsetSingleRowMode(cpkt_postgres_native_connection(connection));
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_set_chunked_rows_mode. */
int cpkt_postgres_set_chunked_rows_mode(cpkt_postgres_connection *connection,
                                        int chunk_size) {
  return PQsetChunkedRowsMode(cpkt_postgres_native_connection(connection),
                              chunk_size);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_get_result. */
cpkt_postgres_result *
cpkt_postgres_get_result(cpkt_postgres_connection *connection) {
  PGconn *native_connection;
  native_connection = cpkt_postgres_native_connection(connection);
  return cpkt_postgres_track_result(PQgetResult(native_connection),
                                    native_connection, NULL);
}
/** Checked asynchronous retrieval preserves the native one-result-at-a-time
 * mechanism while making facade failures distinct from drainage. */
cpkt_postgres_retrieval_status
cpkt_postgres_get_result_checked(cpkt_postgres_connection *connection,
                                 cpkt_postgres_result **result_out,
                                 cpkt_postgres_retrieval_error *error_out) {
  PGconn *native_connection;
  PGresult *native_result;
  if (result_out != NULL)
    *result_out = NULL;
  if (error_out != NULL)
    memset(error_out, 0, sizeof(*error_out));
  if (connection == NULL || result_out == NULL) {
    cpkt_postgres_record_retrieval_failure(
        error_out, CPKT_POSTGRES_RETRIEVAL_ERROR_ARGUMENT, NULL);
    return CPKT_POSTGRES_RETRIEVAL_FAILED;
  }
  native_connection = cpkt_postgres_native_connection(connection);
  native_result = PQgetResult(native_connection);
  if (native_result == NULL) {
    if (PQstatus(native_connection) == CONNECTION_BAD) {
      cpkt_postgres_record_retrieval_failure(
          error_out, CPKT_POSTGRES_RETRIEVAL_ERROR_CONNECTION, NULL);
      return CPKT_POSTGRES_RETRIEVAL_FAILED;
    }
    return CPKT_POSTGRES_RETRIEVAL_DRAINED;
  }
  *result_out = cpkt_postgres_track_result_checked(
      native_result, native_connection, NULL, error_out);
  return *result_out != NULL ? CPKT_POSTGRES_RETRIEVAL_RESULT
                             : CPKT_POSTGRES_RETRIEVAL_FAILED;
}
/** Receiver entry point for the checked asynchronous retrieval contract. */
cpkt_postgres_retrieval_status
cpkt_postgres_receive_checked(cpkt_postgres *receiver,
                              cpkt_postgres_result **result_out,
                              cpkt_postgres_retrieval_error *error_out) {
  return cpkt_postgres_get_result_checked(
      receiver == NULL ? NULL : receiver->connection, result_out, error_out);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_is_busy. */
int cpkt_postgres_is_busy(cpkt_postgres_connection *connection) {
  return PQisBusy(cpkt_postgres_native_connection(connection));
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_consume_input. */
int cpkt_postgres_consume_input(cpkt_postgres_connection *connection) {
  return PQconsumeInput(cpkt_postgres_native_connection(connection));
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_enter_pipeline_mode. */
int cpkt_postgres_enter_pipeline_mode(cpkt_postgres_connection *connection) {
  return PQenterPipelineMode(cpkt_postgres_native_connection(connection));
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_exit_pipeline_mode. */
int cpkt_postgres_exit_pipeline_mode(cpkt_postgres_connection *connection) {
  return PQexitPipelineMode(cpkt_postgres_native_connection(connection));
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_pipeline_sync. */
int cpkt_postgres_pipeline_sync(cpkt_postgres_connection *connection) {
  return PQpipelineSync(cpkt_postgres_native_connection(connection));
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_send_flush_request. */
int cpkt_postgres_send_flush_request(cpkt_postgres_connection *connection) {
  return PQsendFlushRequest(cpkt_postgres_native_connection(connection));
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_send_pipeline_sync. */
int cpkt_postgres_send_pipeline_sync(cpkt_postgres_connection *connection) {
  return PQsendPipelineSync(cpkt_postgres_native_connection(connection));
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_put_copy_data. */
int cpkt_postgres_put_copy_data(cpkt_postgres_connection *connection,
                                const char *buffer, int byte_count) {
  return PQputCopyData(cpkt_postgres_native_connection(connection), buffer,
                       byte_count);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_put_copy_end. */
int cpkt_postgres_put_copy_end(cpkt_postgres_connection *connection,
                               const char *error_message) {
  return PQputCopyEnd(cpkt_postgres_native_connection(connection),
                      error_message);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_get_copy_data. */
int cpkt_postgres_get_copy_data(cpkt_postgres_connection *connection,
                                char **buffer_out, int asynchronous) {
  return PQgetCopyData(cpkt_postgres_native_connection(connection), buffer_out,
                       asynchronous);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_getline. */
int cpkt_postgres_getline(cpkt_postgres_connection *connection, char *buffer,
                          int length) {
  return PQgetline(cpkt_postgres_native_connection(connection), buffer, length);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_putline. */
int cpkt_postgres_putline(cpkt_postgres_connection *connection,
                          const char *line) {
  return PQputline(cpkt_postgres_native_connection(connection), line);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_getline_async. */
int cpkt_postgres_getline_async(cpkt_postgres_connection *connection,
                                char *buffer, int buffer_size) {
  return PQgetlineAsync(cpkt_postgres_native_connection(connection), buffer,
                        buffer_size);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_put_bytes. */
int cpkt_postgres_put_bytes(cpkt_postgres_connection *connection,
                            const char *buffer, int byte_count) {
  return PQputnbytes(cpkt_postgres_native_connection(connection), buffer,
                     byte_count);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_end_copy. */
int cpkt_postgres_end_copy(cpkt_postgres_connection *connection) {
  return PQendcopy(cpkt_postgres_native_connection(connection));
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_set_nonblocking. */
int cpkt_postgres_set_nonblocking(cpkt_postgres_connection *connection,
                                  int enabled) {
  return PQsetnonblocking(cpkt_postgres_native_connection(connection), enabled);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_is_nonblocking. */
int cpkt_postgres_is_nonblocking(const cpkt_postgres_connection *connection) {
  return PQisnonblocking(cpkt_postgres_native_connection_const(connection));
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_is_threadsafe. */
int cpkt_postgres_is_threadsafe(void) { return PQisthreadsafe(); }
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_ping. */
cpkt_postgres_ping_status cpkt_postgres_ping(const char *connection_info) {
  return (cpkt_postgres_ping_status)PQping(connection_info);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_ping_params. */
cpkt_postgres_ping_status cpkt_postgres_ping_params(const char *const *keywords,
                                                    const char *const *values,
                                                    int expand_database_name) {
  return (cpkt_postgres_ping_status)PQpingParams(keywords, values,
                                                 expand_database_name);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_flush. */
int cpkt_postgres_flush(cpkt_postgres_connection *connection) {
  return PQflush(cpkt_postgres_native_connection(connection));
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_fastpath. */
cpkt_postgres_result *cpkt_postgres_fastpath(
    cpkt_postgres_connection *connection, int function_id, int *result_buffer,
    int *result_length_out, int result_is_integer,
    const cpkt_postgres_fastpath_argument *arguments, int argument_count) {
  PQArgBlock *native_arguments;
  int index;
  PGresult *result;

  native_arguments = NULL;
  if (arguments != NULL && argument_count > 0) {
    native_arguments = (PQArgBlock *)malloc((size_t)argument_count *
                                            sizeof(*native_arguments));
    if (native_arguments == NULL) {
      return NULL;
    }
    for (index = 0; index < argument_count; ++index) {
      native_arguments[index].len = arguments[index].length;
      native_arguments[index].isint = arguments[index].is_integer;
      if (arguments[index].is_integer) {
        native_arguments[index].u.integer = arguments[index].integer_value;
      } else {
        native_arguments[index].u.ptr = arguments[index].integer_pointer;
      }
    }
  }
  result = PQfn(cpkt_postgres_native_connection(connection), function_id,
                result_buffer, result_length_out, result_is_integer,
                native_arguments, argument_count);
  free(native_arguments);
  return cpkt_postgres_track_result(
      result, cpkt_postgres_native_connection(connection), NULL);
}

#define CPKT_POSTGRES_RESULT_INT_WRAPPER(name, native_name)                    \
  int name(const cpkt_postgres_result *result) {                               \
    return native_name(cpkt_postgres_native_result_const(result));             \
  }
#define CPKT_POSTGRES_RESULT_TEXT_WRAPPER(name, native_name)                   \
  char *name(const cpkt_postgres_result *result) {                             \
    return native_name(cpkt_postgres_native_result_const(result));             \
  }

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_result_status_get. */
cpkt_postgres_result_status
cpkt_postgres_result_status_get(const cpkt_postgres_result *result) {
  return (cpkt_postgres_result_status)PQresultStatus(
      cpkt_postgres_native_result_const(result));
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_result_status_text. */
char *cpkt_postgres_result_status_text(cpkt_postgres_result_status status) {
  return PQresStatus((ExecStatusType)status);
}
CPKT_POSTGRES_RESULT_TEXT_WRAPPER(cpkt_postgres_result_error_message,
                                  PQresultErrorMessage)
CPKT_POSTGRES_RESULT_INT_WRAPPER(cpkt_postgres_result_row_count, PQntuples)
CPKT_POSTGRES_RESULT_INT_WRAPPER(cpkt_postgres_result_field_count, PQnfields)
CPKT_POSTGRES_RESULT_INT_WRAPPER(cpkt_postgres_result_is_binary, PQbinaryTuples)

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_result_verbose_error_message. */
char *cpkt_postgres_result_verbose_error_message(
    const cpkt_postgres_result *result, cpkt_postgres_verbosity verbosity,
    cpkt_postgres_context_visibility visibility) {
  return PQresultVerboseErrorMessage(cpkt_postgres_native_result_const(result),
                                     (PGVerbosity)verbosity,
                                     (PGContextVisibility)visibility);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_result_error_field. */
char *cpkt_postgres_result_error_field(const cpkt_postgres_result *result,
                                       int field_code) {
  return PQresultErrorField(cpkt_postgres_native_result_const(result),
                            field_code);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_result_field_name. */
char *cpkt_postgres_result_field_name(const cpkt_postgres_result *result,
                                      int field_index) {
  return PQfname(cpkt_postgres_native_result_const(result), field_index);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_result_field_number. */
int cpkt_postgres_result_field_number(const cpkt_postgres_result *result,
                                      const char *field_name) {
  return PQfnumber(cpkt_postgres_native_result_const(result), field_name);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_result_field_table_oid. */
cpkt_postgres_oid
cpkt_postgres_result_field_table_oid(const cpkt_postgres_result *result,
                                     int field_index) {
  return (cpkt_postgres_oid)PQftable(cpkt_postgres_native_result_const(result),
                                     field_index);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_result_field_table_column. */
int cpkt_postgres_result_field_table_column(const cpkt_postgres_result *result,
                                            int field_index) {
  return PQftablecol(cpkt_postgres_native_result_const(result), field_index);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_result_field_format. */
int cpkt_postgres_result_field_format(const cpkt_postgres_result *result,
                                      int field_index) {
  return PQfformat(cpkt_postgres_native_result_const(result), field_index);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_result_field_type. */
cpkt_postgres_oid
cpkt_postgres_result_field_type(const cpkt_postgres_result *result,
                                int field_index) {
  return (cpkt_postgres_oid)PQftype(cpkt_postgres_native_result_const(result),
                                    field_index);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_result_field_size. */
int cpkt_postgres_result_field_size(const cpkt_postgres_result *result,
                                    int field_index) {
  return PQfsize(cpkt_postgres_native_result_const(result), field_index);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_result_field_modifier. */
int cpkt_postgres_result_field_modifier(const cpkt_postgres_result *result,
                                        int field_index) {
  return PQfmod(cpkt_postgres_native_result_const(result), field_index);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_result_command_status. */
char *cpkt_postgres_result_command_status(cpkt_postgres_result *result) {
  return PQcmdStatus(cpkt_postgres_native_result(result));
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_result_oid_status. */
char *cpkt_postgres_result_oid_status(const cpkt_postgres_result *result) {
  return PQoidStatus(cpkt_postgres_native_result_const(result));
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_result_oid_value. */
cpkt_postgres_oid
cpkt_postgres_result_oid_value(const cpkt_postgres_result *result) {
  return (cpkt_postgres_oid)PQoidValue(
      cpkt_postgres_native_result_const(result));
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_result_command_tuples. */
char *cpkt_postgres_result_command_tuples(cpkt_postgres_result *result) {
  return PQcmdTuples(cpkt_postgres_native_result(result));
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_result_value. */
char *cpkt_postgres_result_value(const cpkt_postgres_result *result,
                                 int row_index, int field_index) {
  return PQgetvalue(cpkt_postgres_native_result_const(result), row_index,
                    field_index);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_result_value_length. */
int cpkt_postgres_result_value_length(const cpkt_postgres_result *result,
                                      int row_index, int field_index) {
  return PQgetlength(cpkt_postgres_native_result_const(result), row_index,
                     field_index);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_result_value_is_null. */
int cpkt_postgres_result_value_is_null(const cpkt_postgres_result *result,
                                       int row_index, int field_index) {
  return PQgetisnull(cpkt_postgres_native_result_const(result), row_index,
                     field_index);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_result_parameter_count. */
int cpkt_postgres_result_parameter_count(const cpkt_postgres_result *result) {
  return PQnparams(cpkt_postgres_native_result_const(result));
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_result_parameter_type. */
cpkt_postgres_oid
cpkt_postgres_result_parameter_type(const cpkt_postgres_result *result,
                                    int parameter_index) {
  return (cpkt_postgres_oid)PQparamtype(
      cpkt_postgres_native_result_const(result), parameter_index);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_describe_prepared. */
cpkt_postgres_result *
cpkt_postgres_describe_prepared(cpkt_postgres_connection *connection,
                                const char *statement_name) {
  PGconn *native_connection;
  native_connection = cpkt_postgres_native_connection(connection);
  return cpkt_postgres_track_result(
      PQdescribePrepared(native_connection, statement_name), native_connection,
      NULL);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_describe_portal. */
cpkt_postgres_result *
cpkt_postgres_describe_portal(cpkt_postgres_connection *connection,
                              const char *portal_name) {
  PGconn *native_connection;
  native_connection = cpkt_postgres_native_connection(connection);
  return cpkt_postgres_track_result(
      PQdescribePortal(native_connection, portal_name), native_connection,
      NULL);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_send_describe_prepared. */
int cpkt_postgres_send_describe_prepared(cpkt_postgres_connection *connection,
                                         const char *statement_name) {
  return PQsendDescribePrepared(cpkt_postgres_native_connection(connection),
                                statement_name);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_send_describe_portal. */
int cpkt_postgres_send_describe_portal(cpkt_postgres_connection *connection,
                                       const char *portal_name) {
  return PQsendDescribePortal(cpkt_postgres_native_connection(connection),
                              portal_name);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_close_prepared. */
cpkt_postgres_result *
cpkt_postgres_close_prepared(cpkt_postgres_connection *connection,
                             const char *statement_name) {
  PGconn *native_connection;
  native_connection = cpkt_postgres_native_connection(connection);
  return cpkt_postgres_track_result(
      PQclosePrepared(native_connection, statement_name), native_connection,
      NULL);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_close_portal. */
cpkt_postgres_result *
cpkt_postgres_close_portal(cpkt_postgres_connection *connection,
                           const char *portal_name) {
  PGconn *native_connection;
  native_connection = cpkt_postgres_native_connection(connection);
  return cpkt_postgres_track_result(
      PQclosePortal(native_connection, portal_name), native_connection, NULL);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_send_close_prepared. */
int cpkt_postgres_send_close_prepared(cpkt_postgres_connection *connection,
                                      const char *statement_name) {
  return PQsendClosePrepared(cpkt_postgres_native_connection(connection),
                             statement_name);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_send_close_portal. */
int cpkt_postgres_send_close_portal(cpkt_postgres_connection *connection,
                                    const char *portal_name) {
  return PQsendClosePortal(cpkt_postgres_native_connection(connection),
                           portal_name);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_result_free. */
void cpkt_postgres_result_free(cpkt_postgres_result *result) {
  PGresult *native_result;
  cpkt_postgres_result_binding **slot;
  cpkt_postgres_result_binding *entry;
  cpkt_postgres_pending_clear pending;

  native_result = cpkt_postgres_native_result(result);
  cpkt_postgres_hook_lock_acquire();
  slot = &cpkt_postgres_result_bindings;
  while (*slot != NULL && (*slot)->result != native_result) {
    slot = &(*slot)->next;
  }
  entry = *slot;
  if (entry != NULL)
    *slot = entry->next;
  cpkt_postgres_pending_clear_begin_locked(
      native_result, entry == NULL ? NULL : entry->owner, &pending);
  cpkt_postgres_hook_lock_release();
  /* Detach before PQclear can release this address for another result.
   * Keep the owner's reference until native cleanup is finished. Native
   * DESTROY releases initialized event state; uninitialized state has no
   * callback and must be released before the address becomes reusable. */
  cpkt_postgres_event_release_uninitialized(native_result);
  PQclear(native_result);
  cpkt_postgres_pending_clear_end(&pending);
  free(entry);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_text_free. */
void cpkt_postgres_text_free(char *memory) { PQfreemem(memory); }
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_bytea_free. */
void cpkt_postgres_bytea_free(unsigned char *memory) { PQfreemem(memory); }

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_escape_string_connection. */
size_t
cpkt_postgres_escape_string_connection(cpkt_postgres_connection *connection,
                                       char *destination, const char *source,
                                       size_t source_length, int *error_out) {
  return PQescapeStringConn(cpkt_postgres_native_connection(connection),
                            destination, source, source_length, error_out);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_escape_literal. */
char *cpkt_postgres_escape_literal(cpkt_postgres_connection *connection,
                                   const char *source, size_t source_length) {
  return PQescapeLiteral(cpkt_postgres_native_connection(connection), source,
                         source_length);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_escape_identifier. */
char *cpkt_postgres_escape_identifier(cpkt_postgres_connection *connection,
                                      const char *source,
                                      size_t source_length) {
  return PQescapeIdentifier(cpkt_postgres_native_connection(connection), source,
                            source_length);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_escape_bytea_connection. */
unsigned char *cpkt_postgres_escape_bytea_connection(
    cpkt_postgres_connection *connection, const unsigned char *source,
    size_t source_length, size_t *destination_length_out) {
  return PQescapeByteaConn(cpkt_postgres_native_connection(connection), source,
                           source_length, destination_length_out);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_unescape_bytea. */
unsigned char *cpkt_postgres_unescape_bytea(const unsigned char *source,
                                            size_t *destination_length_out) {
  return PQunescapeBytea(source, destination_length_out);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_escape_string. */
size_t cpkt_postgres_escape_string(char *destination, const char *source,
                                   size_t source_length) {
  return PQescapeString(destination, source, source_length);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_escape_bytea. */
unsigned char *cpkt_postgres_escape_bytea(const unsigned char *source,
                                          size_t source_length,
                                          size_t *destination_length_out) {
  return PQescapeBytea(source, source_length, destination_length_out);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_notification_next. */
cpkt_postgres_notification *
cpkt_postgres_notification_next(cpkt_postgres_connection *connection) {
  PGnotify *native_notification;
  cpkt_postgres_notification *notification;

  native_notification = PQnotifies(cpkt_postgres_native_connection(connection));
  if (native_notification == NULL) {
    return NULL;
  }
  notification = (cpkt_postgres_notification *)calloc(1, sizeof(*notification));
  if (notification != NULL) {
    notification->channel =
        cpkt_postgres_copy_text(native_notification->relname);
    notification->backend_pid = native_notification->be_pid;
    notification->payload = cpkt_postgres_copy_text(native_notification->extra);
    if ((native_notification->relname != NULL &&
         notification->channel == NULL) ||
        (native_notification->extra != NULL && notification->payload == NULL)) {
      free(notification->channel);
      free(notification->payload);
      free(notification);
      notification = NULL;
    }
  }
  PQfreemem(native_notification);
  return notification;
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_notification_free. */
void cpkt_postgres_notification_free(cpkt_postgres_notification *notification) {
  if (notification == NULL) {
    return;
  }
  free(notification->channel);
  free(notification->payload);
  free(notification);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_result_new_empty. */
cpkt_postgres_result *
cpkt_postgres_result_new_empty(cpkt_postgres_connection *connection,
                               cpkt_postgres_result_status status) {
  PGconn *native_connection;
  native_connection = cpkt_postgres_native_connection(connection);
  return cpkt_postgres_track_result(
      PQmakeEmptyPGresult(native_connection, (ExecStatusType)status),
      native_connection, NULL);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_result_copy. */
cpkt_postgres_result *
cpkt_postgres_result_copy(const cpkt_postgres_result *source, int flags) {
  const PGresult *native_source;
  cpkt_postgres_result *result;
  native_source = cpkt_postgres_native_result_const(source);
  result = cpkt_postgres_track_result(PQcopyResult(native_source, flags), NULL,
                                      native_source);
  if (result != NULL && !cpkt_postgres_event_prepare_copy(
                            native_source, (PGresult *)result, flags)) {
    cpkt_postgres_result_free(result);
    return NULL;
  }
  return result;
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_result_set_attributes. */
int cpkt_postgres_result_set_attributes(
    cpkt_postgres_result *result, int attribute_count,
    cpkt_postgres_result_attribute *attributes) {
  PGresAttDesc *native_attributes;
  int index;
  int status;

  if (attribute_count < 0 || (attribute_count > 0 && attributes == NULL)) {
    return 0;
  }
  native_attributes = NULL;
  if (attribute_count > 0) {
    native_attributes = (PGresAttDesc *)calloc((size_t)attribute_count,
                                               sizeof(*native_attributes));
    if (native_attributes == NULL) {
      return 0;
    }
    for (index = 0; index < attribute_count; ++index) {
      if (!cpkt_postgres_native_oid(attributes[index].table_oid,
                                    &native_attributes[index].tableid) ||
          !cpkt_postgres_native_oid(attributes[index].type_oid,
                                    &native_attributes[index].typid)) {
        free(native_attributes);
        return 0;
      }
      native_attributes[index].name = attributes[index].name;
      native_attributes[index].columnid = attributes[index].column_id;
      native_attributes[index].format = attributes[index].format;
      native_attributes[index].typlen = attributes[index].type_size;
      native_attributes[index].atttypmod = attributes[index].type_modifier;
    }
  }
  status = PQsetResultAttrs(cpkt_postgres_native_result(result),
                            attribute_count, native_attributes);
  free(native_attributes);
  return status;
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_result_allocate. */
unsigned char *cpkt_postgres_result_allocate(cpkt_postgres_result *result,
                                             size_t byte_count) {
  return (unsigned char *)PQresultAlloc(cpkt_postgres_native_result(result),
                                        byte_count);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_result_memory_size. */
size_t cpkt_postgres_result_memory_size(const cpkt_postgres_result *result) {
  return PQresultMemorySize(cpkt_postgres_native_result_const(result));
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_result_set_value. */
int cpkt_postgres_result_set_value(cpkt_postgres_result *result, int row_index,
                                   int field_index, char *value,
                                   int value_length) {
  return PQsetvalue(cpkt_postgres_native_result(result), row_index, field_index,
                    value, value_length);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_print. */
void cpkt_postgres_print(FILE *stream, const cpkt_postgres_result *result,
                         const cpkt_postgres_print_options *options) {
  PQprintOpt native_options;

  if (options == NULL) {
    PQprint(stream, cpkt_postgres_native_result_const(result), NULL);
    return;
  }
  native_options.header = options->header;
  native_options.align = options->align;
  native_options.standard = options->standard;
  native_options.html3 = options->html;
  native_options.expanded = options->expanded;
  native_options.pager = options->pager;
  native_options.fieldSep = options->field_separator;
  native_options.tableOpt = options->table_options;
  native_options.caption = options->caption;
  native_options.fieldName = options->field_names;
  PQprint(stream, cpkt_postgres_native_result_const(result), &native_options);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_display_tuples. */
void cpkt_postgres_display_tuples(const cpkt_postgres_result *result,
                                  FILE *stream, int fill_align,
                                  const char *field_separator, int print_header,
                                  int quiet) {
  PQdisplayTuples(cpkt_postgres_native_result_const(result), stream, fill_align,
                  field_separator, print_header, quiet);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_print_tuples. */
void cpkt_postgres_print_tuples(const cpkt_postgres_result *result,
                                FILE *stream, int print_attribute_names,
                                int terse_output, int column_width) {
  PQprintTuples(cpkt_postgres_native_result_const(result), stream,
                print_attribute_names, terse_output, column_width);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_large_object_open. */
int cpkt_postgres_large_object_open(cpkt_postgres_connection *connection,
                                    cpkt_postgres_oid object_oid, int mode) {
  Oid native_oid;
  if (!cpkt_postgres_native_oid(object_oid, &native_oid))
    return -1;
  return lo_open(cpkt_postgres_native_connection(connection), native_oid, mode);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_large_object_close. */
int cpkt_postgres_large_object_close(cpkt_postgres_connection *connection,
                                     int descriptor) {
  return lo_close(cpkt_postgres_native_connection(connection), descriptor);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_large_object_read. */
int cpkt_postgres_large_object_read(cpkt_postgres_connection *connection,
                                    int descriptor, char *buffer,
                                    size_t byte_count) {
  return lo_read(cpkt_postgres_native_connection(connection), descriptor,
                 buffer, byte_count);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_large_object_write. */
int cpkt_postgres_large_object_write(cpkt_postgres_connection *connection,
                                     int descriptor, const char *buffer,
                                     size_t byte_count) {
  return lo_write(cpkt_postgres_native_connection(connection), descriptor,
                  buffer, byte_count);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_large_object_seek. */
int cpkt_postgres_large_object_seek(cpkt_postgres_connection *connection,
                                    int descriptor, int offset, int whence) {
  return lo_lseek(cpkt_postgres_native_connection(connection), descriptor,
                  offset, whence);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_large_object_seek64. */
cpkt_postgres_i64
cpkt_postgres_large_object_seek64(cpkt_postgres_connection *connection,
                                  int descriptor, cpkt_postgres_i64 offset,
                                  int whence) {
  return cpkt_postgres_public_i64(
      lo_lseek64(cpkt_postgres_native_connection(connection), descriptor,
                 cpkt_postgres_native_i64(offset), whence));
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_large_object_create_legacy. */
cpkt_postgres_oid
cpkt_postgres_large_object_create_legacy(cpkt_postgres_connection *connection,
                                         int mode) {
  return (cpkt_postgres_oid)lo_creat(
      cpkt_postgres_native_connection(connection), mode);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_large_object_create. */
cpkt_postgres_oid
cpkt_postgres_large_object_create(cpkt_postgres_connection *connection,
                                  cpkt_postgres_oid object_oid) {
  Oid native_oid;
  if (!cpkt_postgres_native_oid(object_oid, &native_oid))
    return 0;
  return (cpkt_postgres_oid)lo_create(
      cpkt_postgres_native_connection(connection), native_oid);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_large_object_tell. */
int cpkt_postgres_large_object_tell(cpkt_postgres_connection *connection,
                                    int descriptor) {
  return lo_tell(cpkt_postgres_native_connection(connection), descriptor);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_large_object_tell64. */
cpkt_postgres_i64
cpkt_postgres_large_object_tell64(cpkt_postgres_connection *connection,
                                  int descriptor) {
  return cpkt_postgres_public_i64(
      lo_tell64(cpkt_postgres_native_connection(connection), descriptor));
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_large_object_truncate. */
int cpkt_postgres_large_object_truncate(cpkt_postgres_connection *connection,
                                        int descriptor, size_t byte_count) {
  return lo_truncate(cpkt_postgres_native_connection(connection), descriptor,
                     byte_count);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_large_object_truncate64. */
int cpkt_postgres_large_object_truncate64(cpkt_postgres_connection *connection,
                                          int descriptor,
                                          cpkt_postgres_i64 byte_count) {
  return lo_truncate64(cpkt_postgres_native_connection(connection), descriptor,
                       cpkt_postgres_native_i64(byte_count));
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_large_object_unlink. */
int cpkt_postgres_large_object_unlink(cpkt_postgres_connection *connection,
                                      cpkt_postgres_oid object_oid) {
  Oid native_oid;
  if (!cpkt_postgres_native_oid(object_oid, &native_oid))
    return -1;
  return lo_unlink(cpkt_postgres_native_connection(connection), native_oid);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_large_object_import. */
cpkt_postgres_oid
cpkt_postgres_large_object_import(cpkt_postgres_connection *connection,
                                  const char *file_name) {
  return (cpkt_postgres_oid)lo_import(
      cpkt_postgres_native_connection(connection), file_name);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_large_object_import_with_oid. */
cpkt_postgres_oid
cpkt_postgres_large_object_import_with_oid(cpkt_postgres_connection *connection,
                                           const char *file_name,
                                           cpkt_postgres_oid object_oid) {
  Oid native_oid;
  if (!cpkt_postgres_native_oid(object_oid, &native_oid))
    return 0;
  return (cpkt_postgres_oid)lo_import_with_oid(
      cpkt_postgres_native_connection(connection), file_name, native_oid);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_large_object_export. */
int cpkt_postgres_large_object_export(cpkt_postgres_connection *connection,
                                      cpkt_postgres_oid object_oid,
                                      const char *file_name) {
  Oid native_oid;
  if (!cpkt_postgres_native_oid(object_oid, &native_oid))
    return -1;
  return lo_export(cpkt_postgres_native_connection(connection), native_oid,
                   file_name);
}

/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_library_version. */
int cpkt_postgres_library_version(void) { return PQlibVersion(); }
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_socket_poll. */
int cpkt_postgres_socket_poll(int socket_descriptor, int wait_for_read,
                              int wait_for_write, cpkt_postgres_i64 end_time) {
  return PQsocketPoll(socket_descriptor, wait_for_read, wait_for_write,
                      cpkt_postgres_native_i64(end_time));
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_current_time_microseconds. */
cpkt_postgres_i64 cpkt_postgres_current_time_microseconds(void) {
  return cpkt_postgres_public_i64(PQgetCurrentTimeUSec());
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_multibyte_length. */
int cpkt_postgres_multibyte_length(const char *text, int encoding) {
  return PQmblen(text, encoding);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_multibyte_length_bounded. */
int cpkt_postgres_multibyte_length_bounded(const char *text, int encoding) {
  return PQmblenBounded(text, encoding);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_multibyte_display_length. */
int cpkt_postgres_multibyte_display_length(const char *text, int encoding) {
  return PQdsplen(text, encoding);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_environment_encoding. */
int cpkt_postgres_environment_encoding(void) { return PQenv2encoding(); }
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_encrypt_password. */
char *cpkt_postgres_encrypt_password(const char *password, const char *user) {
  return PQencryptPassword(password, user);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_encrypt_password_connection. */
char *cpkt_postgres_encrypt_password_connection(
    cpkt_postgres_connection *connection, const char *password,
    const char *user, const char *algorithm) {
  return PQencryptPasswordConn(cpkt_postgres_native_connection(connection),
                               password, user, algorithm);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_change_password. */
cpkt_postgres_result *
cpkt_postgres_change_password(cpkt_postgres_connection *connection,
                              const char *user, const char *password) {
  PGconn *native_connection;
  native_connection = cpkt_postgres_native_connection(connection);
  return cpkt_postgres_track_result(
      PQchangePassword(native_connection, user, password), native_connection,
      NULL);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_encoding_from_name. */
int cpkt_postgres_encoding_from_name(const char *name) {
  return pg_char_to_encoding(name);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_encoding_name. */
const char *cpkt_postgres_encoding_name(int encoding) {
  return pg_encoding_to_char(encoding);
}
/** Implements the documented public C89 PostgreSQL facade operation
 * cpkt_postgres_server_encoding_is_valid. */
int cpkt_postgres_server_encoding_is_valid(int encoding) {
  return pg_valid_server_encoding_id(encoding);
}
