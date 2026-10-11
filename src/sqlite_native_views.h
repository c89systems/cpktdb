#ifndef CPKT_SQLITE_NATIVE_VIEWS_H
#define CPKT_SQLITE_NATIVE_VIEWS_H

#include <cpkt/sqlite.h>
#include <sqlite3.h>

/* Owned view metadata around borrowed native public VFS/file receivers. */
cpkt_sqlite_vfs *cpkt_sqlite_native_vfs_view(sqlite3_vfs *native);
int cpkt_sqlite_native_vfs_view_is(const cpkt_sqlite_vfs *view);
int cpkt_sqlite_native_vfs_view_register(cpkt_sqlite_vfs *view,
                                         int make_default);
int cpkt_sqlite_native_vfs_view_unregister(cpkt_sqlite_vfs *view);
void cpkt_sqlite_native_vfs_view_close(cpkt_sqlite_vfs *view);
cpkt_sqlite_file *cpkt_sqlite_native_file_view(sqlite3_file *native);
cpkt_sqlite_file *cpkt_sqlite_native_file_metadata_view(sqlite3_file *native);
void cpkt_sqlite_native_file_view_release(cpkt_sqlite_file *view);
int cpkt_sqlite_native_file_view_init(cpkt_sqlite_file *public_file,
                                      sqlite3_file *native, int owns_native);

#endif
