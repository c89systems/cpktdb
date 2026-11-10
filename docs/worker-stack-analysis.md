# Database worker stack budget

Linux/glibc Debug tests run on 128 KiB default pthread stacks with the glibc
stack cache disabled. The tests check actual default and nested worker sizes,
explicit size requests, child execution and real guard faults. Main-thread
limits and build-tool defaults are preserved. Shipped libraries do not change
application thread defaults.

Linux/musl tests run on native default workers without enlarging their stacks.
Actual size and nonzero guards are checked, allowing musl's TLS/alignment
accounting. Static and shared database libraries are exercised. Owned C89 code
rejects VLAs and `alloca()`. GNU facade and SQLite builds emit `.su` frame reports.

## Reproduced failures and repairs

SQLite 3.53.4 exhausted a 128 KiB worker while preparing 1,000 arithmetic terms
(`SELECT 1+1+...`). Unmodified SQLite 3.54.1 reproduced the same failure. The
recipe pins [SQLite 3.54.1](https://www.sqlite.org/releaselog/3_54_1.html), with
exact archive SHA-256 values and its complete facade API contracts.

The ordered `cmake/patches/sqlite.series` applies two patches before compilation:

- `sqlite_expression_worklist.patch` compiles binary arithmetic using
  allocator-owned continuation records. Operand order, constant factoring,
  expression-index lookup, register ownership and subquery NULL shortcuts are
  preserved. Work records are freed on success and allocation failure.
- `sqlite_native_stack.patch` checks physical stack headroom in recursive
  expression walks, compilation, copying, SELECT compilation and index/OR
  planning. It also moves the recursive pathname helper's buffer to heap storage.

The expanded tests reproduced guard faults in unary operators, nested functions,
casts, CASE/coalesce, comparisons, boolean result expressions and indexed
alternating AND/OR predicates. Their recursive compiler/planner paths now stop
with a normal SQLite resource error before reaching the worker's guard.
Bounds belong to the OS thread and are cached in thread-local storage, so moving
a connection between workers does not retain another worker's stack limit.
A 32 KiB reserve remains for error handling and cleanup. Internal copy helpers
retain SQLite's allocation-failure cleanup convention; preparation restores its
allocator state after unwinding before reporting the stack-budget error when
there is no running VM. Genuine heap allocation failures retain `SQLITE_NOMEM`.

The AArch64 musl release rehearsal also reproduced a guard fault in
`sqlite3ExprSimplifiedAndOr` at 900 AND terms. This recursive optimization runs
inside the guarded compiler and can consume its remaining headroom. It now
returns the original subtree when headroom is low, leaving expression semantics
unchanged and letting the compiler guard report a normal resource error.
Boolean simplification regressions cover all nine pairs of true, false and NULL.
After this repair, all 117 AArch64 musl runtime checks passed under QEMU,
including both linkages on default worker stacks.

The pathname regression reproduced a separate guard fault while resolving a
40-link chain through the VFS. GCC measured a 4,320-byte frame for
`appendOnePathElement`; that buffer accumulated once per recursive hop. The
buffer now uses SQLite's allocator, with cleanup on every return. The measured
recursive frame is approximately 240 bytes. Path traversal checks the physical
reserve too, returning `SQLITE_CANTOPEN` when space is insufficient and
`SQLITE_NOMEM` on allocation failure. The existing path traversal limit is kept.

Patch and series files participate in dependency invalidation. Changing them
requires dependency preparation; consumers reject stale prepared outputs.

## Regression contract

The expression-depth limit remains 1,000. The original arithmetic regression
must evaluate successfully at that limit and reject an extra term normally.
Its operator, constant-factor, subquery, NULL, evaluation-order and expression
index cases remain strict success checks.

Other syntax families must succeed at shallow depth. Deep inputs must either
produce the correct result or return a specific parser/depth/stack resource
error. The connection must remain usable afterward. Coverage includes nested
subqueries, indexed predicates, compound SELECTs, FTS3/4/5 queries, JSON nesting,
recursive CTE evaluation and trigger-depth rejection.

The default-worker tests also exercise recursive SQL callbacks and move one
connection between workers. An explicitly requested 512 KiB comparison worker
must successfully compile a nested function expression that reports a stack
resource error on the 128 KiB worker. The VFS test resolves 200 links on the
default worker and requires a controlled error on an explicitly requested
64 KiB worker. These comparisons do not change the default-worker tests.

Allocation-failure sweeps cover every preparation allocation in a 64-term
expression and every pathname allocation in a 40-link chain. Memcheck checks
both successful cleanup and interrupted cleanup for each linkage.

After the normal Debug or musl Release build:

```sh
ctest --test-dir build/x86_64-linux-gnu/db/Debug \
  -R '^db_stack_' --no-tests=error --output-on-failure
ctest --test-dir build/x86_64-linux-musl/db/Release \
  -R '^db_stack_' --no-tests=error --output-on-failure
```

The completed checks passed 33 Debug host cases, 119 Debug runtime cases,
118 native musl runtime cases and 101 Memcheck cases with zero reported defects.
The focused stack selection contains 52 cases. The modified SQLite translation
unit also compiled with the five remaining target compilers (AArch64 and ARMHF
with both libc variants, plus Darwin ARM64); those checks did not execute their
target binaries. Both pathname allocation-failure sweeps finished with zero
bytes in zero blocks still allocated.

## PostgreSQL and iODBC observations

Existing facade/integration tests run on the same measured default workers,
including static/shared iODBC. Libpq parsing of 10,000 repeated connection
options passes with both linkages. The existing live PostgreSQL and CockroachDB
checks cover asynchronous retrieval, pipeline, COPY, large objects, notices,
cancellation and reset. No PostgreSQL or iODBC implementation changes were
needed for the exercised paths.

The largest measured owned facade frame is 384 bytes. The libpq frame analysis
using its optimized producer flags reported 8,544 bytes for
`lo_import_internal`, 8,528 for `lo_export`, and 2,528 for `initialize_SSL`.
The large-object routines use bounded buffers in loops.

These measurements and regressions establish the named stack-budget behavior.
They do not bound arbitrary application callback code or external ODBC drivers;
those implementations must meet their own worker-stack requirements.
