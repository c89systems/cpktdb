#include <cpkt/postgres.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(CPKT_POSTGRES_RETRIEVAL_E2E_FAULT)
static int fail_next_binding;
void *__real_cpkt_postgres_result_binding_allocate(size_t size);
void *__wrap_cpkt_postgres_result_binding_allocate(size_t size) {
  if (fail_next_binding) {
    fail_next_binding = 0;
    return NULL;
  }
  return __real_cpkt_postgres_result_binding_allocate(size);
}
#endif

static int checked_result_retrieval(cpkt_postgres *receiver) {
  cpkt_postgres_connection *connection;
  cpkt_postgres_retrieval_error error;
  cpkt_postgres_retrieval_status status;
  cpkt_postgres_result *result;
  int rows;
  int terminals;
  int ok;

  connection = receiver->connection;
  if (!cpkt_postgres_send_query(connection, "SELECT 1 UNION ALL SELECT 2") ||
      !cpkt_postgres_set_single_row_mode(connection))
    return 0;
  rows = terminals = 0;
  ok = 1;
  do {
    result = (cpkt_postgres_result *)1;
    status = cpkt_postgres_receive_checked(receiver, &result, &error);
    if (status == CPKT_POSTGRES_RETRIEVAL_RESULT) {
      if (result == NULL || error.code != CPKT_POSTGRES_RETRIEVAL_ERROR_NONE)
        ok = 0;
      else if (cpkt_postgres_result_status_get(result) ==
               CPKT_POSTGRES_RESULT_SINGLE_TUPLE)
        ++rows;
      else if (cpkt_postgres_result_status_get(result) ==
               CPKT_POSTGRES_RESULT_TUPLES_OK)
        ++terminals;
      else
        ok = 0;
      cpkt_postgres_result_free(result);
    } else if (status != CPKT_POSTGRES_RETRIEVAL_DRAINED || result != NULL ||
               error.code != CPKT_POSTGRES_RETRIEVAL_ERROR_NONE)
      ok = 0;
  } while (status == CPKT_POSTGRES_RETRIEVAL_RESULT);
  if (rows != 2 || terminals != 1)
    ok = 0;
  if (!ok)
    fprintf(stderr,
            "checked retrieval rows=%d terminals=%d final_status=%d error=%d\n",
            rows, terminals, (int)status, (int)error.code);

  if (!cpkt_postgres_send_query(connection, "SELECT 1/0"))
    return 0;
  status = cpkt_postgres_get_result_checked(connection, &result, &error);
  if (status != CPKT_POSTGRES_RETRIEVAL_RESULT || result == NULL ||
      cpkt_postgres_result_status_get(result) !=
          CPKT_POSTGRES_RESULT_FATAL_ERROR ||
      cpkt_postgres_result_error_field(result, CPKT_POSTGRES_DIAG_SQLSTATE) ==
          NULL ||
      strcmp(
          cpkt_postgres_result_error_field(result, CPKT_POSTGRES_DIAG_SQLSTATE),
          "22012") != 0)
    ok = 0;
  cpkt_postgres_result_free(result);
  status = cpkt_postgres_get_result_checked(connection, &result, &error);
  if (status != CPKT_POSTGRES_RETRIEVAL_DRAINED || result != NULL)
    ok = 0;
  if (!ok)
    fprintf(stderr, "checked retrieval server status=%d error=%d\n",
            (int)status, (int)error.code);

#if defined(CPKT_POSTGRES_RETRIEVAL_E2E_FAULT)
  if (!cpkt_postgres_send_query(connection, "SELECT 1/0"))
    return 0;
  fail_next_binding = 1;
  result = (cpkt_postgres_result *)1;
  status = cpkt_postgres_get_result_checked(connection, &result, &error);
  if (status != CPKT_POSTGRES_RETRIEVAL_FAILED || result != NULL ||
      error.code != CPKT_POSTGRES_RETRIEVAL_ERROR_NOTICE_BINDING ||
      !error.result_discarded ||
      error.discarded_status != CPKT_POSTGRES_RESULT_FATAL_ERROR ||
      strcmp(error.sqlstate, "22012") != 0 || error.server_message[0] == '\0' ||
      fail_next_binding)
    ok = 0;
  if (!ok)
    fprintf(stderr,
            "checked retrieval fault status=%d error=%d discarded=%d "
            "result_status=%d sqlstate=%s\n",
            (int)status, (int)error.code, error.result_discarded,
            (int)error.discarded_status, error.sqlstate);
  status = cpkt_postgres_get_result_checked(connection, &result, &error);
  if (status != CPKT_POSTGRES_RETRIEVAL_DRAINED || result != NULL ||
      error.code != CPKT_POSTGRES_RETRIEVAL_ERROR_NONE)
    ok = 0;
#endif
  return ok;
}

