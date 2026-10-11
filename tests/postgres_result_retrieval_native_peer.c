#include <cpkt/postgres.h>
#include <libpq-fe.h>

#include <stddef.h>

static int next_status;
static int fail_binding;
static int fail_prepare;
static int bad_connection;
static int event_destroy_count;

void *__real_cpkt_postgres_result_binding_allocate(size_t size);
int __real_cpkt_postgres_event_prepare_result(PGconn *connection,
                                              PGresult *result);

void *__wrap_cpkt_postgres_result_binding_allocate(size_t size) {
  if (fail_binding) {
    fail_binding = 0;
    return NULL;
  }
  return __real_cpkt_postgres_result_binding_allocate(size);
}

int __wrap_cpkt_postgres_event_prepare_result(PGconn *connection,
                                              PGresult *result) {
  if (fail_prepare) {
    fail_prepare = 0;
    return 0;
  }
  return __real_cpkt_postgres_event_prepare_result(connection, result);
}

PGresult *__wrap_PQgetResult(PGconn *connection) {
  int status = next_status;
  (void)connection;
  next_status = -1;
  if (status < 0)
    return NULL;
  return PQmakeEmptyPGresult(NULL, (ExecStatusType)status);
}

ConnStatusType __wrap_PQstatus(const PGconn *connection) {
  (void)connection;
  return bad_connection ? CONNECTION_BAD : CONNECTION_OK;
}

static void on_notice(void *context, cpkt_postgres_connection *connection,
                      const cpkt_postgres_result *result) {
  (void)context;
  (void)connection;
  (void)result;
}

static int on_event(cpkt_postgres_event_id id,
                    const cpkt_postgres_event_info *info, void *context) {
  (void)info;
  (void)context;
  if (id == CPKT_POSTGRES_EVENT_RESULT_DESTROY)
    ++event_destroy_count;
  return 1;
}

