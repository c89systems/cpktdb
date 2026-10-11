#include <cpkt/postgres.h>
#include <libpq-fe.h>

#include <stddef.h>
#include <stdio.h>

static PQnoticeReceiver native_receiver;
static void *native_context;
static cpkt_postgres_event *registration;
static cpkt_postgres_result *copies[2];
static cpkt_postgres_result *created_source;
static cpkt_postgres_connection *expected_connection;
static int source_data;
static int copy_count;
static int nested;
static int creates;
static int destroys;
static int connection_destroys;
static int errors;
static int notice_calls;
static int fail_next_event_allocation;
static int fail_copy_once;
static int copy_failure_seen;
static int skip_copy_count;
static int explicit_create_once;
static int source_creates;
static int source_destroys;

void *__real_cpkt_postgres_event_allocate(size_t size);
void *__wrap_cpkt_postgres_event_allocate(size_t size) {
  if (fail_next_event_allocation) {
    fail_next_event_allocation = 0;
    return NULL;
  }
  return __real_cpkt_postgres_event_allocate(size);
}

PQnoticeReceiver __real_PQsetNoticeReceiver(PGconn *connection,
                                            PQnoticeReceiver receiver,
                                            void *context);

PQnoticeReceiver __wrap_PQsetNoticeReceiver(PGconn *connection,
                                            PQnoticeReceiver receiver,
                                            void *context) {
  native_receiver = receiver;
  native_context = context;
  return __real_PQsetNoticeReceiver(connection, receiver, context);
}

static int on_event(cpkt_postgres_event_id id,
                    const cpkt_postgres_event_info *info, void *context) {
  (void)context;
  if (info == NULL || info->event == NULL) {
    ++errors;
    return 0;
  }
  if (id == CPKT_POSTGRES_EVENT_REGISTER) {
    registration = info->event;
  } else if (info->event != registration) {
    ++errors;
  } else if (id == CPKT_POSTGRES_EVENT_RESULT_CREATE) {
    if (info->result == created_source) {
      ++source_creates;
      if (info->connection != expected_connection ||
          !cpkt_postgres_event_set_result_data(info->result, registration,
                                               &source_data))
        ++errors;
    } else {
      ++creates;
      if (info->connection != NULL || info->result == NULL ||
          cpkt_postgres_event_result_data(info->result, registration) !=
              &copies[0])
        ++errors;
    }
  } else if (id == CPKT_POSTGRES_EVENT_RESULT_DESTROY) {
    if (info->result == created_source) {
      ++source_destroys;
      if (cpkt_postgres_event_result_data(info->result, registration) !=
          &source_data)
        ++errors;
    } else {
      ++destroys;
      if (cpkt_postgres_event_result_data(info->result, registration) !=
          &copies[0])
        ++errors;
    }
  } else if (id == CPKT_POSTGRES_EVENT_CONNECTION_DESTROY) {
    ++connection_destroys;
  } else if (id == CPKT_POSTGRES_EVENT_RESULT_COPY) {
    /* The notice source never received RESULT_CREATE. */
    ++errors;
  }
  return 1;
}

static void on_notice(void *context, cpkt_postgres_connection *connection,
                      const cpkt_postgres_result *notice) {
  PGresult *inner;
  cpkt_postgres_result *unexpected_copy;
  (void)context;
  ++notice_calls;
  if (skip_copy_count) {
    --skip_copy_count;
    if (!cpkt_postgres_event_set_result_data((cpkt_postgres_result *)notice,
                                             registration, &source_data))
      ++errors;
    return;
  }
  if (explicit_create_once) {
    explicit_create_once = 0;
    if (!cpkt_postgres_event_fire_result_create(connection,
                                                (cpkt_postgres_result *)notice))
      ++errors;
    return;
  }
  if (fail_copy_once) {
    fail_copy_once = 0;
    fail_next_event_allocation = 1;
    unexpected_copy =
        cpkt_postgres_result_copy(notice, CPKT_POSTGRES_COPY_RESULT_EVENTS);
    if (unexpected_copy != NULL) {
      ++errors;
      cpkt_postgres_result_free(unexpected_copy);
    } else {
      ++copy_failure_seen;
    }
    return;
  }
  if (copy_count >= 2) {
    ++errors;
    return;
  }
  copies[copy_count] =
      cpkt_postgres_result_copy(notice, CPKT_POSTGRES_COPY_RESULT_EVENTS);
  if (copies[copy_count] == NULL)
    ++errors;
  ++copy_count;
  if (!nested) {
    nested = 1;
    inner = PQmakeEmptyPGresult((PGconn *)connection, PGRES_NONFATAL_ERROR);
    if (inner == NULL)
      ++errors;
    else {
      native_receiver(native_context, inner);
      PQclear(inner);
    }
  }
}

