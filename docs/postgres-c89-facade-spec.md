# PostgreSQL C89 facade

`<cpkt/postgres.h>` is the supported PostgreSQL and CockroachDB client
interface in cpkt.  It is a C89 public header: it does not expose
libpq headers, C99 fixed-width integer types, C++ linkage syntax, or native
TLS/GSSAPI objects.

The primary connection form is an explicit-receiver shell:

```c
cpkt_postgres *pg;
cpkt_postgres_result *result;

pg = cpkt_postgres_new("host=127.0.0.1 dbname=app user=app");
if (pg == 0) {
  return 1;
}
result = pg->tx(pg, "select 1");
cpkt_postgres_result_free(result);
pg->close(pg);
```

`cpkt_postgres_new()` owns one connection.  `pg->close(pg)` closes that
connection and releases the receiver.  The direct `cpkt_postgres_*` calls are
also available for every supported connection, result, COPY, pipeline,
prepared statement, escaping, notification, large-object, and fast-path
operation.

## Checked asynchronous results

After `cpkt_postgres_send_query()`, use
`cpkt_postgres_get_result_checked(connection, &result, &error)` when a NULL
result cannot be mistaken for successful completion. It returns
`CPKT_POSTGRES_RETRIEVAL_RESULT` with one owned result,
`CPKT_POSTGRES_RETRIEVAL_DRAINED` at libpq's result-sequence boundary, or
`CPKT_POSTGRES_RETRIEVAL_FAILED` for a connection or facade bookkeeping
failure. On the last two outcomes, `result` is set to NULL. On every call the
caller-owned `error` is reset; it can be omitted by passing NULL.
For nonblocking connections, call `cpkt_postgres_consume_input()` and require
`cpkt_postgres_is_busy()` to return 0 before retrieval. A checked retrieval
called while busy can block waiting for the next complete result; it does not
introduce a new readiness state. The existing socket poll, flush, consume,
and busy methods remain the readiness interface.

A server error is normally a **result**, with its status and SQLSTATE accessed
through `cpkt_postgres_result_status_get()` and
`cpkt_postgres_result_error_field()`. If the facade must discard that result
because notice binding or event preparation failed, the checked call instead
returns `FAILED`, sets `result_discarded`, and copies the result status,
SQLSTATE (when present), and up to 511 bytes of server error text into
`error`. `message_truncated` reports text truncation. This error record does
not allocate memory. A failed call is not latched: another call can fetch a
later result, but it cannot recover the discarded one and must not treat the
query as successful. Drain to the next native NULL boundary, reset, or close
the connection before reuse. A pipeline boundary can have further results.

For a receiver, call `cpkt_postgres_receive_checked(pg, &result, &error)`;
it has the same contract and delegates through its owned connection. Existing `pg->receive(pg)` and
`cpkt_postgres_get_result()` return NULL for both drainage and an internal
retrieval failure; do not use their NULL return as proof of successful async
completion. Each returned result remains owned until
`cpkt_postgres_result_free()` and may outlive its connection.

## Diagnostic and trace delivery

Register `cpkt_postgres_set_default_diagnostic_sink()` before creating a
connection when startup warnings must be captured. It receives client-side
warnings from option parsing, passfile inspection, TLS, and OAuth, including
messages emitted before a connection handle is available (the callback then
receives a null connection). `cpkt_postgres_set_diagnostic_sink()` overrides
delivery for an existing connection; passing null restores the current process
default. The callback receives `CPKT_POSTGRES_DIAGNOSTIC_WARNING` or
`CPKT_POSTGRES_DIAGNOSTIC_DEBUG` and a borrowed, complete message. An
unregistered process default retains libpq's stderr behavior. Register or
replace the process default before starting worker threads; keep callback
context storage alive while registered. Server notices use the separate notice
receiver/processor callbacks, and result errors remain available through the
result and connection error accessors.

`cpkt_postgres_set_notice_receiver()` and
`cpkt_postgres_set_notice_processor()` return 1 when the new binding is
installed, or 0 for a null connection or allocation failure. Failure leaves
the previous callback/context installed. Optional old-binding outputs report
the previous binding when found, including on allocation failure, and are
otherwise null; they do not indicate success. Always check the return value
before treating a callback as replaced or disabled. Passing a null callback
disables delivery for future results after a successful call. Existing results
and copies made with `CPKT_POSTGRES_COPY_RESULT_NOTICE_HOOKS` retain their
frozen bindings: keep each callback context alive until all results using it
have been freed, even after successful replacement or connection destruction.

`cpkt_postgres_set_trace_sink()` receives each already formatted libpq protocol
record once, with borrowed bytes and an explicit length. It replaces
`cpkt_postgres_trace()`'s file destination; `cpkt_postgres_untrace()` stops
either destination. Trace flags apply to both. Protocol records can include
credentials and query contents, so enable them only where the receiving logger
is configured for that data. The facade does not stage a sequence of records.

