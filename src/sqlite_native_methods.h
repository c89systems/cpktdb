#ifndef CPKT_SQLITE_NATIVE_METHODS_H
#define CPKT_SQLITE_NATIVE_METHODS_H

#include <cpkt/sqlite.h>
#include <sqlite3.h>

int cpkt_sqlite_native_mutex_methods_get(const sqlite3_mutex_methods *native,
                                         cpkt_sqlite_mutex_methods *out);
int cpkt_sqlite_native_mutex_methods_unwrap(
    const cpkt_sqlite_mutex_methods *methods, sqlite3_mutex_methods *out);
void cpkt_sqlite_native_mutex_methods_release(
    cpkt_sqlite_mutex_methods *methods);

int cpkt_sqlite_native_page_cache_methods_get(
    const sqlite3_pcache_methods2 *native, cpkt_sqlite_page_cache_methods *out);
int cpkt_sqlite_native_page_cache_methods_unwrap(
    const cpkt_sqlite_page_cache_methods *methods,
    sqlite3_pcache_methods2 *out);
void cpkt_sqlite_native_page_cache_methods_release(
    cpkt_sqlite_page_cache_methods *methods);

#endif
