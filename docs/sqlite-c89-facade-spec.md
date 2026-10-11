# SQLite C89 facade

`<cpkt/sqlite.h>` is the supported embedded SQLite interface in
cpkt.  It is a C89 public header and deliberately does not expose
`sqlite3.h`, SQLite handles, or non-C89 integer types.

The primary form is the receiver shell:

```c
cpkt_sqlite *db;

db = cpkt_sqlite_new("app.db");
if (db == 0) return 1;
if (db->tx(db, "create table if not exists item (id integer primary key)", 0, 0)
    != CPKT_SQLITE_OK) {
  db->close(db);
  return 1;
}
db->close(db);
```

`cpkt_sqlite_open()` preserves SQLite's `open_v2` flags and optional VFS name.
The caller owns the returned receiver, including a receiver returned for a
failed SQLite open where SQLite supplied a database handle: inspect `error`
and `error_code`, then call `close`.

The legacy `sqlite3_temp_directory` global is available through
`cpkt_sqlite_temp_directory_get/set`. The setter makes a SQLite-allocator copy
and frees the previous value; NULL restores SQLite's normal temporary-folder
search. The getter borrows the current string until any native or facade
assignment. SQLite requires changes during process setup, with no open
connections and no concurrent SQLite calls. SQLite discourages this legacy
global on Linux and Darwin; it remains available for applications that need
native compatibility. `sqlite3_data_directory` affects only the Windows VFS
and is excluded from the delivered Linux/Darwin facade. The public
`sqlite3_version[]` string is available through `cpkt_sqlite_library_version()`.

For process-wide automatic extensions, create an
`cpkt_sqlite_auto_extension` receiver and register it before opening a
connection. The initialization callback receives the eventual connection
receiver rather than a transient native handle, so registrations made from the
callback have the same lifetime as the returned connection. Cancel returns the
number of registrations removed, matching the underlying API.

The SDK also installs SQLite's public `sqlite3ext.h` for native loadable
modules. Such a module receives `sqlite3_api_routines` from the bundled SQLite
engine and uses `SQLITE_EXTENSION_INIT1`/`SQLITE_EXTENSION_INIT2` as upstream
specifies. A C89 facade client can enable extension loading, load that module,
and disable loading with `cpkt_sqlite_enable_extension_loading()` and
`cpkt_sqlite_load_extension()`. This native module ABI is separate from the
SDK-bound C89 auto-extension callback: the latter receives a facade database
receiver, while the former receives SQLite's native connection and dispatch
table. The facade callback does not reinterpret a foreign SQLite engine's
native connection as a facade receiver.

`tx` executes one or more SQL statements and invokes its optional row callback
without materializing result sets. `prepare` creates an explicit statement
receiver. Text and blob bind methods copy their input before returning; column
text and blob pointers are SQLite-owned and remain valid only until the next
step/reset/finalize operation on that statement.

The legacy profiler may run alongside a v2 trace callback. Installing or
clearing the legacy profiler preserves the v2 trace registration and returns
the previous legacy profiler context. Registering a new trace callback cancels
the previous trace and legacy profiler, matching SQLite's trace behavior.
The log callback follows SQLite's configured log path and its native
per-record size cap. Formatting, including SQLite's `%q`, `%Q`, `%z`, and `%n`
behavior, occurs only when a log callback is installed. The signed and
unsigned 64-bit helpers pass the exact 64-bit value to that same renderer.
`SQLITE_CONFIG_LOG` remains replaceable after initialization, as in the
configured SQLite provider.
For a connection's latest failure, `cpkt_sqlite_result_code()` follows
`sqlite3_errcode()` and its current extended-result-code mask;
`cpkt_sqlite_error_code()` returns SQLite's extended code.

On serialized connections, callback setters that keep a callback/context pair
in facade state hold the connection mutex while updating the pair and the
native registration. The facade uses a private mutex for its own bookkeeping;
SQLite's static APPLICATION mutexes remain available to callers.

Unlock-notify batches contain contexts registered with the same public
callback. Each registration receives a callback invocation with that callback's
contexts, and the facade retains a replaced binding until SQLite finishes any
concurrent delivery.

Extended changeset apply operations collect rebase output only when
`rebase_out` is provided. Passing `NULL` avoids the input-proportional native
rebase allocation, including for `_strm` apply operations.
An apply without a conflict callback aborts with `CPKT_SQLITE_ABORT` when a
changeset conflicts with existing data.
An empty session changeset may contain `data == NULL` and `byte_count == 0`.
Rebaser `configure` and `rebase` accept that pair; an empty `rebase` result is
still an owned changeset and must be freed.

`db->close(db)` is the convenience destructor: it defers native teardown until
facade-owned statements, blobs, and backups have been finalized or closed, so
callback state never outlives its facade owner. Use `cpkt_sqlite_close_strict()`
when the caller needs strict close behavior: it returns `CPKT_SQLITE_BUSY` and
leaves the receiver usable while any such child remains open.

