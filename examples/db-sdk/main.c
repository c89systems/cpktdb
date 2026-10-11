#include <cpkt/sqlite.h>

int main(void) {
  cpkt_sqlite *database;
  cpkt_sqlite_statement *statement;
  int result;

  if (cpkt_sqlite_library_version_number() <= 0 ||
      cpkt_sqlite_library_version() == 0) {
    return 1;
  }
  database = cpkt_sqlite_new(":memory:");
  if (database == 0 || database->tx == 0 || database->prepare == 0 ||
      database->close == 0) {
    return 2;
  }
  result =
      database->tx(database, "create table pkg_smoke(value integer)", 0, 0);
  if (result != CPKT_SQLITE_OK) {
    database->close(database);
    return 3;
  }
  result = database->prepare(database, "select 42", -1, 0, &statement, 0);
  if (result != CPKT_SQLITE_OK || statement == 0 || statement->step == 0 ||
      statement->finalize == 0) {
    database->close(database);
    return 4;
  }
  result = statement->step(statement);
  if (result != CPKT_SQLITE_ROW || statement->column_int(statement, 0) != 42 ||
      statement->finalize(statement) != CPKT_SQLITE_OK) {
    database->close(database);
    return 5;
  }
  database->close(database);
  return 0;
}
