#include <cpkt/postgres.h>
#include <string.h>

typedef struct event_counts {
  cpkt_postgres_connection *connection;
  cpkt_postgres_event *identity;
  cpkt_postgres_event *nested;
  int register_count;
  int reset_count;
  int connection_destroy_count;
  int create_count;
  int copy_count;
  int result_destroy_count;
  int decline_register;
  int decline_create_once;
  int decline_copy_once;
  int nested_kind;
  int failures;
} event_counts;

static int event_b(cpkt_postgres_event_id id,
                   const cpkt_postgres_event_info *info, void *context);
static int event_d(cpkt_postgres_event_id id,
                   const cpkt_postgres_event_info *info, void *context);

static int run_event(event_counts *counts, cpkt_postgres_event_id id,
                     const cpkt_postgres_event_info *info) {
  if (info == 0 || info->event == 0)
    return 0;
  if (id == CPKT_POSTGRES_EVENT_REGISTER) {
    ++counts->register_count;
    counts->identity = info->event;
    if (info->connection != counts->connection ||
        !cpkt_postgres_event_set_connection_data(info->connection, info->event,
                                                 counts) ||
        cpkt_postgres_event_connection_data(info->connection, info->event) !=
            counts)
      ++counts->failures;
    if (counts->nested_kind == 1)
      counts->nested = cpkt_postgres_event_register(info->connection, event_b,
                                                    "nested-b", counts + 1);
    else if (counts->nested_kind == 2)
      counts->nested = cpkt_postgres_event_register(info->connection, event_d,
                                                    "nested-d", counts + 1);
    if (counts->nested_kind != 0 && counts->nested == 0)
      ++counts->failures;
    return !counts->decline_register;
  }
  if (id == CPKT_POSTGRES_EVENT_CONNECTION_RESET) {
    ++counts->reset_count;
    if (info->connection != counts->connection)
      ++counts->failures;
    return 1;
  }
  if (id == CPKT_POSTGRES_EVENT_CONNECTION_DESTROY) {
    ++counts->connection_destroy_count;
    if (info->connection != counts->connection ||
        cpkt_postgres_event_connection_data(info->connection, info->event) !=
            counts)
      ++counts->failures;
    return 1;
  }
  if (id == CPKT_POSTGRES_EVENT_RESULT_CREATE) {
    ++counts->create_count;
    if (info->result == 0 || info->connection != counts->connection ||
        !cpkt_postgres_event_set_result_data(info->result, info->event,
                                             counts) ||
        cpkt_postgres_event_result_data(info->result, info->event) != counts)
      ++counts->failures;
    if (counts->decline_create_once && counts->create_count == 1)
      return 0;
    return 1;
  }
  if (id == CPKT_POSTGRES_EVENT_RESULT_COPY) {
    ++counts->copy_count;
    if (info->source == 0 || info->destination == 0 ||
        info->source == info->destination ||
        cpkt_postgres_event_result_data(info->source, info->event) != counts ||
        !cpkt_postgres_event_set_result_data(info->destination, info->event,
                                             counts))
      ++counts->failures;
    if (counts->decline_copy_once && counts->copy_count == 1)
      return 0;
    return 1;
  }
  if (id == CPKT_POSTGRES_EVENT_RESULT_DESTROY) {
    ++counts->result_destroy_count;
    if (info->result == 0 ||
        cpkt_postgres_event_result_data(info->result, info->event) != counts)
      ++counts->failures;
    return 1;
  }
  ++counts->failures;
  return 0;
}

static int event_a(cpkt_postgres_event_id id,
                   const cpkt_postgres_event_info *info, void *context) {
  return run_event((event_counts *)context, id, info);
}

static int event_b(cpkt_postgres_event_id id,
                   const cpkt_postgres_event_info *info, void *context) {
  return run_event((event_counts *)context, id, info);
}

static int event_c(cpkt_postgres_event_id id,
                   const cpkt_postgres_event_info *info, void *context) {
  return run_event((event_counts *)context, id, info);
}

static int event_d(cpkt_postgres_event_id id,
                   const cpkt_postgres_event_info *info, void *context) {
  return run_event((event_counts *)context, id, info);
}

static cpkt_postgres_connection *bad_connection(void) {
  return cpkt_postgres_connect(
      "host=/tmp/cpkt-postgres-events-no-socket connect_timeout=1");
}

