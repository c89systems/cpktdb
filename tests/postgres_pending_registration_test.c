#include <cpkt/postgres.h>
#include <stdio.h>
#include <string.h>

int cpkt_postgres_tracking_lifetime_test(int fail_tracking);
int cpkt_postgres_notice_registration_test(int fail_allocations);

typedef struct callback_state {
  cpkt_postgres_event *event;
  int registrations;
  int creates;
  int copies;
  int destroys;
  int connection_destroys;
  int failures;
} callback_state;

typedef struct registration_state {
  callback_state pending;
  callback_state nested;
  cpkt_postgres_result *before;
  cpkt_postgres_result *after;
  int accept;
} registration_state;

static int record_event(cpkt_postgres_event_id id,
                        const cpkt_postgres_event_info *info,
                        callback_state *state) {
  if (id == CPKT_POSTGRES_EVENT_REGISTER) {
    state->event = info->event;
    ++state->registrations;
    if (!cpkt_postgres_event_set_connection_data(info->connection, info->event,
                                                 state))
      ++state->failures;
  } else if (id == CPKT_POSTGRES_EVENT_RESULT_CREATE) {
    ++state->creates;
    if (!cpkt_postgres_event_set_result_data(info->result, info->event, state))
      ++state->failures;
  } else if (id == CPKT_POSTGRES_EVENT_RESULT_COPY) {
    ++state->copies;
    if (cpkt_postgres_event_result_data(info->source, info->event) != state ||
        !cpkt_postgres_event_set_result_data(info->destination, info->event,
                                             state))
      ++state->failures;
  } else if (id == CPKT_POSTGRES_EVENT_RESULT_DESTROY) {
    ++state->destroys;
    if (cpkt_postgres_event_result_data(info->result, info->event) != state)
      ++state->failures;
  } else if (id == CPKT_POSTGRES_EVENT_CONNECTION_DESTROY) {
    ++state->connection_destroys;
    if (cpkt_postgres_event_connection_data(info->connection, info->event) !=
        state)
      ++state->failures;
  } else {
    ++state->failures;
  }
  return 1;
}

static int stable_event(cpkt_postgres_event_id id,
                        const cpkt_postgres_event_info *info, void *context) {
  return record_event(id, info, (callback_state *)context);
}

static int nested_event(cpkt_postgres_event_id id,
                        const cpkt_postgres_event_info *info, void *context) {
  return record_event(id, info, (callback_state *)context);
}

static int pending_event(cpkt_postgres_event_id id,
                         const cpkt_postgres_event_info *info, void *context) {
  registration_state *state;
  state = (registration_state *)context;
  (void)record_event(id, info, &state->pending);
  if (id != CPKT_POSTGRES_EVENT_REGISTER)
    return 1;
  state->before = cpkt_postgres_result_new_empty(
      info->connection, CPKT_POSTGRES_RESULT_TUPLES_OK);
  if (state->before == NULL ||
      cpkt_postgres_event_set_result_data(state->before, info->event, state) ||
      !cpkt_postgres_event_fire_result_create(info->connection, state->before))
    ++state->pending.failures;
  if (cpkt_postgres_event_register(info->connection, nested_event, "nested",
                                   &state->nested) == NULL)
    ++state->pending.failures;
  state->after = cpkt_postgres_result_new_empty(info->connection,
                                                CPKT_POSTGRES_RESULT_TUPLES_OK);
  if (state->after == NULL ||
      cpkt_postgres_event_set_result_data(state->after, info->event, state))
    ++state->pending.failures;
  return state->accept;
}

static int run_case(int accept) {
  callback_state stable;
  registration_state state;
  cpkt_postgres_connection *connection;
  cpkt_postgres_event *event;
  cpkt_postgres_result *fresh;
  cpkt_postgres_result *copies[2];
  memset(&stable, 0, sizeof(stable));
  memset(&state, 0, sizeof(state));
  state.accept = accept;
  connection = cpkt_postgres_connect(
      "host=/tmp/cpkt-pending-registration-no-socket connect_timeout=1");
  if (connection == NULL ||
      cpkt_postgres_event_register(connection, stable_event, "stable",
                                   &stable) == NULL)
    return 1;
  event = cpkt_postgres_event_register(connection, pending_event, "pending",
                                       &state);
  if ((event != NULL) != accept || state.before == NULL || state.after == NULL)
    return 2;
  if (state.pending.failures != 0 || state.pending.creates != 0 ||
      cpkt_postgres_event_set_result_data(state.before, state.pending.event,
                                          &state) ||
      cpkt_postgres_event_set_result_data(state.after, state.pending.event,
                                          &state)) {
    fprintf(stderr, "pending registration was captured by a result\n");
    return 3;
  }
  if (!cpkt_postgres_event_fire_result_create(connection, state.before) ||
      !cpkt_postgres_event_fire_result_create(connection, state.after))
    return 4;
  copies[0] =
      cpkt_postgres_result_copy(state.before, CPKT_POSTGRES_COPY_RESULT_EVENTS);
  copies[1] =
      cpkt_postgres_result_copy(state.after, CPKT_POSTGRES_COPY_RESULT_EVENTS);
  fresh = cpkt_postgres_result_new_empty(connection,
                                         CPKT_POSTGRES_RESULT_TUPLES_OK);
  if (copies[0] == NULL || copies[1] == NULL || fresh == NULL ||
      !cpkt_postgres_event_fire_result_create(connection, copies[0]) ||
      !cpkt_postgres_event_fire_result_create(connection, copies[1]) ||
      !cpkt_postgres_event_fire_result_create(connection, fresh))
    return 5;
  cpkt_postgres_connection_free(connection);
  cpkt_postgres_result_free(state.before);
  cpkt_postgres_result_free(state.after);
  cpkt_postgres_result_free(copies[0]);
  cpkt_postgres_result_free(copies[1]);
  cpkt_postgres_result_free(fresh);
  if (stable.registrations != 1 || stable.creates != 3 || stable.copies != 2 ||
      stable.destroys != 5 || stable.connection_destroys != 1 ||
      state.nested.registrations != 1 || state.nested.creates != 2 ||
      state.nested.copies != 1 || state.nested.destroys != 3 ||
      state.nested.connection_destroys != 1 ||
      state.pending.registrations != 1 || state.pending.creates != accept ||
      state.pending.copies != 0 || state.pending.destroys != accept ||
      state.pending.connection_destroys != accept || stable.failures != 0 ||
      state.nested.failures != 0 || state.pending.failures != 0)
    return 6;
  return 0;
}

int main(void) {
  int status;
  status = run_case(0);
  if (status == 0)
    status = run_case(1);
  if (status == 0)
    status = cpkt_postgres_tracking_lifetime_test(0);
  if (status == 0)
    status = cpkt_postgres_notice_registration_test(0);
  return status;
}
