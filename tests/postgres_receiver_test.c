#include <cpkt/postgres.h>

static void notice_receiver(void *context, cpkt_postgres_connection *connection,
                            const cpkt_postgres_result *result) {
  (void)context;
  (void)connection;
  (void)result;
}

static void notice_processor(void *context,
                             cpkt_postgres_connection *connection,
                             const char *message) {
  (void)context;
  (void)connection;
  (void)message;
}

static cpkt_postgres_connection *observed_connection;
static cpkt_postgres_result *callback_notice_copy;

static void copying_receiver(void *context,
                             cpkt_postgres_connection *connection,
                             const cpkt_postgres_result *result) {
  ++*(int *)context;
  observed_connection = connection;
  if (callback_notice_copy == 0)
    callback_notice_copy = cpkt_postgres_result_copy(
        result, CPKT_POSTGRES_COPY_RESULT_NOTICE_HOOKS);
}

static void counted_receiver(void *context,
                             cpkt_postgres_connection *connection,
                             const cpkt_postgres_result *result) {
  ++*(int *)context;
  observed_connection = connection;
  (void)result;
}

static void counted_processor(void *context,
                              cpkt_postgres_connection *connection,
                              const char *message) {
  ++*(int *)context;
  observed_connection = connection;
  (void)message;
}

static void thread_lock(int acquire) { (void)acquire; }

static int ssl_key_password(char *buffer, int buffer_size,
                            cpkt_postgres_connection *connection) {
  (void)buffer;
  (void)buffer_size;
  (void)connection;
  return 0;
}

static int auth_data(cpkt_postgres_auth_data_kind kind,
                     cpkt_postgres_connection *connection, void *data,
                     void *context) {
  (void)kind;
  (void)connection;
  (void)data;
  (void)context;
  return 0;
}

