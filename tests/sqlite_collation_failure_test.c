#include <cpkt/sqlite.h>

static int compare(void *context, int left_byte_count, const void *left,
                   int right_byte_count, const void *right) {
  (void)context;
  (void)left_byte_count;
  (void)left;
  (void)right_byte_count;
  (void)right;
  return 0;
}

static void destroy(void *context) {
  int *destroy_count;
  destroy_count = (int *)context;
  if (destroy_count != 0)
    *destroy_count += 1;
}

int main(void) {
  cpkt_sqlite *database;
  int destroy_count;
  int index;
  int status;

  destroy_count = 0;
  database = cpkt_sqlite_new(":memory:");
  if (database == 0)
    return 1;
  status = cpkt_sqlite_create_collation(database, "invalid-collation", 0,
                                        &destroy_count, compare, destroy);
  if (status != CPKT_SQLITE_MISUSE || destroy_count != 0) {
    database->close(database);
    return 2;
  }
  for (index = 0; index < 10; ++index) {
    status = cpkt_sqlite_create_collation(database, "cycle-collation",
                                          CPKT_SQLITE_UTF8, &destroy_count,
                                          compare, destroy);
    if (status != CPKT_SQLITE_OK) {
      database->close(database);
      return 3;
    }
    status = cpkt_sqlite_create_collation(database, "cycle-collation",
                                          CPKT_SQLITE_UTF8, &destroy_count, 0,
                                          destroy);
    if (status != CPKT_SQLITE_OK || destroy_count != index + 1) {
      database->close(database);
      return 4;
    }
  }
  database->close(database);
  return destroy_count == 10 ? 0 : 5;
}
