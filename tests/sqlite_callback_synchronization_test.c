#define _DEFAULT_SOURCE 1

#include <cpkt/sqlite.h>

#include <pthread.h>
#include <unistd.h>

static pthread_mutex_t start_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t start_condition = PTHREAD_COND_INITIALIZER;
static int setter_started;
static int setter_status;
static int old_context;
static int new_context;
static int old_calls;
static int new_calls;
static int mismatched_context;

static int old_authorizer(void *context, int action, const char *detail1,
                          const char *detail2, const char *database,
                          const char *trigger) {
  (void)action;
  (void)detail1;
  (void)detail2;
  (void)database;
  (void)trigger;
  if (context != &old_context)
    ++mismatched_context;
  ++old_calls;
  return CPKT_SQLITE_OK;
}

static int new_authorizer(void *context, int action, const char *detail1,
                          const char *detail2, const char *database,
                          const char *trigger) {
  (void)action;
  (void)detail1;
  (void)detail2;
  (void)database;
  (void)trigger;
  if (context != &new_context)
    ++mismatched_context;
  ++new_calls;
  return CPKT_SQLITE_OK;
}

static void *replace_authorizer(void *context) {
  cpkt_sqlite *database;
  database = (cpkt_sqlite *)context;
  pthread_mutex_lock(&start_mutex);
  setter_started = 1;
  pthread_cond_signal(&start_condition);
  pthread_mutex_unlock(&start_mutex);
  setter_status =
      cpkt_sqlite_set_authorizer(database, new_authorizer, &new_context);
  return NULL;
}

static int prepare_and_finalize(cpkt_sqlite *database, const char *sql) {
  cpkt_sqlite_statement *statement;
  int status;
  statement = NULL;
  status = cpkt_sqlite_prepare(database, sql, -1, 0, &statement, NULL);
  if (status != CPKT_SQLITE_OK || statement == NULL)
    return 0;
  return statement->finalize(statement) == CPKT_SQLITE_OK;
}

int main(void) {
  cpkt_sqlite_mutex *application_mutex;
  cpkt_sqlite_mutex *database_mutex;
  cpkt_sqlite *database;
  pthread_t setter;
  int original_calls;

  application_mutex =
      cpkt_sqlite_mutex_new(CPKT_SQLITE_MUTEX_STATIC_APPLICATION_1);
  if (application_mutex == NULL)
    return 1;
  application_mutex->enter(application_mutex);
  database = cpkt_sqlite_new(":memory:");
  application_mutex->leave(application_mutex);
  application_mutex->close(application_mutex);
  if (database == NULL || database->error_code(database) != CPKT_SQLITE_OK)
    return 2;

  database_mutex = cpkt_sqlite_database_mutex(database);
  if (database_mutex == NULL ||
      cpkt_sqlite_set_authorizer(database, old_authorizer, &old_context) !=
          CPKT_SQLITE_OK)
    return 3;
  database_mutex->enter(database_mutex);
  if (pthread_create(&setter, NULL, replace_authorizer, database) != 0) {
    database_mutex->leave(database_mutex);
    return 4;
  }
  pthread_mutex_lock(&start_mutex);
  while (!setter_started)
    pthread_cond_wait(&start_condition, &start_mutex);
  pthread_mutex_unlock(&start_mutex);
  usleep(100000);
  if (!prepare_and_finalize(database, "select 1")) {
    database_mutex->leave(database_mutex);
    pthread_join(setter, NULL);
    return 5;
  }
  original_calls = old_calls;
  database_mutex->leave(database_mutex);
  pthread_join(setter, NULL);
  if (original_calls == 0 || new_calls != 0 || mismatched_context != 0 ||
      setter_status != CPKT_SQLITE_OK)
    return 6;
  if (!prepare_and_finalize(database, "select 2") || new_calls == 0 ||
      mismatched_context != 0)
    return 7;
  database_mutex->close(database_mutex);
  database->close(database);
  return 0;
}