static int require_value(const cpkt_postgres_result *result,
                         const char *expected) {
  char *value;

  if (result == 0 ||
      cpkt_postgres_result_status_get(result) !=
          CPKT_POSTGRES_RESULT_TUPLES_OK ||
      cpkt_postgres_result_row_count(result) != 1 ||
      cpkt_postgres_result_field_count(result) != 1) {
    return 0;
  }
  value = cpkt_postgres_result_value(result, 0, 0);
  return value != 0 && strcmp(value, expected) == 0;
}

static int require_command(const cpkt_postgres_result *result) {
  return result != 0 && cpkt_postgres_result_status_get(result) ==
                            CPKT_POSTGRES_RESULT_COMMAND_OK;
}

typedef struct trace_capture {
  int records;
  int frontend;
  int backend;
  int invalid;
} trace_capture;

static void on_protocol_trace(void *context,
                              cpkt_postgres_connection *connection,
                              const char *record, size_t length) {
  trace_capture *capture;

  capture = (trace_capture *)context;
  if (connection == NULL || record == NULL || length < 3 ||
      record[length - 1] != '\n' || record[1] != '\t') {
    capture->invalid++;
    return;
  }
  capture->records++;
  if (record[0] == 'F')
    capture->frontend++;
  else if (record[0] == 'B')
    capture->backend++;
  else
    capture->invalid++;
}

static int protocol_trace_smoke(cpkt_postgres_connection *connection) {
  trace_capture capture;
  cpkt_postgres_result *result;
  FILE *trace_file;
  int before;
  int ok;

  memset(&capture, 0, sizeof(capture));
  if (!cpkt_postgres_set_trace_sink(connection, on_protocol_trace, &capture))
    return 0;
  cpkt_postgres_set_trace_flags(connection,
                                CPKT_POSTGRES_TRACE_SUPPRESS_TIMESTAMPS);
  result = cpkt_postgres_execute(connection, "SELECT 1");
  ok = require_value(result, "1");
  cpkt_postgres_result_free(result);
  if (capture.frontend < 1 || capture.backend < 1 || capture.invalid != 0)
    ok = 0;
  before = capture.records;
  cpkt_postgres_untrace(connection);
  result = cpkt_postgres_execute(connection, "SELECT 1");
  if (!require_value(result, "1") || capture.records != before)
    ok = 0;
  cpkt_postgres_result_free(result);

  trace_file = tmpfile();
  if (trace_file == NULL)
    return 0;
  cpkt_postgres_trace(connection, trace_file);
  result = cpkt_postgres_execute(connection, "SELECT 1");
  if (!require_value(result, "1"))
    ok = 0;
  cpkt_postgres_result_free(result);
  cpkt_postgres_untrace(connection);
  if (ftell(trace_file) <= 0 || capture.records != before)
    ok = 0;
  fclose(trace_file);
  return ok;
}

typedef struct live_events {
  cpkt_postgres_event *identity;
  int registrations;
  int results;
  int destroyed;
  int failures;
} live_events;

static int live_event(cpkt_postgres_event_id id,
                      const cpkt_postgres_event_info *info, void *context) {
  live_events *events;
  events = (live_events *)context;
  if (info == NULL || info->event == NULL) {
    ++events->failures;
    return 0;
  }
  if (id == CPKT_POSTGRES_EVENT_REGISTER) {
    events->identity = info->event;
    ++events->registrations;
    if (!cpkt_postgres_event_set_connection_data(info->connection, info->event,
                                                 events))
      ++events->failures;
  } else if (info->event != events->identity) {
    ++events->failures;
  } else if (id == CPKT_POSTGRES_EVENT_RESULT_CREATE) {
    ++events->results;
    if (!cpkt_postgres_event_set_result_data(info->result, info->event, events))
      ++events->failures;
  } else if (id == CPKT_POSTGRES_EVENT_RESULT_DESTROY) {
    ++events->destroyed;
    if (cpkt_postgres_event_result_data(info->result, info->event) != events)
      ++events->failures;
  }
  return 1;
}