int main(void) {
  event_counts counts[2];
  event_counts declined[2];
  event_counts late;
  cpkt_postgres_connection *connection;
  cpkt_postgres_result *result;
  cpkt_postgres_result *copy;
  int i;
  for (i = 0; i < 2; ++i) {
    counts[i].connection = 0;
    counts[i].identity = 0;
    counts[i].nested = 0;
    counts[i].register_count = 0;
    counts[i].reset_count = 0;
    counts[i].connection_destroy_count = 0;
    counts[i].create_count = 0;
    counts[i].copy_count = 0;
    counts[i].result_destroy_count = 0;
    counts[i].decline_register = 0;
    counts[i].decline_create_once = i == 1;
    counts[i].decline_copy_once = i == 1;
    counts[i].nested_kind = i == 0;
    counts[i].failures = 0;
  }
  connection = bad_connection();
  if (connection == 0)
    return 1;
  counts[0].connection = connection;
  counts[1].connection = connection;
  if (cpkt_postgres_event_register(connection, event_a, "first", &counts[0]) ==
          0 ||
      counts[0].nested != counts[1].identity ||
      cpkt_postgres_event_register(connection, event_a, "duplicate",
                                   &counts[0]) != 0)
    return 2;
  result = cpkt_postgres_result_new_empty(connection,
                                          CPKT_POSTGRES_RESULT_TUPLES_OK);
  memset(&late, 0, sizeof(late));
  late.connection = connection;
  if (result == 0 ||
      cpkt_postgres_event_register(connection, event_c, "late", &late) == 0 ||
      cpkt_postgres_event_fire_result_create(connection, result) != 0 ||
      cpkt_postgres_event_fire_result_create(connection, result) != 1 ||
      counts[0].create_count != 1 || counts[1].create_count != 2 ||
      late.create_count != 0)
    return 3;
  copy = cpkt_postgres_result_copy(result, CPKT_POSTGRES_COPY_RESULT_EVENTS);
  if (copy == 0 || counts[0].copy_count != 1 || counts[1].copy_count != 1 ||
      cpkt_postgres_event_fire_result_create(connection, copy) != 1 ||
      counts[1].create_count != 3 || late.copy_count != 0)
    return 4;
  cpkt_postgres_connection_free(connection);
  cpkt_postgres_result_free(result);
  cpkt_postgres_result_free(copy);
  if (counts[0].connection_destroy_count != 1 ||
      counts[1].connection_destroy_count != 1 ||
      counts[0].result_destroy_count != 2 ||
      counts[1].result_destroy_count != 2 ||
      late.connection_destroy_count != 1 || late.result_destroy_count != 0 ||
      counts[0].failures != 0 || counts[1].failures != 0 || late.failures != 0)
    return 5;

  for (i = 0; i < 2; ++i) {
    declined[i].connection = 0;
    declined[i].identity = 0;
    declined[i].nested = 0;
    declined[i].register_count = 0;
    declined[i].reset_count = 0;
    declined[i].connection_destroy_count = 0;
    declined[i].create_count = 0;
    declined[i].copy_count = 0;
    declined[i].result_destroy_count = 0;
    declined[i].decline_register = i == 0;
    declined[i].decline_create_once = 0;
    declined[i].decline_copy_once = 0;
    declined[i].nested_kind = i == 0 ? 2 : 0;
    declined[i].failures = 0;
  }
  connection = bad_connection();
  if (connection == 0)
    return 6;
  declined[0].connection = connection;
  declined[1].connection = connection;
  if (cpkt_postgres_event_register(connection, event_c, "declined",
                                   &declined[0]) != 0 ||
      declined[0].nested != declined[1].identity)
    return 7;
  result = cpkt_postgres_result_new_empty(connection,
                                          CPKT_POSTGRES_RESULT_TUPLES_OK);
  if (result == 0 ||
      !cpkt_postgres_event_fire_result_create(connection, result))
    return 8;
  cpkt_postgres_result_free(result);
  cpkt_postgres_connection_free(connection);
  if (declined[0].connection_destroy_count != 0 ||
      declined[0].create_count != 0 ||
      declined[1].connection_destroy_count != 1 ||
      declined[1].result_destroy_count != 1 || declined[0].failures != 0 ||
      declined[1].failures != 0)
    return 9;
  return 0;
}
