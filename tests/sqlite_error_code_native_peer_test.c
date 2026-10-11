#include <cpkt/sqlite.h>
#include <sqlite3.h>

#include <stdio.h>

int main(void) {
  sqlite3 *native = NULL;
  cpkt_sqlite *facade = NULL;
  int native_status;
  int facade_status;
  int extended;
  int stage = 1;
  const char *schema = "CREATE TABLE t(x UNIQUE); INSERT INTO t VALUES(1)";
  const char *conflict = "INSERT INTO t VALUES(1)";
  if (sqlite3_open(":memory:", &native) != SQLITE_OK)
    goto fail;
  facade = cpkt_sqlite_new(":memory:");
  if (facade == NULL ||
      sqlite3_exec(native, schema, NULL, NULL, NULL) != SQLITE_OK ||
      cpkt_sqlite_exec(facade, schema, NULL, NULL) != CPKT_SQLITE_OK)
    goto fail;
  for (extended = 0; extended <= 1; ++extended) {
    stage = 2 + extended;
    if (sqlite3_extended_result_codes(native, extended) != SQLITE_OK ||
        cpkt_sqlite_extended_result_codes(facade, extended) != CPKT_SQLITE_OK)
      goto fail;
    native_status = sqlite3_exec(native, conflict, NULL, NULL, NULL);
    facade_status = cpkt_sqlite_exec(facade, conflict, NULL, NULL);
    if (native_status != facade_status ||
        sqlite3_errcode(native) != cpkt_sqlite_result_code(facade) ||
        sqlite3_extended_errcode(native) != cpkt_sqlite_error_code(facade) ||
        sqlite3_errcode(native) != native_status)
      goto fail;
  }
  stage = 4;
  if (cpkt_sqlite_result_code(NULL) != CPKT_SQLITE_MISUSE)
    goto fail;
  facade->close(facade);
  if (sqlite3_close(native) != SQLITE_OK)
    return 1;
  return 0;
fail:
  fprintf(stderr, "SQLite result-code peer mismatch at stage %d\n", stage);
  if (facade != NULL)
    facade->close(facade);
  if (native != NULL)
    sqlite3_close(native);
  return 1;
}
