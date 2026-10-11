#include <cpkt/sqlite.h>

static int extension_calls;
static int fail_next_open;
static int stale_receiver_seen;
static int order[3];
static int order_count;
static int require_seed;

static int ordered_extension(cpkt_sqlite *database, char **error_out,
                             void *context) {
  int id;
  (void)error_out;
  id = *(int *)context;
  if (order_count >= 3)
    return CPKT_SQLITE_MISUSE;
  order[order_count++] = id;
  if (require_seed && id == 1)
    return database->tx(database, "CREATE TABLE extension_seed(value)", 0, 0);
  if (require_seed && id == 2)
    return database->tx(database, "INSERT INTO extension_seed VALUES(1)", 0, 0);
  return CPKT_SQLITE_OK;
}

static int ordered_open(int first, int second, int third, int wide) {
  unsigned short filename[] = {':', 'm', 'e', 'm', 'o', 'r', 'y', ':', 0};
  cpkt_sqlite *database;
  int ok;
  order_count = 0;
  database = wide ? cpkt_sqlite_open16(filename) : cpkt_sqlite_new(":memory:");
  if (database == 0)
    return 0;
  ok = database->error_code(database) == CPKT_SQLITE_OK &&
       order_count == (third ? 3 : 2) && order[0] == first &&
       order[1] == second && (!third || order[2] == third);
  database->close(database);
  return ok;
}

static int registration_order(void) {
  cpkt_sqlite_auto_extension *extensions[3];
  int ids[3];
  int index;
  int ok;
  for (index = 0; index < 3; ++index) {
    ids[index] = index + 1;
    extensions[index] =
        cpkt_sqlite_auto_extension_new(ordered_extension, &ids[index]);
    if (extensions[index] == 0)
      return 0;
  }
  require_seed = 1;
  ok = extensions[0]->register_extension(extensions[0]) == CPKT_SQLITE_OK &&
       extensions[1]->register_extension(extensions[1]) == CPKT_SQLITE_OK &&
       ordered_open(1, 2, 0, 0) && ordered_open(1, 2, 0, 1);
  require_seed = 0;
  ok = ok &&
       extensions[2]->register_extension(extensions[2]) == CPKT_SQLITE_OK &&
       extensions[1]->register_extension(extensions[1]) == CPKT_SQLITE_OK &&
       ordered_open(1, 2, 3, 0);
  ok = ok && extensions[1]->cancel(extensions[1]) == 1 &&
       ordered_open(1, 3, 0, 0) &&
       extensions[1]->register_extension(extensions[1]) == CPKT_SQLITE_OK &&
       ordered_open(1, 3, 2, 0);
  cpkt_sqlite_auto_extension_reset();
  ok = ok &&
       extensions[1]->register_extension(extensions[1]) == CPKT_SQLITE_OK &&
       extensions[0]->register_extension(extensions[0]) == CPKT_SQLITE_OK &&
       extensions[2]->register_extension(extensions[2]) == CPKT_SQLITE_OK &&
       ordered_open(2, 1, 3, 0);
  extensions[0]->close(extensions[0]);
  ok = ok && ordered_open(2, 3, 0, 0);
  extensions[1]->close(extensions[1]);
  extensions[2]->close(extensions[2]);
  return ok;
}

static int initialize_extension(cpkt_sqlite *database, char **error_out,
                                void *context) {
  (void)error_out;
  (void)context;
  if (database == 0)
    return CPKT_SQLITE_MISUSE;
  ++extension_calls;
  if (fail_next_open) {
    fail_next_open = 0;
    database->tx = 0;
    return CPKT_SQLITE_NOMEM;
  }
  if (database->tx == 0)
    stale_receiver_seen = 1;
  return CPKT_SQLITE_OK;
}

static int open_with_extension(int expected_calls) {
  cpkt_sqlite *database;
  database = cpkt_sqlite_open(
      ":memory:", CPKT_SQLITE_OPEN_READWRITE | CPKT_SQLITE_OPEN_CREATE, 0);
  if (database == 0)
    return 0;
  if (database->error_code(database) != CPKT_SQLITE_OK ||
      extension_calls != expected_calls) {
    database->close(database);
    return 0;
  }
  database->close(database);
  return 1;
}

int main(void) {
  cpkt_sqlite_auto_extension *extension;
  cpkt_sqlite *database;
  unsigned short utf16_filename[] = {':', 'm', 'e', 'm', 'o', 'r', 'y', ':', 0};
  if (cpkt_sqlite_initialize() != CPKT_SQLITE_OK)
    return 1;
  if (!registration_order())
    return 12;
  extension = cpkt_sqlite_auto_extension_new(initialize_extension, 0);
  if (extension == 0 ||
      extension->register_extension(extension) != CPKT_SQLITE_OK)
    return 2;
  if (!open_with_extension(1))
    return 3;
  if (cpkt_sqlite_shutdown() != CPKT_SQLITE_OK ||
      cpkt_sqlite_initialize() != CPKT_SQLITE_OK)
    return 4;
  if (extension->register_extension(extension) != CPKT_SQLITE_OK ||
      !open_with_extension(2))
    return 5;
  fail_next_open = 1;
  database = cpkt_sqlite_open(
      ":memory:", CPKT_SQLITE_OPEN_READWRITE | CPKT_SQLITE_OPEN_CREATE, 0);
  if (database != 0 || extension_calls != 3)
    return 8;
  if (!open_with_extension(4) || stale_receiver_seen)
    return 9;
  fail_next_open = 1;
  database = cpkt_sqlite_open16(utf16_filename);
  if (database != 0 || extension_calls != 5)
    return 10;
  database = cpkt_sqlite_open16(utf16_filename);
  if (database == 0 || database->error_code(database) != CPKT_SQLITE_OK ||
      extension_calls != 6 || stale_receiver_seen)
    return 11;
  database->close(database);
  if (extension->cancel(extension) != 1)
    return 6;
  extension->close(extension);
  return cpkt_sqlite_shutdown() == CPKT_SQLITE_OK ? 0 : 7;
}