static int shared_metadata_and_errors(cpkt_postgres_connection *connection) {
  cpkt_postgres_result *result;
  const char *parameters[1];
  const int lengths[1] = {3};
  const int formats[1] = {1};
  char binary[3];
  int ok;
  ok = 1;
  result = cpkt_postgres_execute(
      connection, "SELECT 'abc' AS text_value, NULL::TEXT AS absent");
  if (result == NULL ||
      cpkt_postgres_result_status_get(result) !=
          CPKT_POSTGRES_RESULT_TUPLES_OK ||
      cpkt_postgres_result_row_count(result) != 1 ||
      cpkt_postgres_result_field_count(result) != 2 ||
      strcmp(cpkt_postgres_result_field_name(result, 0), "text_value") != 0 ||
      cpkt_postgres_result_field_number(result, "absent") != 1 ||
      cpkt_postgres_result_value_length(result, 0, 0) != 3 ||
      cpkt_postgres_result_value_is_null(result, 0, 0) ||
      !cpkt_postgres_result_value_is_null(result, 0, 1))
    ok = 0;
  cpkt_postgres_result_free(result);
  binary[0] = 'A';
  binary[1] = '\0';
  binary[2] = 'Z';
  parameters[0] = binary;
  result = cpkt_postgres_execute_params(connection, "SELECT $1::BYTEA", 1, NULL,
                                        parameters, lengths, formats, 1);
  if (result == NULL ||
      cpkt_postgres_result_status_get(result) !=
          CPKT_POSTGRES_RESULT_TUPLES_OK ||
      cpkt_postgres_result_field_format(result, 0) != 1 ||
      cpkt_postgres_result_value_length(result, 0, 0) != 3 ||
      memcmp(cpkt_postgres_result_value(result, 0, 0), binary, 3) != 0)
    ok = 0;
  cpkt_postgres_result_free(result);
  result = cpkt_postgres_execute(connection, "SELECT FROM");
  if (result == NULL ||
      cpkt_postgres_result_status_get(result) !=
          CPKT_POSTGRES_RESULT_FATAL_ERROR ||
      cpkt_postgres_result_error_message(result) == NULL ||
      cpkt_postgres_result_error_field(result, CPKT_POSTGRES_DIAG_SQLSTATE) ==
          NULL)
    ok = 0;
  cpkt_postgres_result_free(result);
  result = cpkt_postgres_execute(connection, "SELECT 7");
  if (!require_value(result, "7"))
    ok = 0;
  cpkt_postgres_result_free(result);
  return ok;
}

static int shared_single_rows(cpkt_postgres_connection *connection) {
  cpkt_postgres_result *result;
  int rows;
  int ok;
  if (!cpkt_postgres_send_query(connection, "SELECT 1 UNION ALL SELECT 2") ||
      !cpkt_postgres_set_single_row_mode(connection))
    return 0;
  rows = 0;
  ok = 1;
  while ((result = cpkt_postgres_get_result(connection)) != NULL) {
    if (cpkt_postgres_result_status_get(result) ==
        CPKT_POSTGRES_RESULT_SINGLE_TUPLE) {
      if (cpkt_postgres_result_row_count(result) != 1)
        ok = 0;
      ++rows;
    } else if (cpkt_postgres_result_status_get(result) !=
               CPKT_POSTGRES_RESULT_TUPLES_OK) {
      ok = 0;
    }
    cpkt_postgres_result_free(result);
  }
  return ok && rows == 2;
}

