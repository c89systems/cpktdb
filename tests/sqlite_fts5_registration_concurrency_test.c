#include <cpkt/sqlite.h>

#include <pthread.h>
#include <stdio.h>

#define WORKER_COUNT 8
#define REGISTRATIONS_PER_WORKER 500

typedef struct registration_worker {
  cpkt_sqlite_fts5_api *api;
  int index;
  int status;
} registration_worker;

static pthread_mutex_t start_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t start_condition = PTHREAD_COND_INITIALIZER;
static int start_workers;
static int auxiliary_destroyed;
static int tokenizer_destroyed;

static void destroy_binding(void *context) { ++*(int *)context; }

static void auxiliary(cpkt_sqlite_fts5_context *fts_context,
                      cpkt_sqlite_context *sql_context, int argument_count,
                      cpkt_sqlite_value *const *arguments, void *context) {
  (void)fts_context;
  (void)sql_context;
  (void)argument_count;
  (void)arguments;
  (void)context;
}

static int create_tokenizer(void *context, const char *const *arguments,
                            int argument_count,
                            cpkt_sqlite_fts5_tokenizer **out) {
  (void)context;
  (void)arguments;
  (void)argument_count;
  *out = cpkt_sqlite_fts5_tokenizer_new(NULL);
  return *out == NULL ? CPKT_SQLITE_NOMEM : CPKT_SQLITE_OK;
}

static void destroy_tokenizer(cpkt_sqlite_fts5_tokenizer *tokenizer,
                              void *context) {
  (void)tokenizer;
  (void)context;
}

static int tokenize(cpkt_sqlite_fts5_tokenizer *tokenizer, void *context,
                    int flags, const char *text, int text_byte_count,
                    const char *locale, int locale_byte_count,
                    cpkt_sqlite_fts5_token_callback callback) {
  (void)tokenizer;
  (void)context;
  (void)flags;
  (void)text;
  (void)text_byte_count;
  (void)locale;
  (void)locale_byte_count;
  (void)callback;
  return CPKT_SQLITE_OK;
}

static void *register_fts5(void *argument) {
  registration_worker *worker;
  char name[32];
  int index;
  worker = (registration_worker *)argument;
  pthread_mutex_lock(&start_mutex);
  while (!start_workers)
    pthread_cond_wait(&start_condition, &start_mutex);
  pthread_mutex_unlock(&start_mutex);
  worker->status = CPKT_SQLITE_OK;
  for (index = 0; index < REGISTRATIONS_PER_WORKER; ++index) {
    sprintf(name, "aux_%d_%d", worker->index, index);
    worker->status = worker->api->create_auxiliary(
        worker->api, name, &auxiliary_destroyed, auxiliary, destroy_binding);
    if (worker->status != CPKT_SQLITE_OK)
      return NULL;
    sprintf(name, "tok_%d_%d", worker->index, index);
    worker->status = worker->api->create_tokenizer(
        worker->api, name, &tokenizer_destroyed, create_tokenizer,
        destroy_tokenizer, tokenize, destroy_binding);
    if (worker->status != CPKT_SQLITE_OK)
      return NULL;
  }
  return NULL;
}

int main(void) {
  cpkt_sqlite *database;
  cpkt_sqlite_fts5_api *api;
  registration_worker workers[WORKER_COUNT];
  pthread_t threads[WORKER_COUNT];
  int index;
  database = cpkt_sqlite_new(":memory:");
  if (database == NULL || database->error_code(database) != CPKT_SQLITE_OK ||
      cpkt_sqlite_fts5_api_open(database, &api) != CPKT_SQLITE_OK)
    return 1;
  for (index = 0; index < WORKER_COUNT; ++index) {
    workers[index].api = api;
    workers[index].index = index;
    if (pthread_create(&threads[index], NULL, register_fts5, &workers[index]) !=
        0)
      return 2;
  }
  pthread_mutex_lock(&start_mutex);
  start_workers = 1;
  pthread_cond_broadcast(&start_condition);
  pthread_mutex_unlock(&start_mutex);
  for (index = 0; index < WORKER_COUNT; ++index)
    pthread_join(threads[index], NULL);
  for (index = 0; index < WORKER_COUNT; ++index) {
    if (workers[index].status != CPKT_SQLITE_OK)
      return 3;
  }
  api->close(api);
  database->close(database);
  if (auxiliary_destroyed != WORKER_COUNT * REGISTRATIONS_PER_WORKER ||
      tokenizer_destroyed != WORKER_COUNT * REGISTRATIONS_PER_WORKER)
    return 4;
  return 0;
}