int main(void) {
  cpkt_postgres_connection *connection;
  cpkt_postgres_connection *independent;
  cpkt_postgres_result *result;
  cpkt_postgres_retrieval_error error;
  cpkt_postgres_retrieval_status status;
  int ok = 1;

  result = (cpkt_postgres_result *)1;
  status = cpkt_postgres_get_result_checked(NULL, &result, &error);
  if (status != CPKT_POSTGRES_RETRIEVAL_FAILED || result != NULL ||
      error.code != CPKT_POSTGRES_RETRIEVAL_ERROR_ARGUMENT ||
      error.result_discarded)
    return 1;
  result = (cpkt_postgres_result *)1;
  status = cpkt_postgres_receive_checked(NULL, &result, &error);
  if (status != CPKT_POSTGRES_RETRIEVAL_FAILED || result != NULL ||
      error.code != CPKT_POSTGRES_RETRIEVAL_ERROR_ARGUMENT)
    return 1;
  status = cpkt_postgres_get_result_checked(NULL, NULL, &error);
  if (status != CPKT_POSTGRES_RETRIEVAL_FAILED ||
      error.code != CPKT_POSTGRES_RETRIEVAL_ERROR_ARGUMENT)
    return 1;

  connection = cpkt_postgres_connect(
      "host=/tmp/cpkt-postgres-retrieval-no-socket connect_timeout=1");
  if (connection == NULL)
    return 2;
  if (!cpkt_postgres_set_notice_receiver(connection, on_notice, NULL, NULL,
                                         NULL) ||
      cpkt_postgres_event_register(connection, on_event, "retrieval", NULL) ==
          NULL)
    return 3;

  next_status = PGRES_SINGLE_TUPLE;
  status = cpkt_postgres_get_result_checked(connection, &result, &error);
  if (status != CPKT_POSTGRES_RETRIEVAL_RESULT || result == NULL ||
      error.code != CPKT_POSTGRES_RETRIEVAL_ERROR_NONE ||
      cpkt_postgres_result_status_get(result) !=
          CPKT_POSTGRES_RESULT_SINGLE_TUPLE)
    ok = 0;
  cpkt_postgres_result_free(result);

  fail_binding = 1;
  next_status = PGRES_FATAL_ERROR;
  result = (cpkt_postgres_result *)1;
  status = cpkt_postgres_get_result_checked(connection, &result, &error);
  if (status != CPKT_POSTGRES_RETRIEVAL_FAILED || result != NULL ||
      error.code != CPKT_POSTGRES_RETRIEVAL_ERROR_NOTICE_BINDING ||
      !error.result_discarded ||
      error.discarded_status != CPKT_POSTGRES_RESULT_FATAL_ERROR ||
      error.sqlstate[0] != '\0' || fail_binding)
    ok = 0;

  fail_binding = 1;
  next_status = PGRES_SINGLE_TUPLE;
  status = cpkt_postgres_get_result_checked(connection, &result, &error);
  if (status != CPKT_POSTGRES_RETRIEVAL_FAILED || result != NULL ||
      error.code != CPKT_POSTGRES_RETRIEVAL_ERROR_NOTICE_BINDING ||
      error.discarded_status != CPKT_POSTGRES_RESULT_SINGLE_TUPLE ||
      fail_binding)
    ok = 0;

  fail_binding = 1;
  next_status = PGRES_COMMAND_OK;
  status = cpkt_postgres_get_result_checked(connection, &result, &error);
  if (status != CPKT_POSTGRES_RETRIEVAL_FAILED || result != NULL ||
      error.code != CPKT_POSTGRES_RETRIEVAL_ERROR_NOTICE_BINDING ||
      error.discarded_status != CPKT_POSTGRES_RESULT_COMMAND_OK || fail_binding)
    ok = 0;

  independent = cpkt_postgres_connect(
      "host=/tmp/cpkt-postgres-retrieval-independent-no-socket "
      "connect_timeout=1");
  if (independent == NULL)
    return 5;
  next_status = PGRES_COMMAND_OK;
  status = cpkt_postgres_get_result_checked(independent, &result, &error);
  if (status != CPKT_POSTGRES_RETRIEVAL_RESULT || result == NULL ||
      error.code != CPKT_POSTGRES_RETRIEVAL_ERROR_NONE)
    ok = 0;
  cpkt_postgres_result_free(result);
  cpkt_postgres_connection_free(independent);
  next_status = -1;
  bad_connection = 1;
  status = cpkt_postgres_get_result_checked(connection, &result, &error);
  bad_connection = 0;
  if (status != CPKT_POSTGRES_RETRIEVAL_FAILED || result != NULL ||
      error.code != CPKT_POSTGRES_RETRIEVAL_ERROR_CONNECTION ||
      error.result_discarded)
    ok = 0;
  next_status = PGRES_COMMAND_OK;
  status = cpkt_postgres_get_result_checked(connection, &result, &error);
  if (status != CPKT_POSTGRES_RETRIEVAL_RESULT || result == NULL ||
      error.code != CPKT_POSTGRES_RETRIEVAL_ERROR_NONE)
    ok = 0;
  cpkt_postgres_result_free(result);

  fail_prepare = 1;
  next_status = PGRES_COMMAND_OK;
  result = (cpkt_postgres_result *)1;
  status = cpkt_postgres_get_result_checked(connection, &result, &error);
  if (status != CPKT_POSTGRES_RETRIEVAL_FAILED || result != NULL ||
      error.code != CPKT_POSTGRES_RETRIEVAL_ERROR_EVENT_PREPARATION ||
      !error.result_discarded ||
      error.discarded_status != CPKT_POSTGRES_RESULT_COMMAND_OK || fail_prepare)
    ok = 0;

  next_status = PGRES_COMMAND_OK;
  status = cpkt_postgres_get_result_checked(connection, &result, &error);
  if (status != CPKT_POSTGRES_RETRIEVAL_RESULT || result == NULL ||
      error.code != CPKT_POSTGRES_RETRIEVAL_ERROR_NONE)
    ok = 0;
  cpkt_postgres_connection_free(connection);
  cpkt_postgres_result_free(result);
  if (event_destroy_count != 0)
    ok = 0;
  return ok ? 0 : 4;
}
