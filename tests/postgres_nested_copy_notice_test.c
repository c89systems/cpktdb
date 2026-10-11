#include <cpkt/postgres.h>

#include <stdio.h>
#include <string.h>

typedef struct copy_case {
  cpkt_postgres_connection *connection;
  cpkt_postgres_event *event;
  cpkt_postgres_result *source;
  cpkt_postgres_result *outer;
  cpkt_postgres_result *nested;
  int nested_flags;
  int decline_outer;
  int copy_on_destroy;
  int old_notices;
  int new_notices;
  int creates;
  int copies;
  int destroys;
  int connection_destroys;
  int errors;
} copy_case;

static void old_notice(void *context, cpkt_postgres_connection *connection,
                       const cpkt_postgres_result *result) {
  copy_case *state;
  state = (copy_case *)context;
  ++state->old_notices;
  if (connection != NULL || result == NULL)
    ++state->errors;
}

static void new_notice(void *context, cpkt_postgres_connection *connection,
                       const cpkt_postgres_result *result) {
  copy_case *state;
  state = (copy_case *)context;
  ++state->new_notices;
  if (connection != NULL || result == NULL)
    ++state->errors;
}

static int on_event(cpkt_postgres_event_id id,
                    const cpkt_postgres_event_info *info, void *context) {
  copy_case *state;
  state = (copy_case *)context;
  if (info == NULL || info->event == NULL) {
    ++state->errors;
    return 0;
  }
  if (id == CPKT_POSTGRES_EVENT_REGISTER) {
    state->event = info->event;
    if (info->connection != state->connection)
      ++state->errors;
  } else if (info->event != state->event) {
    ++state->errors;
  } else if (id == CPKT_POSTGRES_EVENT_RESULT_CREATE) {
    ++state->creates;
    if ((info->result != state->source && info->result != state->nested) ||
        !cpkt_postgres_event_set_result_data(info->result, info->event, state))
      ++state->errors;
  } else if (id == CPKT_POSTGRES_EVENT_RESULT_COPY) {
    ++state->copies;
    if (info->source == state->source) {
      state->outer = info->destination;
      if (cpkt_postgres_event_result_data(info->source, info->event) != state ||
          !cpkt_postgres_event_set_result_data(info->destination, info->event,
                                               state))
        ++state->errors;
      state->nested =
          cpkt_postgres_result_copy(info->destination, state->nested_flags);
      if (state->nested == NULL)
        ++state->errors;
      if (state->decline_outer)
        return 0;
    } else if (info->source == state->outer) {
      if (cpkt_postgres_event_result_data(info->source, info->event) != state ||
          !cpkt_postgres_event_set_result_data(info->destination, info->event,
                                               state))
        ++state->errors;
    } else {
      ++state->errors;
    }
  } else if (id == CPKT_POSTGRES_EVENT_RESULT_DESTROY) {
    ++state->destroys;
    if (cpkt_postgres_event_result_data(info->result, info->event) != state)
      ++state->errors;
    if (state->copy_on_destroy && info->result == state->source) {
      state->nested = cpkt_postgres_result_copy(
          info->result, CPKT_POSTGRES_COPY_RESULT_NOTICE_HOOKS);
      if (state->nested == NULL)
        ++state->errors;
    }
  } else if (id == CPKT_POSTGRES_EVENT_CONNECTION_DESTROY) {
    ++state->connection_destroys;
    if (info->connection != state->connection)
      ++state->errors;
  } else if (id != CPKT_POSTGRES_EVENT_CONNECTION_RESET) {
    ++state->errors;
  }
  return 1;
}