static int shared_chunked_rows(cpkt_postgres_connection *connection) {
  cpkt_postgres_result *result;
  int rows;
  int chunks;
  int ok;
  if (!cpkt_postgres_send_query(
          connection, "SELECT 1 UNION ALL SELECT 2 UNION ALL SELECT 3") ||
      !cpkt_postgres_set_chunked_rows_mode(connection, 2))
    return 0;
  rows = chunks = 0;
  ok = 1;
  while ((result = cpkt_postgres_get_result(connection)) != NULL) {
    if (cpkt_postgres_result_status_get(result) ==
        CPKT_POSTGRES_RESULT_TUPLES_CHUNK) {
      rows += cpkt_postgres_result_row_count(result);
      ++chunks;
    } else if (cpkt_postgres_result_status_get(result) !=
               CPKT_POSTGRES_RESULT_TUPLES_OK) {
      ok = 0;
    }
    cpkt_postgres_result_free(result);
  }
  return ok && rows == 3 && chunks == 2;
}

static int postgres_pipeline(cpkt_postgres_connection *connection) {
  cpkt_postgres_result *result;
  cpkt_postgres_result_status status;
  int values;
  int failures;
  int aborted;
  int syncs;
  int attempts;
  if (!cpkt_postgres_enter_pipeline_mode(connection) ||
      !cpkt_postgres_send_query_params(connection, "SELECT 11", 0, NULL, NULL,
                                       NULL, NULL, 0) ||
      !cpkt_postgres_send_query_params(connection, "SELECT FROM", 0, NULL, NULL,
                                       NULL, NULL, 0) ||
      !cpkt_postgres_send_query_params(connection, "SELECT 12", 0, NULL, NULL,
                                       NULL, NULL, 0) ||
      !cpkt_postgres_pipeline_sync(connection))
    return 0;
  values = failures = aborted = syncs = 0;
  for (attempts = 0; attempts < 16 && syncs == 0; ++attempts) {
    result = cpkt_postgres_get_result(connection);
    if (result == NULL)
      continue;
    status = cpkt_postgres_result_status_get(result);
    if (status == CPKT_POSTGRES_RESULT_TUPLES_OK && require_value(result, "11"))
      ++values;
    else if (status == CPKT_POSTGRES_RESULT_FATAL_ERROR)
      ++failures;
    else if (status == CPKT_POSTGRES_RESULT_PIPELINE_ABORTED)
      ++aborted;
    else if (status == CPKT_POSTGRES_RESULT_PIPELINE_SYNC)
      ++syncs;
    cpkt_postgres_result_free(result);
  }
  if (!cpkt_postgres_exit_pipeline_mode(connection))
    return 0;
  result = cpkt_postgres_execute(connection, "SELECT 13");
  if (!require_value(result, "13")) {
    cpkt_postgres_result_free(result);
    return 0;
  }
  cpkt_postgres_result_free(result);
  return values == 1 && failures == 1 && aborted == 1 && syncs == 1;
}

static int postgres_copy(cpkt_postgres_connection *connection) {
  cpkt_postgres_result *result;
  char *data;
  int byte_count;
  int total;
  int chunks;
  int ok;
  result = cpkt_postgres_execute(
      connection, "CREATE TEMP TABLE cpkt_e2e_copy (value TEXT)");
  ok = require_command(result);
  cpkt_postgres_result_free(result);
  if (!ok)
    return 0;
  result = cpkt_postgres_execute(
      connection, "COPY cpkt_e2e_copy FROM STDIN WITH (FORMAT csv)");
  ok = result != NULL &&
       cpkt_postgres_result_status_get(result) == CPKT_POSTGRES_RESULT_COPY_IN;
  cpkt_postgres_result_free(result);
  if (!ok || cpkt_postgres_put_copy_data(connection, "ab", 2) != 1 ||
      cpkt_postgres_put_copy_data(connection, "c\n", 2) != 1 ||
      cpkt_postgres_put_copy_data(connection, "def\n", 4) != 1 ||
      cpkt_postgres_put_copy_end(connection, NULL) != 1)
    return 0;
  result = cpkt_postgres_get_result(connection);
  ok = require_command(result);
  cpkt_postgres_result_free(result);
  if (!ok)
    return 0;
  result = cpkt_postgres_execute(connection,
                                 "COPY (SELECT value FROM cpkt_e2e_copy ORDER "
                                 "BY value) TO STDOUT WITH (FORMAT csv)");
  ok = result != NULL &&
       cpkt_postgres_result_status_get(result) == CPKT_POSTGRES_RESULT_COPY_OUT;
  cpkt_postgres_result_free(result);
  if (!ok)
    return 0;
  total = chunks = 0;
  for (;;) {
    data = NULL;
    byte_count = cpkt_postgres_get_copy_data(connection, &data, 0);
    if (byte_count == -1)
      break;
    if (byte_count <= 0 || data == NULL)
      return 0;
    total += byte_count;
    ++chunks;
    cpkt_postgres_text_free(data);
  }
  result = cpkt_postgres_get_result(connection);
  ok = require_command(result) && total == 8 && chunks >= 2;
  cpkt_postgres_result_free(result);
  return ok;
}

