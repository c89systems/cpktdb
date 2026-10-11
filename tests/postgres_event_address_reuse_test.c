#include <cpkt/postgres.h>
#include <libpq-fe.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct reuse_case {
  cpkt_postgres_connection *connection;
  cpkt_postgres_event *event;
  cpkt_postgres_result *source;
  cpkt_postgres_result *old;
  cpkt_postgres_result *replacement;
  PGresult *old_native;
  int mode;
  int phase;
  int errors;
  int old_destroy;
  int replacement_destroy;
  int source_destroy;
  int connection_destroy;
  int replacement_create;
  int native_copy;
  int reused_allocations;
} reuse_case;

static reuse_case active;
static int old_marker;
static int replacement_marker;
static int source_marker;
static void *held_result_block;
static void *result_to_hold;
static int reuse_pending;
static int clear_armed;
static int fail_after_make;
static int fail_next_malloc;

static void on_notice(void *context, cpkt_postgres_connection *connection,
                      const cpkt_postgres_result *result) {
  (void)context;
  (void)connection;
  (void)result;
}

void *__real_malloc(size_t size);
void __real_free(void *memory);
void __real_PQclear(PGresult *result);
PGresult *__real_PQmakeEmptyPGresult(PGconn *connection, ExecStatusType status);

void *__wrap_malloc(size_t size) {
  void *memory;
  if (fail_next_malloc) {
    fail_next_malloc = 0;
    return NULL;
  }
  if (reuse_pending && held_result_block != NULL) {
    memory = held_result_block;
    held_result_block = NULL;
    reuse_pending = 0;
    ++active.reused_allocations;
    return memory;
  }
  return __real_malloc(size);
}

void __wrap_free(void *memory) {
  if (result_to_hold != NULL && memory == result_to_hold) {
    held_result_block = memory;
    result_to_hold = NULL;
    return;
  }
  __real_free(memory);
}

PGresult *__wrap_PQmakeEmptyPGresult(PGconn *connection,
                                     ExecStatusType status) {
  PGresult *result;
  result = __real_PQmakeEmptyPGresult(connection, status);
  if (fail_after_make && result != NULL) {
    fail_after_make = 0;
    active.old_native = result;
    result_to_hold = result;
    fail_next_malloc = 1;
  }
  return result;
}

void __wrap_PQclear(PGresult *result) {
  int create_status;
  __real_PQclear(result);
  if (!clear_armed || result != active.old_native)
    return;
  clear_armed = 0;
  if (held_result_block != result)
    ++active.errors;
  active.phase = 1;
  reuse_pending = 1;
  active.replacement = cpkt_postgres_result_new_empty(
      active.connection, CPKT_POSTGRES_RESULT_TUPLES_OK);
  if (active.replacement == NULL ||
      (void *)active.replacement != (void *)result)
    ++active.errors;
  if (active.replacement == NULL)
    return;
  create_status = cpkt_postgres_event_fire_result_create(active.connection,
                                                         active.replacement);
  if (!create_status ||
      !cpkt_postgres_event_set_result_data(active.replacement, active.event,
                                           &replacement_marker) ||
      cpkt_postgres_event_result_data(active.replacement, active.event) !=
          &replacement_marker)
    ++active.errors;
}

static int on_event(cpkt_postgres_event_id id,
                    const cpkt_postgres_event_info *info, void *context) {
  reuse_case *state;
  state = (reuse_case *)context;
  if (info == NULL || info->event == NULL) {
    ++state->errors;
    return 0;
  }
  if (id == CPKT_POSTGRES_EVENT_REGISTER) {
    state->event = info->event;
    if (info->connection != state->connection)
      ++state->errors;
    return 1;
  }
  if (info->event != state->event)
    ++state->errors;
  if (id == CPKT_POSTGRES_EVENT_RESULT_CREATE) {
    if (state->phase == -1) {
      if (!cpkt_postgres_event_set_result_data(info->result, info->event,
                                               &source_marker))
        ++state->errors;
    } else if (state->phase == 0) {
      if (!cpkt_postgres_event_set_result_data(info->result, info->event,
                                               &old_marker))
        ++state->errors;
      if (state->mode == 4)
        return 0;
    } else if (state->phase == 1) {
      ++state->replacement_create;
      if (!cpkt_postgres_event_set_result_data(info->result, info->event,
                                               &replacement_marker))
        ++state->errors;
    } else {
      ++state->errors;
    }
  } else if (id == CPKT_POSTGRES_EVENT_RESULT_COPY) {
    ++state->native_copy;
    if (state->phase != 0 || info->source != state->source ||
        cpkt_postgres_event_result_data(info->source, info->event) !=
            &source_marker ||
        !cpkt_postgres_event_set_result_data(info->destination, info->event,
                                             &old_marker))
      ++state->errors;
    if (state->mode == 3)
      return 0;
  } else if (id == CPKT_POSTGRES_EVENT_RESULT_DESTROY) {
    if (state->phase == 0) {
      ++state->old_destroy;
      if (cpkt_postgres_event_result_data(info->result, info->event) !=
          &old_marker)
        ++state->errors;
    } else if (state->phase == 2) {
      ++state->replacement_destroy;
      if (cpkt_postgres_event_result_data(info->result, info->event) !=
          &replacement_marker)
        ++state->errors;
    } else if (state->phase == 3) {
      ++state->source_destroy;
      if (cpkt_postgres_event_result_data(info->result, info->event) !=
          &source_marker)
        ++state->errors;
    } else {
      ++state->errors;
    }
  } else if (id == CPKT_POSTGRES_EVENT_CONNECTION_DESTROY) {
    ++state->connection_destroy;
  }
  return 1;
}