static int run_case(int flags, int decline_outer) {
  copy_case state;
  cpkt_postgres_result *outer;
  memset(&state, 0, sizeof(state));
  state.nested_flags = flags;
  state.decline_outer = decline_outer;
  state.connection = cpkt_postgres_connect(
      "host=/tmp/cpkt-postgres-nested-copy-no-socket connect_timeout=1");
  if (state.connection == NULL)
    return 1;
  if (!cpkt_postgres_set_notice_receiver(state.connection, old_notice, &state,
                                         NULL, NULL))
    return 90;
  if (cpkt_postgres_event_register(state.connection, on_event, "nested-copy",
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
  outer = cpkt_postgres_result_copy(state.source,
                                    CPKT_POSTGRES_COPY_RESULT_EVENTS |
                                        CPKT_POSTGRES_COPY_RESULT_NOTICE_HOOKS);
  if (outer == NULL || outer != state.outer || state.nested == NULL ||
      state.copies != 1 || state.errors != 0) {
    fprintf(stderr,
            "before teardown flags=%d outer=%p seen=%p nested=%p copies=%d "
            "errors=%d\n",
            flags, (void *)outer, (void *)state.outer, (void *)state.nested,
            state.copies, state.errors);
    return 4;
  }
  cpkt_postgres_connection_free(state.connection);
  cpkt_postgres_result_free(state.source);
  cpkt_postgres_result_free(outer);
  if (flags & CPKT_POSTGRES_COPY_RESULT_EVENTS) {
    if (!cpkt_postgres_event_fire_result_create(NULL, state.nested) ||
        state.creates != 2)
      ++state.errors;
  }
  if (cpkt_postgres_result_field_name(state.nested, 99) != NULL)
    ++state.errors;
  if (state.old_notices !=
          ((flags & CPKT_POSTGRES_COPY_RESULT_NOTICE_HOOKS) ? 1 : 0) ||
      state.new_notices != 0 || state.connection_destroys != 1 ||
      state.destroys != (decline_outer ? 1 : 2))
    ++state.errors;
  cpkt_postgres_result_free(state.nested);
  if (flags & CPKT_POSTGRES_COPY_RESULT_EVENTS) {
    if (state.destroys != (decline_outer ? 2 : 3))
      ++state.errors;
  }
  if (state.errors) {
    fprintf(stderr,
            "nested flags=%d copies=%d destroys=%d old=%d new=%d errors=%d\n",
            flags, state.copies, state.destroys, state.old_notices,
            state.new_notices, state.errors);
    return 5;
  }
  return 0;
}

static int run_destroy_case(void) {
  copy_case state;
  memset(&state, 0, sizeof(state));
  state.copy_on_destroy = 1;
  state.connection = cpkt_postgres_connect(
      "host=/tmp/cpkt-postgres-nested-destroy-no-socket connect_timeout=1");
  if (state.connection == NULL)
    return 1;
  if (!cpkt_postgres_set_notice_receiver(state.connection, old_notice, &state,
                                         NULL, NULL))
    return 90;
  if (cpkt_postgres_event_register(state.connection, on_event, "destroy-copy",
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
  cpkt_postgres_connection_free(state.connection);
  cpkt_postgres_result_free(state.source);
  if (state.nested == NULL || state.destroys != 1 || state.errors != 0)
    return 4;
  if (cpkt_postgres_result_field_name(state.nested, 99) != NULL)
    ++state.errors;
  if (state.old_notices != 1 || state.new_notices != 0 ||
      state.connection_destroys != 1)
    ++state.errors;
  cpkt_postgres_result_free(state.nested);
  if (state.errors) {
    fprintf(stderr, "destroy copy old=%d new=%d errors=%d\n", state.old_notices,
            state.new_notices, state.errors);
    return 5;
  }
  return 0;
}

int main(void) {
  int status;
  status = run_case(CPKT_POSTGRES_COPY_RESULT_NOTICE_HOOKS, 0);
  if (status != 0)
    return status;
  status = run_case(CPKT_POSTGRES_COPY_RESULT_EVENTS |
                        CPKT_POSTGRES_COPY_RESULT_NOTICE_HOOKS,
                    0);
  if (status != 0)
    return status + 10;
  status = run_case(CPKT_POSTGRES_COPY_RESULT_EVENTS, 0);
  if (status != 0)
    return status + 20;
  status = run_case(CPKT_POSTGRES_COPY_RESULT_NOTICE_HOOKS, 1);
  if (status != 0)
    return status + 30;
  status = run_destroy_case();
  return status == 0 ? 0 : status + 40;
}