static int postgres_large_object(cpkt_postgres_connection *connection) {
  cpkt_postgres_result *result;
  cpkt_postgres_oid object;
  cpkt_postgres_i64 offset;
  cpkt_postgres_i64 observed;
  int descriptor;
  int ok;
  result = cpkt_postgres_execute(connection, "BEGIN");
  ok = require_command(result);
  cpkt_postgres_result_free(result);
  if (!ok)
    return 0;
  object = cpkt_postgres_large_object_create(connection, 0);
  descriptor = cpkt_postgres_large_object_open(
      connection, object,
      CPKT_POSTGRES_LARGE_OBJECT_READ | CPKT_POSTGRES_LARGE_OBJECT_WRITE);
  offset.high = 1UL;
  offset.low = 5UL;
  if (object == 0 || descriptor < 0 ||
      cpkt_postgres_large_object_seek64(connection, descriptor, offset,
                                        SEEK_SET)
              .high != 1UL ||
      cpkt_postgres_large_object_write(connection, descriptor, "Q", 1) != 1)
    ok = 0;
  if (ok) {
    observed = cpkt_postgres_large_object_tell64(connection, descriptor);
    if (observed.high != 1UL || observed.low != 6UL)
      ok = 0;
  }
  if (descriptor >= 0 &&
      cpkt_postgres_large_object_close(connection, descriptor) != 0)
    ok = 0;
  if (object != 0 && cpkt_postgres_large_object_unlink(connection, object) != 1)
    ok = 0;
  result = cpkt_postgres_execute(connection, "ROLLBACK");
  if (!require_command(result))
    ok = 0;
  cpkt_postgres_result_free(result);
  return ok;
}

typedef struct live_notice {
  cpkt_postgres_connection *connection;
  cpkt_postgres_result *copy;
  int messages;
  int failures;
} live_notice;

static void on_notice(void *context, cpkt_postgres_connection *connection,
                      const char *message) {
  live_notice *notice;
  notice = (live_notice *)context;
  if (connection != notice->connection || message == NULL ||
      strstr(message, "cpkt e2e notice") == NULL)
    ++notice->failures;
  ++notice->messages;
}

static void on_notice_result(void *context,
                             cpkt_postgres_connection *connection,
                             const cpkt_postgres_result *result) {
  live_notice *notice;
  notice = (live_notice *)context;
  if (connection != notice->connection || result == NULL ||
      notice->copy != NULL) {
    ++notice->failures;
    return;
  }
  notice->copy =
      cpkt_postgres_result_copy(result, CPKT_POSTGRES_COPY_RESULT_EVENTS);
  if (notice->copy == NULL)
    ++notice->failures;
}

