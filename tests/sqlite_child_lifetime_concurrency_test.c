#include <cpkt/sqlite.h>

#include <pthread.h>
#include <string.h>

#define WORKERS 16
#define ROUNDS 32

typedef struct child_test_state {
  cpkt_sqlite *database;
  pthread_mutex_t mutex;
  pthread_cond_t condition;
  int start;
  int finish;
  int prepared;
  int failures;
} child_test_state;

static void child_test_scalar(cpkt_sqlite_context *context, int count,
                              cpkt_sqlite_value *const *arguments,
                              void *user_data) {
  (void)count;
  (void)arguments;
  (void)user_data;
  cpkt_sqlite_context_result_int(context, 1);
}

static void child_test_destroy(void *context) {
  int *count;
  count = (int *)context;
  ++*count;
}

static void *child_test_worker(void *context) {
  child_test_state *state;
  cpkt_sqlite_statement *statement;
  int status;
  state = (child_test_state *)context;
  pthread_mutex_lock(&state->mutex);
  while (!state->start)
    pthread_cond_wait(&state->condition, &state->mutex);
  pthread_mutex_unlock(&state->mutex);

  statement = NULL;
  status = cpkt_sqlite_prepare(state->database, "select lifetime_test()", -1, 0,
                               &statement, NULL);
  pthread_mutex_lock(&state->mutex);
  if (status != CPKT_SQLITE_OK || statement == NULL)
    ++state->failures;
  ++state->prepared;
  pthread_cond_broadcast(&state->condition);
  while (!state->finish)
    pthread_cond_wait(&state->condition, &state->mutex);
  pthread_mutex_unlock(&state->mutex);

  if (statement != NULL && statement->finalize(statement) != CPKT_SQLITE_OK) {
    pthread_mutex_lock(&state->mutex);
    ++state->failures;
    pthread_mutex_unlock(&state->mutex);
  }
  return NULL;
}

int main(void) {
  child_test_state state;
  pthread_t threads[WORKERS];
  int destroyed;
  int created;
  int index;
  int round;
  for (round = 0; round < ROUNDS; ++round) {
    memset(&state, 0, sizeof(state));
    destroyed = 0;
    if (pthread_mutex_init(&state.mutex, NULL) != 0 ||
        pthread_cond_init(&state.condition, NULL) != 0)
      return 1;
    state.database = cpkt_sqlite_new(":memory:");
    if (state.database == NULL ||
        cpkt_sqlite_create_function(state.database, "lifetime_test", 0,
                                    CPKT_SQLITE_UTF8, &destroyed,
                                    child_test_scalar, NULL, NULL,
                                    child_test_destroy) != CPKT_SQLITE_OK)
      return 2;
    created = 0;
    for (index = 0; index < WORKERS; ++index) {
      if (pthread_create(&threads[index], NULL, child_test_worker, &state) != 0)
        break;
      ++created;
    }
    pthread_mutex_lock(&state.mutex);
    state.start = 1;
    pthread_cond_broadcast(&state.condition);
    while (state.prepared < created)
      pthread_cond_wait(&state.condition, &state.mutex);
    pthread_mutex_unlock(&state.mutex);

    cpkt_sqlite_close(state.database);
    pthread_mutex_lock(&state.mutex);
    state.finish = 1;
    pthread_cond_broadcast(&state.condition);
    pthread_mutex_unlock(&state.mutex);
    for (index = 0; index < created; ++index)
      pthread_join(threads[index], NULL);
    pthread_cond_destroy(&state.condition);
    pthread_mutex_destroy(&state.mutex);
    if (created != WORKERS || state.failures != 0 || destroyed != 1)
      return 3;
  }
  return 0;
}