int main(void) {
  cpkt_postgres *pg;
  cpkt_postgres_notice_receiver old_receiver;
  cpkt_postgres_notice_processor old_processor;
  cpkt_postgres_auth_data_hook auth_callback;
  void *old_context;
  cpkt_postgres_result *result;
  cpkt_postgres_result *copy;
  int first_count;
  int second_count;

  pg = cpkt_postgres_new("host=/tmp/cpkt-postgres-no-socket connect_timeout=1");
  if (pg == 0 || pg->tx == 0 || pg->send == 0 || pg->receive == 0 ||
      pg->reset == 0 || pg->close == 0) {
    return 1;
  }
  if (pg->status(pg) != CPKT_POSTGRES_CONNECTION_BAD) {
    pg->close(pg);
    return 2;
  }
  old_context = (void *)1;
  if (!cpkt_postgres_set_notice_receiver(pg->connection, notice_receiver, pg,
                                         &old_receiver, &old_context))
    return 90;
  if (old_receiver != 0 || old_context != 0) {
    pg->close(pg);
    return 3;
  }
  if (!cpkt_postgres_set_notice_processor(pg->connection, notice_processor, pg,
                                          &old_processor, &old_context))
    return 90;
  if (old_processor != 0 || old_context != 0) {
    pg->close(pg);
    return 4;
  }
  if (cpkt_postgres_register_thread_lock(thread_lock) != 0) {
    pg->close(pg);
    return 5;
  }
  if (cpkt_postgres_register_thread_lock(0) != thread_lock) {
    pg->close(pg);
    return 6;
  }
  if (cpkt_postgres_set_ssl_key_password_hook(ssl_key_password) != 0 ||
      cpkt_postgres_get_ssl_key_password_hook() != ssl_key_password) {
    pg->close(pg);
    return 7;
  }
  if (cpkt_postgres_set_ssl_key_password_hook(0) != ssl_key_password ||
      cpkt_postgres_get_ssl_key_password_hook() != 0) {
    pg->close(pg);
    return 8;
  }
  cpkt_postgres_set_auth_data_hook(auth_data, pg);
  old_context = 0;
  auth_callback = cpkt_postgres_get_auth_data_hook(&old_context);
  if (auth_callback != auth_data || old_context != pg) {
    pg->close(pg);
    return 9;
  }
  cpkt_postgres_set_auth_data_hook(0, 0);
  first_count = 0;
  second_count = 0;
  if (!cpkt_postgres_set_notice_receiver(pg->connection, counted_receiver,
                                         &first_count, 0, 0))
    return 90;
  result = cpkt_postgres_result_new_empty(pg->connection,
                                          CPKT_POSTGRES_RESULT_TUPLES_OK);
  copy = result == 0 ? 0 : cpkt_postgres_result_copy(result, 0);
  if (result == 0 || copy == 0)
    return 10;
  if (!cpkt_postgres_set_notice_receiver(pg->connection, counted_receiver,
                                         &second_count, 0, 0))
    return 90;
  (void)cpkt_postgres_result_field_name(result, -1);
  if (first_count != 1 || second_count != 0 ||
      observed_connection != pg->connection)
    return 11;
  pg->close(pg);
  observed_connection = (cpkt_postgres_connection *)1;
  (void)cpkt_postgres_result_field_name(result, -1);
  if (first_count != 2 || second_count != 0 || observed_connection != 0)
    return 12;
  /* Libpq's PQcopyResult does not copy notice hooks. */
  (void)cpkt_postgres_result_field_name(copy, -1);
  if (first_count != 2 || second_count != 0)
    return 17;
  cpkt_postgres_result_free(result);
  cpkt_postgres_result_free(copy);

  pg = cpkt_postgres_new("host=/tmp/cpkt-postgres-no-socket connect_timeout=1");
  if (pg == 0)
    return 13;
  first_count = 0;
  second_count = 0;
  if (!cpkt_postgres_set_notice_processor(pg->connection, counted_processor,
                                          &first_count, 0, 0))
    return 90;
  result = cpkt_postgres_result_new_empty(pg->connection,
                                          CPKT_POSTGRES_RESULT_TUPLES_OK);
  if (result == 0)
    return 14;
  if (!cpkt_postgres_set_notice_processor(pg->connection, counted_processor,
                                          &second_count, 0, 0))
    return 90;
  (void)cpkt_postgres_result_field_name(result, -1);
  if (first_count != 1 || second_count != 0 ||
      observed_connection != pg->connection)
    return 15;
  pg->close(pg);
  observed_connection = (cpkt_postgres_connection *)1;
  (void)cpkt_postgres_result_field_name(result, -1);
  if (first_count != 2 || second_count != 0 || observed_connection != 0)
    return 16;
  cpkt_postgres_result_free(result);
  pg = cpkt_postgres_new("host=/tmp/cpkt-postgres-no-socket connect_timeout=1");
  if (pg == 0)
    return 18;
  first_count = 0;
  callback_notice_copy = 0;
  if (!cpkt_postgres_set_notice_receiver(pg->connection, copying_receiver,
                                         &first_count, 0, 0))
    return 90;
  result = cpkt_postgres_result_new_empty(pg->connection,
                                          CPKT_POSTGRES_RESULT_TUPLES_OK);
  if (result == 0)
    return 19;
  (void)cpkt_postgres_result_field_name(result, -1);
  if (first_count != 1 || callback_notice_copy == 0 ||
      observed_connection != pg->connection)
    return 20;
  pg->close(pg);
  cpkt_postgres_result_free(result);
  observed_connection = (cpkt_postgres_connection *)1;
  (void)cpkt_postgres_result_field_name(callback_notice_copy, -1);
  if (first_count != 2 || observed_connection != 0)
    return 21;
  copy = cpkt_postgres_result_copy(callback_notice_copy,
                                   CPKT_POSTGRES_COPY_RESULT_NOTICE_HOOKS);
  if (copy == 0)
    return 22;
  cpkt_postgres_result_free(callback_notice_copy);
  callback_notice_copy = copy;
  observed_connection = (cpkt_postgres_connection *)1;
  (void)cpkt_postgres_result_field_name(copy, -1);
  if (first_count != 3 || observed_connection != 0)
    return 23;
  cpkt_postgres_result_free(copy);
  return 0;
}