static int postgres_notice_and_notify(cpkt_postgres_connection *connection,
                                      cpkt_postgres_result **copy_out,
                                      live_notice *notice) {
  cpkt_postgres_result *result;
  cpkt_postgres_notification *notification;
  int ok;
  memset(notice, 0, sizeof(*notice));
  *copy_out = NULL;
  notice->connection = connection;
  if (!cpkt_postgres_set_notice_processor(connection, on_notice, notice, NULL,
                                          NULL))
    return 0;
  result = cpkt_postgres_execute(
      connection, "DO $$ BEGIN RAISE NOTICE 'cpkt e2e notice'; END $$");
  ok = require_command(result);
  cpkt_postgres_result_free(result);
  if (!cpkt_postgres_set_notice_processor(connection, NULL, NULL, NULL, NULL))
    return 0;
  if (!ok || notice->messages != 1 || notice->failures != 0)
    return 0;
  if (!cpkt_postgres_set_notice_receiver(connection, on_notice_result, notice,
                                         NULL, NULL))
    return 0;
  result = cpkt_postgres_execute(
      connection, "DO $$ BEGIN RAISE NOTICE 'cpkt e2e copy'; END $$");
  ok = require_command(result);
  cpkt_postgres_result_free(result);
  if (!cpkt_postgres_set_notice_receiver(connection, NULL, NULL, NULL, NULL)) {
    cpkt_postgres_result_free(notice->copy);
    notice->copy = NULL;
    return 0;
  }
  if (!ok || notice->copy == NULL || notice->failures != 0) {
    cpkt_postgres_result_free(notice->copy);
    return 0;
  }
  *copy_out = notice->copy;
  result = cpkt_postgres_execute(connection, "LISTEN cpkt_e2e_channel");
  ok = require_command(result);
  cpkt_postgres_result_free(result);
  if (!ok)
    return 0;
  result =
      cpkt_postgres_execute(connection, "NOTIFY cpkt_e2e_channel, 'payload'");
  ok = require_command(result);
  cpkt_postgres_result_free(result);
  if (!ok)
    return 0;
  (void)cpkt_postgres_consume_input(connection);
  notification = cpkt_postgres_notification_next(connection);
  ok = notification != NULL &&
       strcmp(notification->channel, "cpkt_e2e_channel") == 0 &&
       strcmp(notification->payload, "payload") == 0 &&
       notification->backend_pid > 0;
  cpkt_postgres_notification_free(notification);
  return ok;
}

static int shared_escaping(cpkt_postgres_connection *connection) {
  char *literal;
  char *identifier;
  unsigned char *encoded;
  unsigned char *decoded;
  unsigned char source[3];
  size_t encoded_length;
  size_t decoded_length;
  int ok;
  source[0] = 'x';
  source[1] = '\0';
  source[2] = 'z';
  literal = cpkt_postgres_escape_literal(connection, "a'b", 3);
  identifier = cpkt_postgres_escape_identifier(connection, "a\"b", 3);
  encoded = cpkt_postgres_escape_bytea_connection(connection, source, 3,
                                                  &encoded_length);
  decoded = encoded == NULL
                ? NULL
                : cpkt_postgres_unescape_bytea(encoded, &decoded_length);
  ok = literal != NULL && strcmp(literal, "'a''b'") == 0 &&
       identifier != NULL && strcmp(identifier, "\"a\"\"b\"") == 0 &&
       encoded != NULL && encoded_length > 3 && decoded != NULL &&
       decoded_length == 3 && memcmp(decoded, source, 3) == 0;
  cpkt_postgres_text_free(literal);
  cpkt_postgres_text_free(identifier);
  cpkt_postgres_bytea_free(encoded);
  cpkt_postgres_bytea_free(decoded);
  return ok;
}

static cpkt_postgres_i64 deadline_after(cpkt_postgres_i64 now,
                                        unsigned long microseconds) {
  if (now.low > 0xffffffffUL - microseconds)
    ++now.high;
  now.low = (now.low + microseconds) & 0xffffffffUL;
  return now;
}

static int shared_nonblocking(cpkt_postgres_connection *connection) {
  cpkt_postgres_i64 deadline;
  cpkt_postgres_result *result;
  int status;
  int attempts;
  if (cpkt_postgres_set_nonblocking(connection, 1) != 0 ||
      !cpkt_postgres_is_nonblocking(connection) ||
      !cpkt_postgres_send_query(connection, "SELECT 17"))
    return 0;
  deadline =
      deadline_after(cpkt_postgres_current_time_microseconds(), 5000000UL);
  for (attempts = 0; attempts < 100; ++attempts) {
    status = cpkt_postgres_flush(connection);
    if (status == 0)
      break;
    if (status < 0 ||
        cpkt_postgres_socket_poll(cpkt_postgres_socket(connection), 0, 1,
                                  deadline) <= 0)
      return 0;
  }
  if (attempts == 100)
    return 0;
  for (attempts = 0; attempts < 100 && cpkt_postgres_is_busy(connection);
       ++attempts) {
    if (cpkt_postgres_socket_poll(cpkt_postgres_socket(connection), 1, 0,
                                  deadline) <= 0 ||
        !cpkt_postgres_consume_input(connection))
      return 0;
  }
  if (attempts == 100 || cpkt_postgres_set_nonblocking(connection, 0) != 0)
    return 0;
  result = cpkt_postgres_get_result(connection);
  status = require_value(result, "17");
  cpkt_postgres_result_free(result);
  if (cpkt_postgres_get_result(connection) != NULL)
    return 0;
  return status;
}

