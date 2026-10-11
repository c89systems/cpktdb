#include <cpkt/postgres.h>
#include <pthread.h>
#include <string.h>

typedef struct release_case {
  pthread_mutex_t mutex;
  pthread_cond_t condition;
  int waiting;
  int start;
  cpkt_postgres_connection *connection;
  cpkt_postgres_result *result;
  cpkt_postgres_event *event;
  int connection_destroyed;
  int result_destroyed;
  int errors;
} release_case;

static int on_event(cpkt_postgres_event_id id,
                    const cpkt_postgres_event_info *info, void *context) {
  release_case *test;
  test = (release_case *)context;
  (void)pthread_mutex_lock(&test->mutex);
  if (info == NULL || info->event == NULL) {
    ++test->errors;
    (void)pthread_mutex_unlock(&test->mutex);
    return 0;
  }
  if (id == CPKT_POSTGRES_EVENT_REGISTER) {
    test->event = info->event;
    (void)pthread_mutex_unlock(&test->mutex);
    return 1;
  }
  if (info->event != test->event)
    ++test->errors;
  if (id == CPKT_POSTGRES_EVENT_CONNECTION_DESTROY) {
    if (info->connection != test->connection)
      ++test->errors;
    ++test->connection_destroyed;
  } else if (id == CPKT_POSTGRES_EVENT_RESULT_DESTROY) {
    if (info->result != test->result)
      ++test->errors;
    ++test->result_destroyed;
  }
  (void)pthread_mutex_unlock(&test->mutex);
  return 1;
}

static void *release_worker(void *context) {
  release_case *test;
  int role;
  test = (release_case *)context;
  (void)pthread_mutex_lock(&test->mutex);
  role = ++test->waiting;
  (void)pthread_cond_broadcast(&test->condition);
  while (!test->start)
    (void)pthread_cond_wait(&test->condition, &test->mutex);
  (void)pthread_mutex_unlock(&test->mutex);
  if (role == 1)
    cpkt_postgres_connection_free(test->connection);
  else
    cpkt_postgres_result_free(test->result);
  return NULL;
}

int main(void) {
  release_case test;
  pthread_t first;
  pthread_t second;
  int round;
  for (round = 0; round < 256; ++round) {
    memset(&test, 0, sizeof(test));
    if (pthread_mutex_init(&test.mutex, NULL) != 0 ||
        pthread_cond_init(&test.condition, NULL) != 0)
      return 1;
    test.connection = cpkt_postgres_connect(
        "host=/tmp/cpkt-postgres-events-no-socket connect_timeout=1");
    if (test.connection == NULL ||
        cpkt_postgres_event_register(test.connection, on_event, "release",
                                     &test) == NULL)
      return 2;
    test.result = cpkt_postgres_result_new_empty(
        test.connection, CPKT_POSTGRES_RESULT_TUPLES_OK);
    if (test.result == NULL ||
        !cpkt_postgres_event_fire_result_create(test.connection, test.result))
      return 3;
    if (pthread_create(&first, NULL, release_worker, &test) != 0)
      return 4;
    if (pthread_create(&second, NULL, release_worker, &test) != 0) {
      (void)pthread_mutex_lock(&test.mutex);
      test.start = 1;
      (void)pthread_cond_broadcast(&test.condition);
      (void)pthread_mutex_unlock(&test.mutex);
      (void)pthread_join(first, NULL);
      return 5;
    }
    (void)pthread_mutex_lock(&test.mutex);
    while (test.waiting != 2)
      (void)pthread_cond_wait(&test.condition, &test.mutex);
    test.start = 1;
    (void)pthread_cond_broadcast(&test.condition);
    (void)pthread_mutex_unlock(&test.mutex);
    (void)pthread_join(first, NULL);
    (void)pthread_join(second, NULL);
    if (test.connection_destroyed != 1 || test.result_destroyed != 1 ||
        test.errors != 0)
      return 6;
    (void)pthread_cond_destroy(&test.condition);
    (void)pthread_mutex_destroy(&test.mutex);
  }
  return 0;
}
