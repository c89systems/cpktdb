#ifndef CPKT_POSTGRES_EVENTS_PRIVATE_H
#define CPKT_POSTGRES_EVENTS_PRIVATE_H

#include <libpq-fe.h>
#include <stddef.h>

typedef struct cpkt_postgres_callback_result {
  const PGresult *result;
  struct cpkt_postgres_notice_binding *owner;
  struct cpkt_postgres_callback_result *next;
} cpkt_postgres_callback_result;

/* Borrow a result identity during native event dispatch. Create/copy
 * callbacks run before lasting result tracking; destroy callbacks run after
 * that tracking is detached. Nested copies can retain the source snapshot
 * only during the native callback's valid result borrow. */
void cpkt_postgres_notice_callback_begin(const PGconn *connection,
                                         const PGresult *source,
                                         const PGresult *borrowed,
                                         cpkt_postgres_callback_result *scope);
void cpkt_postgres_notice_callback_end(cpkt_postgres_callback_result *scope);

/* Private allocation boundary used by native-peer fault regressions. */
void *cpkt_postgres_event_allocate(size_t size);
/* Private result binding allocation boundary for native-peer fault tests. */
void *cpkt_postgres_result_binding_allocate(size_t size);

/* Prepare result state without firing RESULTCREATE for application-created
 * results. These helpers are private to the PostgreSQL facade. */
int cpkt_postgres_event_prepare_result(PGconn *connection, PGresult *result);
/* A borrowed notice has native events but no initial RESULTCREATE. Hold a
 * source role during its callback; release it afterward unless an explicit
 * RESULTCREATE initialized native destruction for that result. */
int cpkt_postgres_event_prepare_notice(PGconn *connection,
                                       const PGresult *result);
void cpkt_postgres_event_release_notice(const PGresult *result);
int cpkt_postgres_event_prepare_copy(const PGresult *source, PGresult *dest,
                                     int flags);
/* Drop only uninitialized native-dispatcher metadata before PQclear can free
 * and reuse the result address. Initialized metadata belongs to DESTROY. */
void cpkt_postgres_event_release_uninitialized(const PGresult *result);
void cpkt_postgres_event_result_release(const PGresult *result);

#endif