static int postgres_cancel_and_reset(cpkt_postgres_connection *connection) {
  cpkt_postgres_result *result;
  cpkt_postgres_cancel *cancel;
  cpkt_postgres_i64 deadline;
  char error[256];
  int ok;
  result =
      cpkt_postgres_execute(connection, "SET statement_timeout = '2000ms'");
  ok = require_command(result);
  cpkt_postgres_result_free(result);
  if (!ok || !cpkt_postgres_send_query(connection, "SELECT pg_sleep(10)"))
    return 0;
  deadline =
      deadline_after(cpkt_postgres_current_time_microseconds(), 100000UL);
  (void)cpkt_postgres_socket_poll(cpkt_postgres_socket(connection), 1, 0,
                                  deadline);
  cancel = cpkt_postgres_cancel_create(connection);
  if (cancel == NULL)
    return 0;
  error[0] = '\0';
  ok = cpkt_postgres_cancel_request(cancel, error, sizeof(error));
  cpkt_postgres_cancel_free(cancel);
  result = cpkt_postgres_get_result(connection);
  if (!ok || result == NULL ||
      cpkt_postgres_result_status_get(result) !=
          CPKT_POSTGRES_RESULT_FATAL_ERROR ||
      cpkt_postgres_result_error_field(result, CPKT_POSTGRES_DIAG_SQLSTATE) ==
          NULL ||
      strcmp(
          cpkt_postgres_result_error_field(result, CPKT_POSTGRES_DIAG_SQLSTATE),
          "57014") != 0)
    ok = 0;
  cpkt_postgres_result_free(result);
  while ((result = cpkt_postgres_get_result(connection)) != NULL)
    cpkt_postgres_result_free(result);
  cpkt_postgres_reset(connection);
  result = cpkt_postgres_execute(connection, "SELECT 19");
  if (!require_value(result, "19"))
    ok = 0;
  cpkt_postgres_result_free(result);
  return ok;
}

