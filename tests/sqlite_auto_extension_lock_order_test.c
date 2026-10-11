#define _POSIX_C_SOURCE 200809L
#include <cpkt/sqlite.h>

#include <pthread.h>
#include <time.h>

typedef struct extension_operation {
  cpkt_sqlite_auto_extension *extension;
  int cancel;
  int status;
} extension_operation;

static pthread_mutex_t handshake_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t handshake_condition = PTHREAD_COND_INITIALIZER;
static int native_call_entered;
static int notification_count;

int __real_sqlite3_auto_extension(void (*entry)(void));
int __real_sqlite3_cancel_auto_extension(void (*entry)(void));

static void pause_native_call(void) {
  struct timespec delay;
  pthread_mutex_lock(&handshake_mutex);
  native_call_entered = 1;
  pthread_cond_signal(&handshake_condition);
  pthread_mutex_unlock(&handshake_mutex);
  delay.tv_sec = 0;
  delay.tv_nsec = 300000000;
  (void)nanosleep(&delay, NULL);
}

int __wrap_sqlite3_auto_extension(void (*entry)(void)) {
  pause_native_call();
  return __real_sqlite3_auto_extension(entry);
}

int __wrap_sqlite3_cancel_auto_extension(void (*entry)(void)) {
  pause_native_call();
  return __real_sqlite3_cancel_auto_extension(entry);
}

static int initialize_extension(cpkt_sqlite *database, char **error_out,
                                void *context) {
  (void)database;
  (void)error_out;
  (void)context;
  return CPKT_SQLITE_OK;
}

static void notify(void *context, int count, void *const *contexts) {
  (void)context;
  (void)count;
  (void)contexts;
  ++notification_count;
}

static void *run_extension_operation(void *argument) {
  extension_operation *operation;
  operation = (extension_operation *)argument;
  if (operation->cancel)
    operation->status = operation->extension->cancel(operation->extension);
  else
    operation->status =
        operation->extension->register_extension(operation->extension);
  return NULL;
}

static int run_lock_order_phase(cpkt_sqlite *writer, cpkt_sqlite *blocked,
                                extension_operation *operation,
                                const char *write_sql) {
  pthread_t thread;
  int status;
  if (writer->tx(writer, write_sql, NULL, NULL) != CPKT_SQLITE_OK ||
      blocked->tx(blocked, "begin immediate", NULL, NULL) !=
          CPKT_SQLITE_LOCKED ||
      blocked->unlock_notify(blocked, notify, NULL) != CPKT_SQLITE_OK)
    return 0;
  pthread_mutex_lock(&handshake_mutex);
  native_call_entered = 0;
  pthread_mutex_unlock(&handshake_mutex);
  if (pthread_create(&thread, NULL, run_extension_operation, operation) != 0)
    return 0;
  pthread_mutex_lock(&handshake_mutex);
  while (!native_call_entered)
    pthread_cond_wait(&handshake_condition, &handshake_mutex);
  pthread_mutex_unlock(&handshake_mutex);
  status = writer->tx(writer, "commit", NULL, NULL);
  pthread_join(thread, NULL);
  return status == CPKT_SQLITE_OK &&
         operation->status == (operation->cancel ? 1 : CPKT_SQLITE_OK);
}

int main(void) {
  cpkt_sqlite *writer;
  cpkt_sqlite *blocked;
  extension_operation operation;
  int flags;
  flags = CPKT_SQLITE_OPEN_READWRITE | CPKT_SQLITE_OPEN_CREATE |
          CPKT_SQLITE_OPEN_URI;
  writer = cpkt_sqlite_open("file:cpkt-lock-order?mode=memory&cache=shared",
                            flags, NULL);
  blocked = cpkt_sqlite_open("file:cpkt-lock-order?mode=memory&cache=shared",
                             flags, NULL);
  operation.extension =
      cpkt_sqlite_auto_extension_new(initialize_extension, NULL);
  if (writer == NULL || blocked == NULL || operation.extension == NULL)
    return 1;
  operation.cancel = 0;
  if (!run_lock_order_phase(writer, blocked, &operation,
                            "begin immediate; create table x(v)"))
    return 2;
  operation.cancel = 1;
  if (!run_lock_order_phase(writer, blocked, &operation,
                            "begin immediate; insert into x values(1)"))
    return 3;
  if (notification_count != 2)
    return 4;
  operation.extension->close(operation.extension);
  blocked->close(blocked);
  writer->close(writer);
  return 0;
}