int main(void) {
  cpkt_postgres_connection *connection;
  PGresult *notice;
  int index;
  int stage;
  stage = 1;
  connection = cpkt_postgres_connect(
      "host=/tmp/cpkt-postgres-notice-no-socket connect_timeout=1");
  if (connection == NULL ||
      cpkt_postgres_event_register(connection, on_event, "notice-lifetime",
                                   NULL) == NULL)
    return stage;
  if (!cpkt_postgres_set_notice_receiver(connection, on_notice, NULL, NULL,
                                         NULL))
    return 90;
  expected_connection = connection;
  if (native_receiver == NULL)
    return stage;
  notice = PQmakeEmptyPGresult((PGconn *)connection, PGRES_NONFATAL_ERROR);
  if (notice == NULL)
    return stage;
  fail_next_event_allocation = 1;
  native_receiver(native_context, notice);
  PQclear(notice);
  if (notice_calls != 0 || fail_next_event_allocation != 0)
    return stage;
  notice = PQmakeEmptyPGresult((PGconn *)connection, PGRES_NONFATAL_ERROR);
  if (notice == NULL)
    return stage;
  fail_copy_once = 1;
  native_receiver(native_context, notice);
  if (cpkt_postgres_event_result_data((cpkt_postgres_result *)notice,
                                      registration) != NULL)
    ++errors;
  PQclear(notice);
  if (copy_failure_seen != 1 || notice_calls != 1 || errors)
    return stage;
  skip_copy_count = 16;
  for (index = 0; index < 16; ++index) {
    notice = PQmakeEmptyPGresult((PGconn *)connection, PGRES_NONFATAL_ERROR);
    if (notice == NULL)
      return stage;
    native_receiver(native_context, notice);
    if (cpkt_postgres_event_result_data((cpkt_postgres_result *)notice,
                                        registration) != NULL)
      ++errors;
    PQclear(notice);
  }
  if (skip_copy_count != 0 || notice_calls != 17 || errors)
    return stage;
  notice = PQmakeEmptyPGresult((PGconn *)connection, PGRES_NONFATAL_ERROR);
  if (notice == NULL)
    return stage;
  created_source = (cpkt_postgres_result *)notice;
  explicit_create_once = 1;
  native_receiver(native_context, notice);
  if (source_creates != 1 || source_destroys != 0 ||
      cpkt_postgres_event_result_data(created_source, registration) !=
          &source_data)
    ++errors;
  PQclear(notice);
  created_source = NULL;
  if (source_destroys != 1 || notice_calls != 18 || errors)
    return stage;
  notice = PQmakeEmptyPGresult((PGconn *)connection, PGRES_NONFATAL_ERROR);
  if (notice == NULL)
    return stage;
  native_receiver(native_context, notice);
  PQclear(notice);
  stage = 2;
  if (copy_count != 2 || copies[0] == NULL || copies[1] == NULL ||
      notice_calls != 20 || errors)
    return stage;
  for (index = 0; index < 2; ++index) {
    if (!cpkt_postgres_event_set_result_data(copies[index], registration,
                                             &copies[0])) {
      cpkt_postgres_result_free(copies[0]);
      cpkt_postgres_result_free(copies[1]);
      cpkt_postgres_connection_free(connection);
      return 3;
    }
  }
  cpkt_postgres_connection_free(connection);
  for (index = 0; index < 2; ++index) {
    if (!cpkt_postgres_event_fire_result_create(NULL, copies[index]))
      ++errors;
    cpkt_postgres_result_free(copies[index]);
  }
  if (errors || creates != 2 || destroys != 2 || connection_destroys != 1) {
    fprintf(stderr,
            "notice lifecycle: create=%d destroy=%d conn=%d errors=%d\n",
            creates, destroys, connection_destroys, errors);
    return 4;
  }
  return 0;
}
