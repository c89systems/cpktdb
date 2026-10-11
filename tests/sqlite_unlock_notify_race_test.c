#include <cpkt/sqlite.h>

#include <pthread.h>

static pthread_mutex_t pause_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t pause_condition = PTHREAD_COND_INITIALIZER;
static int pause_registration;
static int registration_entered;
static int resume_registration;
static int old_calls;
static int new_calls;
static int replacement_status;

int __real_sqlite3_unlock_notify(void *database, void (*callback)(void **, int),
                                 void *context);

int __wrap_sqlite3_unlock_notify(void *database, void (*callback)(void **, int),
                                 void *context) {
  pthread_mutex_lock(&pause_mutex);
  if (pause_registration) {
    registration_entered = 1;
    pthread_cond_signal(&pause_condition);
    while (!resume_registration)
      pthread_cond_wait(&pause_condition, &pause_mutex);
  }
  pthread_mutex_unlock(&pause_mutex);
  return __real_sqlite3_unlock_notify(database, callback, context);
}

static void old_callback(void *context, int count, void *const *contexts) {
  (void)context;
  if (count == 1 && contexts != NULL && contexts[0] == NULL)
    ++old_calls;
}

static void new_callback(void *context, int count, void *const *contexts) {
  (void)context;
  if (count == 1 && contexts != NULL && contexts[0] == NULL)
    ++new_calls;
}

static void *replace_notification(void *context) {
  replacement_status =
      cpkt_sqlite_unlock_notify((cpkt_sqlite *)context, new_callback, NULL);
  return NULL;
}

int main(void) {
  cpkt_sqlite *blocker;
  cpkt_sqlite *blocked;
  pthread_t replacement;
  int flags;
  flags = CPKT_SQLITE_OPEN_READWRITE | CPKT_SQLITE_OPEN_CREATE |
          CPKT_SQLITE_OPEN_URI;
  blocker = cpkt_sqlite_open("file:cpkt-notify-race?mode=memory&cache=shared",
                             flags, NULL);
  blocked = cpkt_sqlite_open("file:cpkt-notify-race?mode=memory&cache=shared",
                             flags, NULL);
  if (blocker == NULL || blocked == NULL ||
      blocker->tx(blocker, "begin immediate; create table item(value);", NULL,
                  NULL) != CPKT_SQLITE_OK ||
      blocked->tx(blocked, "begin immediate", NULL, NULL) !=
          CPKT_SQLITE_LOCKED ||
      blocked->unlock_notify(blocked, old_callback, NULL) != CPKT_SQLITE_OK)
    return 1;
  pthread_mutex_lock(&pause_mutex);
  pause_registration = 1;
  pthread_mutex_unlock(&pause_mutex);
  if (pthread_create(&replacement, NULL, replace_notification, blocked) != 0)
    return 2;
  pthread_mutex_lock(&pause_mutex);
  while (!registration_entered)
    pthread_cond_wait(&pause_condition, &pause_mutex);
  pthread_mutex_unlock(&pause_mutex);
  if (blocker->tx(blocker, "commit", NULL, NULL) != CPKT_SQLITE_OK)
    return 3;
  pthread_mutex_lock(&pause_mutex);
  resume_registration = 1;
  pthread_cond_signal(&pause_condition);
  pthread_mutex_unlock(&pause_mutex);
  pthread_join(replacement, NULL);
  if (old_calls != 1 || new_calls != 1 || replacement_status != CPKT_SQLITE_OK)
    return 4;
  blocked->close(blocked);
  blocker->close(blocker);
  return 0;
}
