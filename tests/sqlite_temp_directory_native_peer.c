#include <sqlite3.h>

const char *cpkt_sqlite_temp_native_peer(void) {
  return sqlite3_temp_directory;
}

char *cpkt_sqlite_temp_native_pointer(void) { return sqlite3_temp_directory; }

char *cpkt_sqlite_temp_native_copy(const char *value) {
  sqlite3_temp_directory = sqlite3_mprintf("%s", value);
  return sqlite3_temp_directory;
}
