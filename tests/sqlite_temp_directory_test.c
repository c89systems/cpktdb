#include <cpkt/sqlite.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

extern const char *cpkt_sqlite_temp_native_peer(void);
extern char *cpkt_sqlite_temp_native_pointer(void);
extern char *cpkt_sqlite_temp_native_copy(const char *value);

static int fail_allocation;

char *__real_sqlite3_mprintf(const char *format, ...);
char *__wrap_sqlite3_mprintf(const char *format, ...) {
  va_list arguments;
  const char *value;
  if (fail_allocation)
    return NULL;
  va_start(arguments, format);
  value = va_arg(arguments, const char *);
  va_end(arguments);
  return __real_sqlite3_mprintf(format, value);
}

int main(void) {
  char directory[16];
  char *previous;
  strcpy(directory, "build");
  if (cpkt_sqlite_temp_directory_set(directory) != CPKT_SQLITE_OK ||
      cpkt_sqlite_temp_directory_get() != cpkt_sqlite_temp_native_peer() ||
      cpkt_sqlite_temp_native_pointer() == directory)
    return 1;
  directory[0] = 'X';
  if (strcmp(cpkt_sqlite_temp_directory_get(), "build") != 0)
    return 2;
  previous = cpkt_sqlite_temp_native_pointer();
  fail_allocation = 1;
  if (cpkt_sqlite_temp_directory_set("lost") != CPKT_SQLITE_NOMEM ||
      cpkt_sqlite_temp_native_pointer() != previous)
    return 3;
  fail_allocation = 0;
  if (cpkt_sqlite_temp_directory_set("build/db-completion") != CPKT_SQLITE_OK ||
      strcmp(cpkt_sqlite_temp_native_peer(), "build/db-completion") != 0)
    return 4;
  if (cpkt_sqlite_temp_directory_set(NULL) != CPKT_SQLITE_OK ||
      cpkt_sqlite_temp_native_peer() != NULL)
    return 5;
  previous = cpkt_sqlite_temp_native_copy("native");
  if (previous == NULL || cpkt_sqlite_temp_directory_get() != previous ||
      cpkt_sqlite_temp_directory_set(NULL) != CPKT_SQLITE_OK ||
      cpkt_sqlite_temp_native_peer() != NULL)
    return 6;
  return 0;
}
