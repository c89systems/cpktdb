#include <cpkt/sqlite.h>

#include <string.h>

static int destroy_calls;

static int query_callback(void *context, cpkt_sqlite_rtree_query *query) {
  (void)context;
  (void)query;
  return CPKT_SQLITE_OK;
}

static void destroy_context(void *context) {
  (void)context;
  ++destroy_calls;
}

int main(void) {
  char name[257];
  cpkt_sqlite *database;
  int status;
  memset(name, 'r', sizeof(name) - 1U);
  name[sizeof(name) - 1U] = '\0';
  database = cpkt_sqlite_new(":memory:");
  if (database == 0 || database->error_code(database) != CPKT_SQLITE_OK)
    return 1;
  status = cpkt_sqlite_register_rtree_query(database, name, query_callback, 0,
                                            destroy_context);
  if (status == CPKT_SQLITE_OK || destroy_calls != 1)
    return 2;
  database->close(database);
  return destroy_calls == 1 ? 0 : 3;
}
