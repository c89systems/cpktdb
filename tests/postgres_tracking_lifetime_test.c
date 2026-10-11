#include <cpkt/postgres.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct tracking_case {
  cpkt_postgres_connection *connection;
  cpkt_postgres_result *source;
  cpkt_postgres_result *outer;
  cpkt_postgres_result *nested;
  int fail_tracking;
  int nested_flags;
  int old_notices;
  int new_notices;
  int destroys;
  int errors;
} tracking_case;

#if defined(CPKT_POSTGRES_TRACKING_FAULT_INJECTION)
static int fail_next_malloc;
void *__real_malloc(size_t size);
void *__wrap_malloc(size_t size) {
  if (fail_next_malloc) {
    fail_next_malloc = 0;
    return NULL;
  }
  return __real_malloc(size);
}
#endif

static void old_notice(void *context, cpkt_postgres_connection *connection,
                       const cpkt_postgres_result *result) {
  tracking_case *state;
  state = (tracking_case *)context;
  ++state->old_notices;
  if (connection != NULL || result == NULL)
    ++state->errors;
}

static void new_notice(void *context, cpkt_postgres_connection *connection,
                       const cpkt_postgres_result *result) {
  tracking_case *state;
  (void)connection;
  (void)result;
  state = (tracking_case *)context;
  ++state->new_notices;
}

static int on_event(cpkt_postgres_event_id id,
                    const cpkt_postgres_event_info *info, void *context) {
  tracking_case *state;
  state = (tracking_case *)context;
  if (id == CPKT_POSTGRES_EVENT_RESULT_COPY && info->source == state->source) {
    state->outer = info->destination;
#if defined(CPKT_POSTGRES_TRACKING_FAULT_INJECTION)
    if (state->fail_tracking)
      fail_next_malloc = 1;
#endif
  } else if (id == CPKT_POSTGRES_EVENT_RESULT_DESTROY) {
    ++state->destroys;
    if (info->result == state->outer) {
      state->nested =
          cpkt_postgres_result_copy(info->result, state->nested_flags);
      if (state->nested == NULL)
        ++state->errors;
    }
  }
  return 1;
}

static int run_case(int fail_tracking, int close_first, int nested_flags) {
  tracking_case state;
  cpkt_postgres_result *outer;
  memset(&state, 0, sizeof(state));
  state.fail_tracking = fail_tracking;
  state.nested_flags = nested_flags;
  state.connection = cpkt_postgres_connect(
      "host=/tmp/cpkt-postgres-tracking-no-socket connect_timeout=1");
  if (state.connection == NULL)
    return 1;
  if (!cpkt_postgres_set_notice_receiver(state.connection, old_notice, &state,
                                         NULL, NULL))
    return 90;
  if (cpkt_postgres_event_register(state.connection, on_event, "tracking",
                                   &state) == NULL)
    return 2;
  state.source = cpkt_postgres_result_new_empty(state.connection,
                                                CPKT_POSTGRES_RESULT_TUPLES_OK);
  if (state.source == NULL ||
      !cpkt_postgres_event_fire_result_create(state.connection, state.source))
    return 3;
  if (!cpkt_postgres_set_notice_receiver(state.connection, new_notice, &state,
                                         NULL, NULL))
    return 90;
  if (close_first)
    cpkt_postgres_connection_free(state.connection);
  outer = cpkt_postgres_result_copy(state.source,
                                    CPKT_POSTGRES_COPY_RESULT_EVENTS |
                                        CPKT_POSTGRES_COPY_RESULT_NOTICE_HOOKS);
  if ((outer == NULL) != fail_tracking || state.outer == NULL)
    return 4;
#if defined(CPKT_POSTGRES_TRACKING_FAULT_INJECTION)
  if (fail_next_malloc != 0)
    return 5;
#endif
  if (outer != NULL)
    cpkt_postgres_result_free(outer);
  if (state.nested == NULL || state.destroys != 1 || state.errors != 0)
    return 6;
  if (!close_first)
    cpkt_postgres_connection_free(state.connection);
  cpkt_postgres_result_free(state.source);
  if (cpkt_postgres_result_field_name(state.nested, 99) != NULL ||
      state.old_notices != 1 || state.new_notices != 0 || state.destroys != 2)
    ++state.errors;
  cpkt_postgres_result_free(state.nested);
  if (state.destroys !=
      ((nested_flags & CPKT_POSTGRES_COPY_RESULT_EVENTS) ? 3 : 2))
    ++state.errors;
  if (state.errors != 0)
    fprintf(stderr, "tracking failure=%d closed=%d flags=%d errors=%d\n",
            fail_tracking, close_first, nested_flags, state.errors);
  return state.errors != 0;
}

/* Shared C89 cases also run in the existing native Darwin CI consumers. */
int cpkt_postgres_tracking_lifetime_test(int fail_tracking) {
  int close_first;
  int with_events;
  int status;
  for (close_first = 0; close_first < 2; ++close_first) {
    for (with_events = 0; with_events < 2; ++with_events) {
      status =
          run_case(fail_tracking, close_first,
                   CPKT_POSTGRES_COPY_RESULT_NOTICE_HOOKS |
                       (with_events ? CPKT_POSTGRES_COPY_RESULT_EVENTS : 0));
      if (status != 0)
        return status;
    }
  }
  return 0;
}
