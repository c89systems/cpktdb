#include <cpkt/sqlite.h>

int main(void) {
  cpkt_sqlite *database;
  cpkt_sqlite_session *session;
  database = cpkt_sqlite_new(":memory:");
  if (database == 0 || database->error_code(database) != CPKT_SQLITE_OK)
    return 1;
  if (database->tx(database, "create table item(id integer primary key)", 0,
                   0) != CPKT_SQLITE_OK)
    return 2;
  session = 0;
  if (cpkt_sqlite_session_new(database, "main", &session) != CPKT_SQLITE_OK ||
      session == 0 || session->attach(session, "item") != CPKT_SQLITE_OK)
    return 3;
  if (cpkt_sqlite_set_preupdate_hook(database, 0, 0) != CPKT_SQLITE_MISUSE)
    return 4;
  if (database->tx(database, "insert into item values(1)", 0, 0) !=
          CPKT_SQLITE_OK ||
      session->empty(session) != 0)
    return 5;
  session->close(session);
  if (cpkt_sqlite_set_preupdate_hook(database, 0, 0) != CPKT_SQLITE_OK)
    return 6;
  database->close(database);
  return 0;
}