`cpkt_sqlite_i64` holds exact signed 64-bit two's-complement bits in two
32-bit words, avoiding a public `long long` requirement. Use
`cpkt_sqlite_i64_make(high, low)` for binding and compare `high` and `low`
after `column_i64` or `last_insert_rowid`.

SQLite's `%ll` formatting conversions require a native 64-bit scalar in a
variadic call. In C89, use the typed `cpkt_sqlite_format_i64()` and
`cpkt_sqlite_format_u64()` functions for one signed or unsigned conversion;
the corresponding `format_into`, `log`, and `string_append_format` typed
functions cover the same case. Their format string may include literal text
and `%%`, but exactly one conversion requiring an argument, with matching
signedness, is allowed. Combine multiple typed values through a
`cpkt_sqlite_string` builder. The ordinary variadic formatting functions
accept only C89-native argument types.

For file controls `SQLITE_FCNTL_SIZE_HINT`, `SQLITE_FCNTL_MMAP_SIZE`, and
`SQLITE_FCNTL_SIZE_LIMIT`, pass a `cpkt_sqlite_i64 *` to
`cpkt_sqlite_file_control()`. The same payload type is passed to facade VFS
file-control callbacks. Use the typed int, unsigned, text, file, VFS, and
FILESTAT calls for their documented operation IDs. Text returned by
`cpkt_sqlite_file_control_text()` is owned and released with
`cpkt_sqlite_free()`. For `GET_LOCKPROXYFILE`, the facade copies SQLite's
borrowed provider path or static status string; allocation failure returns
`CPKT_SQLITE_NOMEM` and a null output, while provider errors leave the output
null. A successful null provider path remains null. File and VFS control
handles own only facade metadata. They borrow SQLite's open file or registered
VFS, so close each handle before its database or VFS is closed. A file view
borrowed through a handle cannot close the database's file; close the handle
to release the view metadata. Custom facade VFS files follow this same rule;
each handle has independent borrowed-view metadata. A VFS handle's methods refer
to the exact provider retained by its database, including after same-name
replacement or unregistration. Keep that provider alive until the database and
its handles are closed. A directly opened VFS file owns its native file
and must be closed through its file methods. `cpkt_sqlite_vfs_find()` returns
the original facade registration for a custom VFS, or a separately owned
callable view of the native/default VFS; close the latter before unregistering
its provider. Other opaque application payloads remain `void *`; native
provider-private structs are not part of this C89 interface.

Direct `cpkt_sqlite_vfs_register()` and `cpkt_sqlite_vfs_unregister()` work on
both custom registrations and native/default views. A native view's close
releases only view metadata and leaves its provider registration alone. If the
native default is unregistered, register it again before opening another
database. `cpkt_sqlite_vfs_database_file_object()` accepts a journal or WAL
filename during a VFS open callback. For a native database file it returns
independent metadata that its file `close` releases without closing SQLite's
file. A file-control handle view remains borrowed from its handle: its file
`close` returns misuse, and the handle releases its metadata.

`cpkt_sqlite_global_config_mutex_methods_get()` and the PCACHE2 getter
return callable tables for native defaults as well as facade registrations.
Each native table getter owns independent adapter metadata; release it with
its matching `*_methods_release()` after its cache pages or mutexes are gone.
For a native PCACHE2 view, repeated fetches of a pinned key return the same
page receiver. One unpin releases that receiver regardless of fetch count;
truncate implicitly releases pinned receivers at or above its limit, and
destroy releases all remaining page receivers.
The selected native function table is copied into that metadata, so a later
configuration change does not retarget an earlier view. Facade registration
getters return the registered application callback/context identities.
Optional mutex held/not-held debug callbacks may be NULL.

FTS5 extension registration is also facade-owned. `cpkt_sqlite_fts5_api_open()`
returns a receiver scoped to its database connection. It registers locale-aware
custom tokenizers and auxiliary functions without exposing native FTS types.
An auxiliary callback receives a callback-local `cpkt_sqlite_fts5_context`
receiver for phrase, occurrence, token, column, row-count, query-phrase, and
auxiliary-data operations. Do not retain that context, its phrase iterator, or
its text/token views after the callback returns. Closing the database releases
registered tokenizer and auxiliary bindings and invokes their supplied
destructors exactly once.
The query-phrase callback receives its own callback-local context; its
`user_data` receiver returns the pointer passed to `query_phrase`.

Static consumers use `find_package(CpktSqlite CONFIG REQUIRED)` and
`cpkt::sqlite`, or `pkg-config --static --libs cpkt-sqlite`. Shared consumers
link `cpkt::sqlite_shared`.

The V0 delivery contract is a complete facade for the published SQLite 3.53.4
public surface, not only the receiver convenience layer shown above. Its
required feature families and coverage rule are recorded in
`docs/sqlite-c89-facade-surface.md`.
