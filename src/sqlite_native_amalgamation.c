/* Keep private adapters beside SQLite's renderer and UTF-16 name conversion. */
#include "sqlite3.c"
#include <stdarg.h>

/* Keep the pinned amalgamation's existing diagnostic policy. Apply the same
 * warning groups as other project-owned C only after the upstream include. */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic error "-Wall"
#pragma GCC diagnostic error "-Wextra"
#pragma GCC diagnostic error "-Wpedantic"
#endif

SQLITE_API void sqlite3__cpkt_log_v(int code, const char *format,
                                    va_list args) {
  if (sqlite3GlobalConfig.xLog != 0)
    renderLogMsg(code, format, args);
}

SQLITE_API void sqlite3__cpkt_log(int code, const char *format, ...) {
  va_list args;
  if (sqlite3GlobalConfig.xLog == 0)
    return;
  va_start(args, format);
  renderLogMsg(code, format, args);
  va_end(args);
}

/* Use the same native-endian conversion as sqlite3_create_function16, with
 * registration-owned cleanup from the public v2 API. This avoids a second
 * connection-wide list of obsolete facade registrations. */
SQLITE_API int sqlite3__cpkt_create_function16_v2(
    sqlite3 *db, const void *name, int argument_count, int representation,
    void *user_data, void (*scalar)(sqlite3_context *, int, sqlite3_value **),
    void (*step)(sqlite3_context *, int, sqlite3_value **),
    void (*final)(sqlite3_context *), void (*destroy)(void *)) {
  char *name_utf8;
  int status;
#ifdef SQLITE_ENABLE_API_ARMOR
  if (!sqlite3SafetyCheckOk(db) || name == 0) {
    if (destroy != 0)
      destroy(user_data);
    return SQLITE_MISUSE;
  }
#endif
  sqlite3_mutex_enter(db->mutex);
  name_utf8 = sqlite3Utf16to8(db, name, -1, SQLITE_UTF16NATIVE);
  if (name_utf8 != 0) {
    status = sqlite3_create_function_v2(db, name_utf8, argument_count,
                                        representation, user_data, scalar, step,
                                        final, destroy);
  } else {
    if (destroy != 0)
      destroy(user_data);
    status = SQLITE_NOMEM;
  }
  sqlite3DbFree(db, name_utf8);
  status = sqlite3ApiExit(db, status);
  sqlite3_mutex_leave(db->mutex);
  return status;
}
