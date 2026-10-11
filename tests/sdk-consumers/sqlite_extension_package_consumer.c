#include <cpkt/sqlite.h>

#include <stdio.h>

int main(int argc, char **argv) {
  cpkt_sqlite *database;
  cpkt_sqlite_statement *statement;
  char *error;
  int status;
  if (argc != 2)
    return 2;
  database = cpkt_sqlite_open(
      ":memory:", CPKT_SQLITE_OPEN_READWRITE | CPKT_SQLITE_OPEN_CREATE, NULL);
  if (database == NULL)
    return 1;
  statement = NULL;
  error = NULL;
  status = cpkt_sqlite_enable_extension_loading(database, 1);
  if (status == CPKT_SQLITE_OK)
    status = cpkt_sqlite_load_extension(database, argv[1],
                                        "sqlite3_cpktprobe_init", &error);
  if (status == CPKT_SQLITE_OK)
    status = cpkt_sqlite_enable_extension_loading(database, 0);
  if (status == CPKT_SQLITE_OK)
    status = cpkt_sqlite_prepare(database, "SELECT cpkt_add_seven(35)", -1, 0,
                                 &statement, NULL);
  if (status == CPKT_SQLITE_OK &&
      (statement->step(statement) != CPKT_SQLITE_ROW ||
       statement->column_int(statement, 0) != 42 ||
       statement->step(statement) != CPKT_SQLITE_DONE))
    status = CPKT_SQLITE_ERROR;
  if (error != NULL)
    cpkt_sqlite_free(error);
  if (statement != NULL)
    statement->finalize(statement);
  database->close(database);
  if (status != CPKT_SQLITE_OK) {
    fputs("SQLite loadable extension failed\n", stderr);
    return 1;
  }
  return 0;
}