/* Modes: initialized, uninitialized, copied, declined copy, declined create,
 * uninitialized copy, and failed facade tracking after native allocation. */
static int run_case(int mode) {
  int expected_old_destroy;
  int expected_source_destroy;
  int expected_copy;
  int status;
  memset(&active, 0, sizeof(active));
  active.mode = mode;
  held_result_block = NULL;
  result_to_hold = NULL;
  reuse_pending = 0;
  clear_armed = 0;
  fail_after_make = 0;
  fail_next_malloc = 0;
  active.connection = cpkt_postgres_connect(
      "host=/tmp/cpkt-postgres-reuse-no-socket connect_timeout=1");
  if (active.connection == NULL)
    return 1;
  if (!cpkt_postgres_set_notice_receiver(active.connection, on_notice, &active,
                                         NULL, NULL))
    return 90;
  active.event = cpkt_postgres_event_register(active.connection, on_event,
                                              "address-reuse", &active);
  if (active.event == NULL)
    return 2;
  if (mode == 2 || mode == 3 || mode == 5) {
    active.phase = -1;
    active.source = cpkt_postgres_result_new_empty(
        active.connection, CPKT_POSTGRES_RESULT_TUPLES_OK);
    if (active.source == NULL)
      return 3;
    if (mode != 5 && !cpkt_postgres_event_fire_result_create(active.connection,
                                                             active.source))
      return 4;
    active.phase = 0;
    active.old = cpkt_postgres_result_copy(active.source,
                                           CPKT_POSTGRES_COPY_RESULT_EVENTS);
  } else if (mode != 6) {
    active.phase = 0;
    active.old = cpkt_postgres_result_new_empty(active.connection,
                                                CPKT_POSTGRES_RESULT_TUPLES_OK);
    if (active.old != NULL && mode != 1) {
      status =
          cpkt_postgres_event_fire_result_create(active.connection, active.old);
      if ((mode == 4 && status != 0) || (mode != 4 && status == 0))
        ++active.errors;
    }
  }
  if (mode != 6) {
    if (active.old == NULL)
      return 5;
    if ((mode == 1 || mode == 5) && !cpkt_postgres_event_set_result_data(
                                        active.old, active.event, &old_marker))
      ++active.errors;
    if (cpkt_postgres_event_result_data(active.old, active.event) !=
        &old_marker)
      ++active.errors;
    active.old_native = (PGresult *)active.old;
    result_to_hold = active.old;
    clear_armed = 1;
    cpkt_postgres_result_free(active.old);
  } else {
    active.phase = 0;
    clear_armed = 1;
    fail_after_make = 1;
    active.old = cpkt_postgres_result_new_empty(active.connection,
                                                CPKT_POSTGRES_RESULT_TUPLES_OK);
    if (active.old != NULL || fail_next_malloc != 0 || fail_after_make != 0)
      ++active.errors;
  }
  if (active.replacement == NULL ||
      (void *)active.replacement != (void *)active.old_native ||
      active.reused_allocations != 1 || reuse_pending != 0 ||
      cpkt_postgres_event_result_data(active.replacement, active.event) !=
          &replacement_marker)
    ++active.errors;
  active.phase = 2;
  if (active.replacement != NULL)
    cpkt_postgres_result_free(active.replacement);
  active.phase = 3;
  if (active.source != NULL)
    cpkt_postgres_result_free(active.source);
  cpkt_postgres_connection_free(active.connection);
  expected_old_destroy = mode == 0 || mode == 2;
  expected_source_destroy = mode == 2 || mode == 3;
  expected_copy = mode == 2 || mode == 3;
  if (active.old_destroy != expected_old_destroy ||
      active.source_destroy != expected_source_destroy ||
      active.native_copy != expected_copy || active.replacement_create != 1 ||
      active.replacement_destroy != 1 || active.connection_destroy != 1 ||
      held_result_block != NULL || result_to_hold != NULL)
    ++active.errors;
  if (active.errors != 0)
    fprintf(stderr,
            "mode=%d errors=%d reused=%d old_destroy=%d source_destroy=%d "
            "replacement_create=%d replacement_destroy=%d copy=%d\n",
            mode, active.errors, active.reused_allocations, active.old_destroy,
            active.source_destroy, active.replacement_create,
            active.replacement_destroy, active.native_copy);
  return active.errors != 0;
}

int main(void) {
  int mode;
  for (mode = 0; mode < 7; ++mode) {
    if (run_case(mode))
      return mode + 1;
  }
  return 0;
}