Event registrations use `cpkt_postgres_event_register()`. Each callback
receives `info->event`, the borrowed identity of its current registration,
including during REGISTER before `register()` returns. That identity can be
used immediately with the connection instance-data accessors. A result freezes
only registrations whose REGISTER callbacks have already succeeded. Results
created during REGISTER exclude that pending registration even if it later
succeeds; successful nested registrations are included in subsequently created
results. Registration callbacks may register another callback. A failed REGISTER
is removed and receives no later callbacks, so its application context may be
released when registration returns NULL. A failed RESULT_CREATE remains eligible
on a later explicit fire,
and only initialized registrations receive COPY and DESTROY. A result retains
its frozen registration list after its connection closes, and detached results
may be cleared independently. A callback identity remains borrowed until the
connection and all results that refer to it have been destroyed.
Server notice results carry native libpq events even though libpq does not
fire RESULT_CREATE for the borrowed notice. A receiver may copy the notice
with `CPKT_POSTGRES_COPY_RESULT_EVENTS`; the copy keeps its frozen event owner
after the callback and connection close. Fire RESULT_CREATE explicitly if the
application needs create callbacks on that copy, then free the copy normally.
During RESULT_COPY dispatch, the destination is a borrowed result identity.
An application callback may copy it again, including with notice hooks only;
the nested copy keeps the source's notice callback and context snapshot after
the connection, source, and outer copy have been released. Libpq initializes
the outer copy's event only after that callback returns, so a nested copy made
inside the callback does not receive RESULT_COPY for that still-uninitialized
native event. A later explicit RESULT_CREATE may initialize its copied event.
During RESULT_DESTROY, `info->result` remains borrowed through the callback.
A notice-hooks copy made there also retains its source callback and context
after the original result and connection are freed. The borrow ends when the
destroy callback returns; later uses of `info->result` are invalid.
This also holds when result tracking allocation fails after native copying:
the facade returns NULL, runs native destruction callbacks with a retained
notice owner, and preserves any copies made by those callbacks. Cleanup uses
a stack-local scope so retaining the owner does not require another allocation.
When a result is freed, native-initialized event state is released by libpq's
RESULT_DESTROY dispatch while the result remains valid. Frozen state for a
native-uninitialized result is released before native clear, which does not
dispatch DESTROY; a later result reusing that address keeps its own state.
The borrowed notice's event state ends when its receiver returns because
libpq does not send RESULT_DESTROY for an uninitialized notice. If an
application explicitly fires RESULT_CREATE on that borrowed result, libpq
initializes its event and sends RESULT_DESTROY when it clears the notice.
If event bookkeeping cannot be allocated, the facade skips that notice
receiver invocation so it cannot return an unsafe event-bearing copy.

`cpkt_postgres_i64` represents an exact signed 64-bit value as two 32-bit
words (`high`, `low`) without requiring `long long`.  Values are two's
complement bits; the facade performs the conversion for large-object offsets
and PostgreSQL microsecond timestamps.  PostgreSQL OIDs use
`cpkt_postgres_oid`; values outside PostgreSQL's 32-bit protocol range are
rejected before they reach libpq.

The bundled client includes TLS, GSSAPI/Kerberos, LDAP, Cyrus SASL, curl, and
the PostgreSQL OAuth client module.  It ships library artifacts only: no
server, command-line client, daemon, authentication helper, or generated
configuration tree is part of the runtime bundle.

Local Darwin facade tests need runtime search paths for the separate dependency
install directories, including curl, nghttp2, libssh2, and zlib. These search
paths apply only to local tests; the packaged SDK uses relative library paths.

For static consumers, use `find_package(CpktPostgres CONFIG REQUIRED)` and
link `cpkt::postgres`, or use `pkg-config --static --libs cpkt-postgres`.
Those interfaces carry libpq's complete static closure, including its OAuth,
frontend-common, frontend-port, LDAP, SASL, GSSAPI, TLS, resolver, and math
dependencies. Shared consumers link `cpkt::postgres_shared`. The OAuth client
module is a libpq-internal loadable component tied to PostgreSQL's major
version. The SDK ships it beside shared libpq so the built-in OAuth flow can
load it at runtime; it has no public shared-library ABI or link target.

## Live protocol compatibility gate

`make e2e-postgres` exercises the receiver shell against both PostgreSQL and
CockroachDB. The target starts both services from the rootless Podman Kube
manifest generated from `devenv.yaml.in`, provides their libpq connection
strings to the test runner, and stops the services when the run finishes. To run
`scripts/e2e-postgres.sh` directly against existing servers, set
`CPKT_POSTGRES_E2E_CONNINFO` and `CPKT_COCKROACH_E2E_CONNINFO` to standard libpq
connection strings. The gate covers receiver queries, parameters, asynchronous
send/receive, prepared statements, transaction state, and an exact 64-bit
`INT8` result. The integration executable reads connection strings directly
from those environment variables; the harness never prints them or passes them
as command-line arguments. Shared wire assertions cover result metadata,
protocol trace callback delivery and destination replacement,
NULL and binary values, errors and recovery, event instance data, single-row
and chunked delivery, nonblocking transport, escaping, prepared statements,
transactions, and 64-bit text results on both servers. PostgreSQL-specific
assertions cover
pipeline error recovery, fragmented COPY in/out, notice and notification
callbacks, cancellation/reset, and sparse large-object offsets above 4 GiB.
Those assertions run only against PostgreSQL because CockroachDB does not
promise those server-side features.
