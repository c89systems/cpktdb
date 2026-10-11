#include <cpkt/sqlite.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct mutex_counts {
  int initialize_count;
  int shutdown_count;
  int allocate_count;
  int free_count;
  int enter_count;
  int leave_count;
  int static_slots[32];
} mutex_counts;

static int log_length;
static int log_prefix_ok;

static int mutex_initialize(void *context) {
  ++((mutex_counts *)context)->initialize_count;
  return CPKT_SQLITE_OK;
}

static int mutex_shutdown(void *context) {
  ++((mutex_counts *)context)->shutdown_count;
  return CPKT_SQLITE_OK;
}

static void *mutex_allocate(void *context, int type) {
  mutex_counts *counts;
  counts = (mutex_counts *)context;
  ++counts->allocate_count;
  if (type >= CPKT_SQLITE_MUTEX_STATIC_MAIN && type < 32)
    return &counts->static_slots[type];
  return malloc(1);
}

static void mutex_free(void *context, void *mutex) {
  ++((mutex_counts *)context)->free_count;
  free(mutex);
}

static void mutex_enter(void *context, void *mutex) {
  (void)mutex;
  ++((mutex_counts *)context)->enter_count;
}

static int mutex_try(void *context, void *mutex) {
  mutex_enter(context, mutex);
  return CPKT_SQLITE_OK;
}

static void mutex_leave(void *context, void *mutex) {
  (void)mutex;
  ++((mutex_counts *)context)->leave_count;
}

static void log_callback(void *context, int code, const char *message) {
  (void)context;
  (void)code;
  log_length = message == NULL ? 0 : (int)strlen(message);
  log_prefix_ok = message != NULL && message[0] == 'x';
}

static int token_callback(void *context, int flags, const char *token,
                          int byte_count, int start, int end) {
  int *count;
  (void)flags;
  count = (int *)context;
  if (token == NULL || byte_count <= 0 || start < 0 || end <= start)
    return CPKT_SQLITE_ERROR;
  ++*count;
  return CPKT_SQLITE_OK;
}

