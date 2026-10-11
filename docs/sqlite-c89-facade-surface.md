# SQLite C89 facade surface contract

SQLite 3.53.4's published `sqlite3.h` is the source of truth for the V0
facade. The facade is complete only when every applicable entry point below is
available through C89-owned names, handles, scalar compatibility values, and
callback records. A native pointer, native struct definition, native allocator,
or a compiler extension must not escape through `<cpkt/sqlite.h>`.

The required surface is:

- library lifecycle, compile options, global and per-database configuration,
  memory allocation/limits/status, randomness, logging, and error reporting;
- database opening (including URI, VFS, UTF-16, filename, lock-timeout, and
  file-control paths), close, interrupt, limits, transaction state, and
  database metadata;
- direct SQL execution, prepare variants, statement inspection/status,
  binding (including owned/destructor, zero/blob, pointer, and 64-bit forms),
  stepping/reset/finalize, and every column/value conversion;
- SQL functions, aggregates, window functions, collations, authorizers,
  busy/progress/trace/profile/update/commit/rollback/WAL/preupdate hooks, and
  their callback lifetime/destruction rules;
- blobs, online backup, serialize/deserialize, snapshots, shared cache,
  extension loading and auto-extension registration;
- virtual-table modules and index information, virtual-table configuration,
  RTree callbacks, C-array binding, VFS/file/io-method registration, mutexes,
  and all callback structures required to implement those extension points;
- keyword/string/formatting helpers, `sqlite3_str`, table helpers, and
  statement scan status;
- session, changeset, changegroup, rebaser, conflict/filter callbacks, and all
  SQLite `_strm` callback APIs enabled by the bundled SQLite build.

The bundled source enables the corresponding SQLite optional core features:
FTS3/4/5, RTree/Geopoly, session/changesets, preupdate hooks, snapshots,
deserialize, column metadata, normalized SQL, math functions, statement scan
status and virtual tables, DBSTAT, unlock notify, and lock-timeout support.
If an upstream release adds a published API or a feature profile changes, the
facade API inventory and generated coverage check must fail until the facade is
adapted deliberately.

SQLite's test-control interface is intentionally outside this public facade:
SQLite documents it as an unstable, test-only fault-injection interface that
applications must not use, and its variadic operation records have no stable
portable contract to adapt. CEROD and Win32-only directory entry points are
also outside the delivered feature profile. All other published APIs applicable
to the configured SQLite source belong to this contract.

The VFS facade supports VFS method versions 1 through 3 and file I/O method
versions 1 through 3. File offsets, sizes, and VFS current-time milliseconds
use the facade's two-word 64-bit value so native non-C89 integer types do not
cross the public boundary.

`cpkt_sqlite_auto_extension_new()` creates a process-wide registration
receiver. Register it before opening the connections it should initialize;
cancel returns the number of native registrations removed, and close releases
the receiver. Its callback is passed the same connection receiver subsequently
returned by `cpkt_sqlite_open()`, so facade-created functions and other
connection-scoped registrations retain their correct lifetime.
