#include <cpkt/postgres.h>

#include <stddef.h>

static int fail_after = -1;
static int register_count;
static int destroy_count;

int cpkt_postgres_tracking_lifetime_test(int fail_tracking);
int cpkt_postgres_notice_registration_test(int fail_allocations);

void *__real_cpkt_postgres_event_allocate(size_t size);

void *__wrap_cpkt_postgres_event_allocate(size_t size) {
  if (fail_after == 0) {
    fail_after = -1;
    return NULL;
  }
  if (fail_after > 0)
    --fail_after;
  return __real_cpkt_postgres_event_allocate(size);
}

static int on_event(cpkt_postgres_event_id id,
                    const cpkt_postgres_event_info *info, void *context) {
  (void)context;
  if (info == NULL || info->event == NULL)
    return 0;
  if (id == CPKT_POSTGRES_EVENT_REGISTER)
    ++register_count;
  if (id == CPKT_POSTGRES_EVENT_CONNECTION_DESTROY)
    ++destroy_count;
  return 1;
}

int main(void) {
  cpkt_postgres_connection *connection;
  cpkt_postgres_result *result;
  int status;
  connection = cpkt_postgres_connect(
      "host=/tmp/cpkt-postgres-event-alloc-no-socket connect_timeout=1");
  if (connection == NULL)
    return 1;
  fail_after = 0;
  if (cpkt_postgres_event_register(connection, on_event, "owner-fail", NULL) !=
      NULL)
    return 2;
  fail_after = 1;
  if (cpkt_postgres_event_register(connection, on_event, "event-fail", NULL) !=
      NULL)
    return 3;
  if (cpkt_postgres_event_register(connection, on_event, "success", NULL) ==
      NULL)
    return 4;
  fail_after = 0;
  result = cpkt_postgres_result_new_empty(connection,
                                          CPKT_POSTGRES_RESULT_TUPLES_OK);
  if (result != NULL || fail_after != -1)
    return 5;
  fail_after = 1;
  result = cpkt_postgres_result_new_empty(connection,
                                          CPKT_POSTGRES_RESULT_TUPLES_OK);
  if (result != NULL || fail_after != -1)
    return 6;
  result = cpkt_postgres_result_new_empty(connection,
                                          CPKT_POSTGRES_RESULT_TUPLES_OK);
  if (result == NULL)
    return 7;
  cpkt_postgres_connection_free(connection);
  cpkt_postgres_result_free(result);
  if (register_count != 1 || destroy_count != 1)
    return 8;
  status = cpkt_postgres_tracking_lifetime_test(1);
  if (status == 0)
    status = cpkt_postgres_notice_registration_test(1);
  return status;
}