int main(int argc, char **argv) {
  cpkt_sqlite_mutex_methods first;
  cpkt_sqlite_mutex_methods second;
  cpkt_sqlite_mutex_methods saved;
  cpkt_sqlite_mutex_methods restored;
  cpkt_sqlite *database;
  cpkt_sqlite_statement *statement;
  cpkt_sqlite_statement *modern_statement;
  cpkt_sqlite_fts5_api *fts;
  cpkt_sqlite_fts5_found_tokenizer *factory;
  cpkt_sqlite_fts5_tokenizer_instance *instance;
  mutex_counts counts[2];
  char long_message[2049];
  char temporary_directory[16];
  unsigned short utf16_sql[9];
  void *one_mutex;
  int token_count;
  int guard;
  int finalize_status;
  int legacy_status;
  int stage;
  if (argc != 2)
    return 2;
  memset(counts, 0, sizeof(counts));
  memset(&first, 0, sizeof(first));
  memset(&second, 0, sizeof(second));
  memset(&saved, 0, sizeof(saved));
  memset(&restored, 0, sizeof(restored));
  database = NULL;
  statement = NULL;
  modern_statement = NULL;
  fts = NULL;
  factory = NULL;
  instance = NULL;
  stage = 1;
  (void)remove(argv[1]);
  guard = 123456;
  if (cpkt_sqlite_global_config_none(CPKT_SQLITE_CONFIG_LOOKASIDE) !=
      CPKT_SQLITE_MISUSE)
    goto fail;
  stage = 101;
  if (cpkt_sqlite_global_config_int(CPKT_SQLITE_CONFIG_SINGLETHREAD, 1) !=
      CPKT_SQLITE_MISUSE)
    goto fail;
  stage = 102;
  if (cpkt_sqlite_global_config_two_int(CPKT_SQLITE_CONFIG_MEMSTATUS, 1, 2) !=
      CPKT_SQLITE_MISUSE)
    goto fail;
  stage = 103;
  if (cpkt_sqlite_global_config_pointer(CPKT_SQLITE_CONFIG_MUTEX, NULL) !=
      CPKT_SQLITE_MISUSE)
    goto fail;
  stage = 104;
  if (cpkt_sqlite_global_config_int_out(CPKT_SQLITE_CONFIG_PMASZ, &guard) !=
          CPKT_SQLITE_MISUSE ||
      guard != 123456)
    goto fail;
  stage = 105;
  if (cpkt_sqlite_global_config_int(CPKT_SQLITE_CONFIG_SORTERREF_SIZE, 0) !=
      CPKT_SQLITE_MISUSE)
    goto fail;
  stage = 11;
  memset(long_message, 'x', sizeof(long_message) - 1U);
  long_message[sizeof(long_message) - 1U] = '\0';
  if (cpkt_sqlite_global_config_log(log_callback, NULL) != CPKT_SQLITE_OK)
    goto fail;
  stage = 111;
  cpkt_sqlite_log(CPKT_SQLITE_NOTICE, "%s", long_message);
  /* sqlite3_log itself caps a record (699 bytes in this configured build). */
  if (!log_prefix_ok || log_length != 699)
    goto fail;
  stage = 12;
  strcpy(temporary_directory, "build");
  if (cpkt_sqlite_temp_directory_set(temporary_directory) != CPKT_SQLITE_OK ||
      cpkt_sqlite_temp_directory_get() == NULL ||
      strcmp(cpkt_sqlite_temp_directory_get(), "build") != 0)
    goto fail;
  temporary_directory[0] = 'X';
  if (strcmp(cpkt_sqlite_temp_directory_get(), "build") != 0 ||
      cpkt_sqlite_temp_directory_set(NULL) != CPKT_SQLITE_OK ||
      cpkt_sqlite_temp_directory_get() != NULL)
    goto fail;
  stage = 13;
  stage = 2;
  if (cpkt_sqlite_initialize() != CPKT_SQLITE_OK ||
      cpkt_sqlite_shutdown() != CPKT_SQLITE_OK ||
      cpkt_sqlite_global_config_mutex_methods_get(&saved) != CPKT_SQLITE_OK ||
      saved.allocate == NULL)
    goto fail;
  first.context = &counts[0];
  first.initialize = mutex_initialize;
  first.shutdown = mutex_shutdown;
  first.allocate = mutex_allocate;
  first.free = mutex_free;
  first.enter = mutex_enter;
  first.try_enter = mutex_try;
  first.leave = mutex_leave;
  second = first;
  second.context = &counts[1];
  if (cpkt_sqlite_global_config_mutex_methods_set(&first) != CPKT_SQLITE_OK ||
      cpkt_sqlite_global_config_mutex_methods_get(&restored) !=
          CPKT_SQLITE_OK ||
      restored.context != &counts[0] || restored.held != NULL ||
      restored.not_held != NULL ||
      cpkt_sqlite_global_config_mutex_methods_set(&second) != CPKT_SQLITE_OK)
    goto fail;
  one_mutex = restored.allocate(restored.context, CPKT_SQLITE_MUTEX_FAST);
  if (one_mutex == NULL)
    goto fail;
  restored.enter(restored.context, one_mutex);
  restored.leave(restored.context, one_mutex);
  restored.free(restored.context, one_mutex);
  if (counts[0].allocate_count != 1 || counts[1].allocate_count != 0 ||
      cpkt_sqlite_global_config_mutex_methods_get(&restored) !=
          CPKT_SQLITE_OK ||
      restored.context != &counts[1] ||
      cpkt_sqlite_initialize() != CPKT_SQLITE_OK)
    goto fail;
  stage = 3;
  database = cpkt_sqlite_open(
      argv[1], CPKT_SQLITE_OPEN_READWRITE | CPKT_SQLITE_OPEN_CREATE, NULL);
  if (database == NULL ||
      database->tx(database, "CREATE TABLE t(x); INSERT INTO t VALUES(7)", NULL,
                   NULL) != CPKT_SQLITE_OK ||
      database->tx(database, "INSERT INTO absent VALUES(1)", NULL, NULL) ==
          CPKT_SQLITE_OK ||
      database->tx(database, "INSERT INTO t VALUES(8)", NULL, NULL) !=
          CPKT_SQLITE_OK)
    goto fail;
  stage = 31;
  guard = 123456;
  if (cpkt_sqlite_database_config_int(database, CPKT_SQLITE_DBCONFIG_LOOKASIDE,
                                      0, &guard) != CPKT_SQLITE_MISUSE ||
      guard != 123456 ||
      cpkt_sqlite_virtual_table_config_int(database, CPKT_SQLITE_VTAB_INNOCUOUS,
                                           1) != CPKT_SQLITE_MISUSE ||
      cpkt_sqlite_virtual_table_config_none(
          database, CPKT_SQLITE_VTAB_CONSTRAINT_SUPPORT) !=
          CPKT_SQLITE_MISUSE ||
      cpkt_sqlite_file_control_none(database, "main",
                                    CPKT_SQLITE_FCNTL_FILE_POINTER) !=
          CPKT_SQLITE_MISUSE)
    goto fail;
  stage = 32;
  if (cpkt_sqlite_prepare_legacy(database, "SELECT x FROM t", -1, &statement,
                                 NULL) != CPKT_SQLITE_OK ||
      statement == NULL ||
      database->prepare(database, "SELECT x FROM t", -1, 0, &modern_statement,
                        NULL) != CPKT_SQLITE_OK ||
      modern_statement == NULL ||
      database->tx(database, "ALTER TABLE t ADD COLUMN y", NULL, NULL) !=
          CPKT_SQLITE_OK)
    goto fail;
  legacy_status = statement->step(statement);
  if (legacy_status != CPKT_SQLITE_ERROR ||
      statement->reset(statement) != CPKT_SQLITE_SCHEMA ||
      modern_statement->step(modern_statement) != CPKT_SQLITE_ROW) {
    fprintf(stderr, "legacy step status %d\n", legacy_status);
    goto fail;
  }
  (void)statement->finalize(statement);
  statement = NULL;
  (void)modern_statement->finalize(modern_statement);
  modern_statement = NULL;
  stage = 4;
  utf16_sql[0] = 'S';
  utf16_sql[1] = 'E';
  utf16_sql[2] = 'L';
  utf16_sql[3] = 'E';
  utf16_sql[4] = 'C';
  utf16_sql[5] = 'T';
  utf16_sql[6] = ' ';
  utf16_sql[7] = '1';
  utf16_sql[8] = 0;
  if (cpkt_sqlite_prepare_legacy16(database, utf16_sql, -1, &statement, NULL) !=
          CPKT_SQLITE_OK ||
      statement == NULL || statement->step(statement) != CPKT_SQLITE_ROW)
    goto fail;
  finalize_status = statement->finalize(statement);
  statement = NULL;
  if (finalize_status != CPKT_SQLITE_OK)
    goto fail;
  if (database->prepare(database, "SELECT x FROM t", -1, 0, &statement, NULL) !=
          CPKT_SQLITE_OK ||
      statement == NULL)
    goto fail;
  guard = 0x12345678;
  if (statement->scan_status_int(statement, 0, CPKT_SQLITE_SCANSTAT_NVISIT, 0,
                                 &guard) != CPKT_SQLITE_MISUSE ||
      guard != 0x12345678 ||
      statement->scan_status_i64(statement, 0, CPKT_SQLITE_SCANSTAT_EST, 0,
                                 NULL) != CPKT_SQLITE_MISUSE)
    goto fail;
  finalize_status = statement->finalize(statement);
  statement = NULL;
  if (finalize_status != CPKT_SQLITE_OK)
    goto fail;
  stage = 5;
  if (cpkt_sqlite_fts5_api_open(database, &fts) != CPKT_SQLITE_OK ||
      fts == NULL ||
      fts->find_tokenizer(fts, "unicode61", &factory) != CPKT_SQLITE_OK ||
      factory == NULL ||
      factory->create(factory, NULL, 0, &instance) != CPKT_SQLITE_OK ||
      instance == NULL)
    goto fail;
  stage = 51;
  token_count = 0;
  if (instance->tokenize(instance, &token_count,
                         CPKT_SQLITE_FTS5_TOKENIZE_DOCUMENT, "One two", 7, NULL,
                         0, token_callback) != CPKT_SQLITE_OK ||
      token_count != 2)
    goto fail;
  stage = 52;
  instance->close(instance);
  instance = NULL;
  factory->close(factory);
  factory = NULL;
  fts->close(fts);
  fts = NULL;
  database->close(database);
  database = NULL;
  stage = 53;
  if (cpkt_sqlite_shutdown() != CPKT_SQLITE_OK ||
      counts[1].initialize_count < 1 || counts[1].shutdown_count < 1 ||
      counts[1].allocate_count == 0 || counts[1].enter_count == 0 ||
      counts[1].leave_count == 0)
    goto fail;
  stage = 54;
  if (cpkt_sqlite_global_config_mutex_methods_set(&saved) != CPKT_SQLITE_OK)
    goto fail;
  cpkt_sqlite_mutex_methods_release(&saved);
  (void)remove(argv[1]);
  return 0;
fail:
  fprintf(stderr, "sqlite completion failed at stage %d (log length %d)\n",
          stage, log_length);
  fprintf(
      stderr, "mutex callbacks: init=%d end=%d alloc=%d enter=%d leave=%d\n",
      counts[1].initialize_count, counts[1].shutdown_count,
      counts[1].allocate_count, counts[1].enter_count, counts[1].leave_count);
  if (instance != NULL)
    instance->close(instance);
  if (factory != NULL)
    factory->close(factory);
  if (fts != NULL)
    fts->close(fts);
  if (statement != NULL)
    (void)statement->finalize(statement);
  if (modern_statement != NULL)
    (void)modern_statement->finalize(modern_statement);
  if (database != NULL)
    database->close(database);
  (void)cpkt_sqlite_shutdown();
  cpkt_sqlite_mutex_methods_release(&saved);
  (void)remove(argv[1]);
  return stage;
}