static int run_integration(const char *server_name,
                           const char *connection_info) {
  const char *parameters[2];
  cpkt_postgres *pg;
  cpkt_postgres_result *result;
  cpkt_postgres_result *notice_copy;
  live_events events;
  /* Outlive the connection even if notice unregistration fails. */
  live_notice notice;
  int ok;

  pg = cpkt_postgres_new(connection_info);
  if (pg == 0 || pg->status(pg) != CPKT_POSTGRES_CONNECTION_OK) {
    fprintf(stderr, "%s: connection failed: %s\n", server_name,
            pg == 0 ? "allocation failed" : pg->error(pg));
    if (pg != 0) {
      pg->close(pg);
    }
    return 0;
  }

  ok = 1;
  notice_copy = NULL;
  memset(&events, 0, sizeof(events));
  if (cpkt_postgres_event_register(pg->connection, live_event, "live-e2e",
                                   &events) == NULL)
    ok = 0;
  if (!shared_metadata_and_errors(pg->connection) ||
      !protocol_trace_smoke(pg->connection) || !checked_result_retrieval(pg) ||
      !shared_single_rows(pg->connection) ||
      !shared_chunked_rows(pg->connection) ||
      !shared_escaping(pg->connection) || !shared_nonblocking(pg->connection))
    ok = 0;
  if (!ok)
    fprintf(stderr, "%s: initial integration stage failed\n", server_name);
  result = pg->tx(pg, "SELECT 1");
  if (!require_value(result, "1")) {
    ok = 0;
  }
  cpkt_postgres_result_free(result);

  parameters[0] = "cpkt";
  parameters[1] = "-facade";
  result = pg->tx_params(pg, "SELECT $1::TEXT || $2::TEXT", 2, 0, parameters, 0,
                         0, 0);
  if (!require_value(result, "cpkt-facade")) {
    ok = 0;
  }
  cpkt_postgres_result_free(result);

  if (!pg->send(pg, "SELECT 9223372036854775807::INT8")) {
    ok = 0;
  } else {
    result = pg->receive(pg);
    if (!require_value(result, "9223372036854775807")) {
      ok = 0;
    }
    cpkt_postgres_result_free(result);
  }

  result = cpkt_postgres_prepare(pg->connection, "cpkt_receiver_statement",
                                 "SELECT $1::INT4 + 1", 1, 0);
  if (!require_command(result)) {
    ok = 0;
  }
  cpkt_postgres_result_free(result);
  parameters[0] = "41";
  result = cpkt_postgres_execute_prepared(
      pg->connection, "cpkt_receiver_statement", 1, parameters, 0, 0, 0);
  if (!require_value(result, "42")) {
    ok = 0;
  }
  cpkt_postgres_result_free(result);

  result = pg->tx(pg, "BEGIN");
  if (!require_command(result) ||
      cpkt_postgres_transaction_status_get(pg->connection) !=
          CPKT_POSTGRES_TRANSACTION_IN_TRANSACTION) {
    ok = 0;
  }
  cpkt_postgres_result_free(result);
  result = pg->tx(pg, "ROLLBACK");
  if (!require_command(result) ||
      cpkt_postgres_transaction_status_get(pg->connection) !=
          CPKT_POSTGRES_TRANSACTION_IDLE) {
    ok = 0;
  }
  cpkt_postgres_result_free(result);

  if (strcmp(server_name, "postgresql") == 0) {
    const char *extended_failure;
    extended_failure = NULL;
    if (!postgres_pipeline(pg->connection))
      extended_failure = "pipeline";
    else if (!postgres_copy(pg->connection))
      extended_failure = "copy";
    else if (!postgres_large_object(pg->connection))
      extended_failure = "large object";
    else if (!postgres_notice_and_notify(pg->connection, &notice_copy, &notice))
      extended_failure = "notice/notify";
    else if (!postgres_cancel_and_reset(pg->connection))
      extended_failure = "cancel/reset";
    if (extended_failure != NULL) {
      fprintf(stderr, "%s: extended integration failure: %s\n", server_name,
              extended_failure);
      ok = 0;
    }
  }

  pg->close(pg);
  if (notice_copy != NULL) {
    if (!cpkt_postgres_event_fire_result_create(NULL, notice_copy) ||
        cpkt_postgres_event_result_data(notice_copy, events.identity) !=
            &events)
      ok = 0;
    cpkt_postgres_result_free(notice_copy);
  }
  if (events.registrations != 1 || events.results < 5 ||
      events.destroyed != events.results || events.failures != 0)
    ok = 0;
  if (!ok)
    fprintf(stderr,
            "%s: event counts registrations=%d results=%d destroyed=%d "
            "failures=%d\n",
            server_name, events.registrations, events.results, events.destroyed,
            events.failures);
  if (!ok) {
    fprintf(stderr, "%s: PostgreSQL-wire integration assertion failed\n",
            server_name);
  }
  return ok;
}

int main(int argc, char **argv) {
  const char *connection_variable;
  const char *connection_info;
  if (argc != 2) {
    fprintf(stderr, "usage: %s {postgresql|cockroachdb}\n", argv[0]);
    return 2;
  }
  if (strcmp(argv[1], "postgresql") == 0)
    connection_variable = "CPKT_POSTGRES_E2E_CONNINFO";
  else if (strcmp(argv[1], "cockroachdb") == 0)
    connection_variable = "CPKT_COCKROACH_E2E_CONNINFO";
  else {
    fprintf(stderr, "unknown database server: %s\n", argv[1]);
    return 2;
  }
  connection_info = getenv(connection_variable);
  if (connection_info == NULL || connection_info[0] == '\0') {
    fprintf(stderr, "%s is required\n", connection_variable);
    return 2;
  }
  return run_integration(argv[1], connection_info) ? 0 : 1;
}
