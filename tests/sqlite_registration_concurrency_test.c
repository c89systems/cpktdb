#include <cpkt/sqlite.h>

#include <pthread.h>

#define REGISTRATION_COUNT 256

typedef struct registration_worker {
  cpkt_sqlite *database;
  char suffix;
  int result_value;
  int status;
} registration_worker;

static void scalar_result(cpkt_sqlite_context *context, int argument_count,
                          cpkt_sqlite_value *const *arguments,
                          void *user_data) {
  (void)argument_count;
  (void)arguments;
  cpkt_sqlite_context_result_int(context, *(int *)user_data);
}

static int geometry_result(void *context, cpkt_sqlite_rtree_geometry *geometry,
                           int coordinate_count, double *coordinates,
                           int *within_out) {
  (void)context;
  (void)geometry;
  (void)coordinate_count;
  (void)coordinates;
  *within_out = 1;
  return CPKT_SQLITE_OK;
}

static void *register_callbacks(void *context) {
  registration_worker *worker;
  char name[16];
  unsigned short wide_name[16];
  int index;
  int digit;

  worker = (registration_worker *)context;
  worker->status = CPKT_SQLITE_OK;
  for (index = 0; index < REGISTRATION_COUNT; ++index) {
    wide_name[0] = 'f';
    wide_name[1] = (unsigned short)worker->suffix;
    wide_name[2] = (unsigned short)('0' + index / 100);
    wide_name[3] = (unsigned short)('0' + (index / 10) % 10);
    wide_name[4] = (unsigned short)('0' + index % 10);
    wide_name[5] = 0;
    worker->status = cpkt_sqlite_create_function16(
        worker->database, wide_name, 0, CPKT_SQLITE_UTF16,
        &worker->result_value, scalar_result, NULL, NULL);
    if (worker->status != CPKT_SQLITE_OK)
      return NULL;
    name[0] = 'g';
    name[1] = worker->suffix;
    for (digit = 0; digit < 3; ++digit)
      name[2 + digit] = (char)wide_name[2 + digit];
    name[5] = '\0';
    worker->status = cpkt_sqlite_register_rtree_geometry(worker->database, name,
                                                         geometry_result, NULL);
    if (worker->status != CPKT_SQLITE_OK)
      return NULL;
  }
  return NULL;
}

static int check_function(cpkt_sqlite *database, char suffix) {
  char query[] = "select fA255()";
  cpkt_sqlite_statement *statement;
  int ok;
  query[8] = suffix;
  statement = NULL;
  if (database->prepare(database, query, -1, 0, &statement, NULL) !=
          CPKT_SQLITE_OK ||
      statement == NULL)
    return 0;
  ok = statement->step(statement) == CPKT_SQLITE_ROW &&
       statement->column_int(statement, 0) == REGISTRATION_COUNT - 1;
  return statement->finalize(statement) == CPKT_SQLITE_OK && ok;
}

static int check_geometry(cpkt_sqlite *database, char suffix) {
  char query[] = "select id from boxes where min_x match gA255(5, 5)";
  cpkt_sqlite_statement *statement;
  int ok;
  query[40] = suffix;
  statement = NULL;
  if (database->prepare(database, query, -1, 0, &statement, NULL) !=
          CPKT_SQLITE_OK ||
      statement == NULL)
    return 0;
  ok = statement->step(statement) == CPKT_SQLITE_ROW &&
       statement->column_int(statement, 0) == 1;
  return statement->finalize(statement) == CPKT_SQLITE_OK && ok;
}

int main(void) {
  cpkt_sqlite *database;
  registration_worker first;
  registration_worker second;
  pthread_t first_thread;
  pthread_t second_thread;
  int status;

  database = cpkt_sqlite_new(":memory:");
  if (database == NULL || database->error_code(database) != CPKT_SQLITE_OK)
    return 1;
  status = database->tx(
      database,
      "create virtual table boxes using rtree(id, min_x, max_x, min_y, max_y);"
      "insert into boxes values(1, 0, 10, 0, 10);",
      NULL, NULL);
  if (status != CPKT_SQLITE_OK)
    return 2;
  first.database = database;
  first.suffix = 'A';
  first.result_value = REGISTRATION_COUNT - 1;
  second.database = database;
  second.suffix = 'B';
  second.result_value = REGISTRATION_COUNT - 1;
  if (pthread_create(&first_thread, NULL, register_callbacks, &first) != 0)
    return 3;
  if (pthread_create(&second_thread, NULL, register_callbacks, &second) != 0) {
    pthread_join(first_thread, NULL);
    return 4;
  }
  pthread_join(first_thread, NULL);
  pthread_join(second_thread, NULL);
  if (first.status != CPKT_SQLITE_OK || second.status != CPKT_SQLITE_OK ||
      !check_function(database, 'A') || !check_function(database, 'B') ||
      !check_geometry(database, 'A') || !check_geometry(database, 'B'))
    return 5;
  database->close(database);
  return 0;
}
