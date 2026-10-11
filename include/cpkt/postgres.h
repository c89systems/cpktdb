#ifndef CPKT_POSTGRES_H
#define CPKT_POSTGRES_H

/**
 * @defgroup cpkt_postgres PostgreSQL C89 facade
 *
 * Receiver methods take the receiver as their first argument. Results are
 * separately owned; release each with cpkt_postgres_result_free(), including
 * results returned by tx() and receive(). Text returned by the provider remains
 * borrowed from its connection or result unless a function says otherwise.
 * Connection and callback lifetime rules are in
 * docs/postgres-c89-facade-spec.md.
 * @{
 */

#include <stddef.h>
#include <stdio.h>

/** Opaque native connection; release with cpkt_postgres_connection_free(). */
typedef struct cpkt_postgres_connection cpkt_postgres_connection;
/** Owned query result; release with cpkt_postgres_result_free(). */
typedef struct cpkt_postgres_result cpkt_postgres_result;
/** Owned legacy cancel handle; release with cpkt_postgres_cancel_free(). */
typedef struct cpkt_postgres_cancel cpkt_postgres_cancel;
/** Owned asynchronous cancel connection; finish with
 * cpkt_postgres_cancel_connection_free(). */
typedef struct cpkt_postgres_cancel_connection cpkt_postgres_cancel_connection;
/** Borrowed TLS implementation object; valid while its connection lives. */
typedef struct cpkt_postgres_tls_object cpkt_postgres_tls_object;
/** Borrowed GSS security context; valid while its connection lives. */
typedef struct cpkt_postgres_gss_context cpkt_postgres_gss_context;
/** Receiver shell for one connection; close it while freeing results
 * separately. */
typedef struct cpkt_postgres cpkt_postgres;
/** Event registration owned by its connection and any surviving results. */
typedef struct cpkt_postgres_event cpkt_postgres_event;

/** PostgreSQL client event identifiers, in provider dispatch order. */
typedef enum cpkt_postgres_event_id {
  CPKT_POSTGRES_EVENT_REGISTER = 0,
  CPKT_POSTGRES_EVENT_CONNECTION_RESET = 1,
  CPKT_POSTGRES_EVENT_CONNECTION_DESTROY = 2,
  CPKT_POSTGRES_EVENT_RESULT_CREATE = 3,
  CPKT_POSTGRES_EVENT_RESULT_COPY = 4,
  CPKT_POSTGRES_EVENT_RESULT_DESTROY = 5
} cpkt_postgres_event_id;

/** Callback-local event information; use the member selected by id.
 * RESULT_DESTROY borrows result until callback return, including cleanup
 * after a failed result-copy allocation. A copy with NOTICE_HOOKS retains
 * the source notice snapshot beyond connection and source destruction. */
typedef struct cpkt_postgres_event_info {
  /** Borrowed identity of the registration currently receiving this event. */
  cpkt_postgres_event *event;
  cpkt_postgres_connection *connection;
  cpkt_postgres_result *result;
  const cpkt_postgres_result *source;
  cpkt_postgres_result *destination;
} cpkt_postgres_event_info;

/** Return zero to decline initialization of a registration or result. */
typedef int (*cpkt_postgres_event_callback)(
    cpkt_postgres_event_id id, const cpkt_postgres_event_info *info,
    void *context);

/** Registers an event callback; returns NULL if its REGISTER callback fails.
 * The returned identity is borrowed until connection and its results die.
 * Results created during REGISTER exclude this pending registration, even
 * if it later succeeds. A failed registration receives no later callbacks;
 * its application context may be released when this function returns NULL.
 * A callback pointer may be registered once per connection. */
cpkt_postgres_event *
cpkt_postgres_event_register(cpkt_postgres_connection *connection,
                             cpkt_postgres_event_callback callback,
                             const char *name, void *context);
/** Sets or reads per-connection application data for a registration. */
int cpkt_postgres_event_set_connection_data(
    cpkt_postgres_connection *connection, cpkt_postgres_event *event,
    void *data);
/** Returns the borrowed application data associated with this connection event.
 */
void *
cpkt_postgres_event_connection_data(const cpkt_postgres_connection *connection,
                                    const cpkt_postgres_event *event);
/** Sets or reads per-result application data for a registration. */
int cpkt_postgres_event_set_result_data(cpkt_postgres_result *result,
                                        cpkt_postgres_event *event, void *data);
/** Returns the borrowed application data associated with this result event. */
void *cpkt_postgres_event_result_data(const cpkt_postgres_result *result,
                                      const cpkt_postgres_event *event);
/** Fires pending result-create callbacks, including callbacks that failed
 * on a prior attempt. Returns zero while any callback still fails. */
int cpkt_postgres_event_fire_result_create(cpkt_postgres_connection *connection,
                                           cpkt_postgres_result *result);

/** Receives a borrowed notice result; retain a copy if needed after return. */
typedef void (*cpkt_postgres_notice_receiver)(
    void *context, cpkt_postgres_connection *connection,
    const cpkt_postgres_result *result);
/** Receives borrowed notice text during dispatch; do not retain it. */
typedef void (*cpkt_postgres_notice_processor)(
    void *context, cpkt_postgres_connection *connection, const char *message);
/** Diagnostic record outside server notices and result errors. The text is
 * borrowed until return. Do not free the connection from this callback. */
typedef void (*cpkt_postgres_diagnostic_sink)(
    void *context, cpkt_postgres_connection *connection, int level,
    const char *message);
/** One formatted PostgreSQL protocol trace record. Bytes are borrowed until
 * return. A record can contain connection credentials or query data. */
typedef void (*cpkt_postgres_trace_sink)(void *context,
                                         cpkt_postgres_connection *connection,
                                         const char *record, size_t length);
/** Warning level for a client-side diagnostic. */
#define CPKT_POSTGRES_DIAGNOSTIC_WARNING 1
/** Explicitly enabled developer tracing; may contain sensitive data. */
#define CPKT_POSTGRES_DIAGNOSTIC_DEBUG 2
/** Process-wide lock callback; nonzero acquires and zero releases. */
typedef void (*cpkt_postgres_thread_lock)(int acquire);
/** Fills the supplied buffer with a private-key password for TLS startup. */
typedef int (*cpkt_postgres_ssl_key_password_hook)(
    char *buffer, int buffer_size, cpkt_postgres_connection *connection);

/* PostgreSQL OIDs are unsigned 32-bit protocol identifiers. */
/** PostgreSQL protocol object identifier, carried as an unsigned 32-bit value.
 */
typedef unsigned long cpkt_postgres_oid;

/* Exact 64-bit two's-complement bits, represented without a non-C89 scalar. */
/** Signed 64-bit value split into high and low 32-bit words for C89. */
typedef struct cpkt_postgres_i64 {
  unsigned long high;
  unsigned long low;
} cpkt_postgres_i64;

/** Connection state returned by status and asynchronous polling. */
typedef enum cpkt_postgres_connection_status {
  CPKT_POSTGRES_CONNECTION_OK = 0,
  CPKT_POSTGRES_CONNECTION_BAD = 1,
  CPKT_POSTGRES_CONNECTION_STARTED = 2,
  CPKT_POSTGRES_CONNECTION_MADE = 3,
  CPKT_POSTGRES_CONNECTION_AWAITING_RESPONSE = 4,
  CPKT_POSTGRES_CONNECTION_AUTH_OK = 5,
  CPKT_POSTGRES_CONNECTION_SETENV = 6,
  CPKT_POSTGRES_CONNECTION_SSL_STARTUP = 7,
  CPKT_POSTGRES_CONNECTION_NEEDED = 8,
  CPKT_POSTGRES_CONNECTION_CHECK_WRITABLE = 9,
  CPKT_POSTGRES_CONNECTION_CONSUME = 10,
  CPKT_POSTGRES_CONNECTION_GSS_STARTUP = 11,
  CPKT_POSTGRES_CONNECTION_CHECK_TARGET = 12,
  CPKT_POSTGRES_CONNECTION_CHECK_STANDBY = 13,
  CPKT_POSTGRES_CONNECTION_ALLOCATED = 14,
  CPKT_POSTGRES_CONNECTION_AUTHENTICATING = 15
} cpkt_postgres_connection_status;

/** Next socket readiness action for an asynchronous operation. */
typedef enum cpkt_postgres_poll_status {
  CPKT_POSTGRES_POLL_FAILED = 0,
  CPKT_POSTGRES_POLL_READING = 1,
  CPKT_POSTGRES_POLL_WRITING = 2,
  CPKT_POSTGRES_POLL_OK = 3,
  CPKT_POSTGRES_POLL_ACTIVE = 4
} cpkt_postgres_poll_status;

/** Authentication payload kind passed to the custom auth hook. */
typedef enum cpkt_postgres_auth_data_kind {
  CPKT_POSTGRES_AUTH_DATA_PROMPT_OAUTH_DEVICE = 0,
  CPKT_POSTGRES_AUTH_DATA_OAUTH_BEARER_TOKEN = 1
} cpkt_postgres_auth_data_kind;

/** Socket identifier split into words for C89 platform compatibility. */
typedef struct cpkt_postgres_async_socket {
  unsigned long high;
  unsigned long low;
} cpkt_postgres_async_socket;

/** Borrowed device authorization URL, code, and expiry for an OAuth prompt. */
typedef struct cpkt_postgres_oauth_device_prompt {
  const char *verification_uri;
  const char *user_code;
  const char *verification_uri_complete;
  int expires_in;
} cpkt_postgres_oauth_device_prompt;

/** OAuth bearer request and response fields used by the async hook. */
typedef struct cpkt_postgres_oauth_bearer_request
    cpkt_postgres_oauth_bearer_request;
/** Starts or advances an asynchronous OAuth exchange. */
typedef cpkt_postgres_poll_status (*cpkt_postgres_oauth_async)(
    cpkt_postgres_connection *connection,
    cpkt_postgres_oauth_bearer_request *request,
    cpkt_postgres_async_socket *alternate_socket);
/** Releases resources held by an asynchronous OAuth exchange. */
typedef void (*cpkt_postgres_oauth_cleanup)(
    cpkt_postgres_connection *connection,
    cpkt_postgres_oauth_bearer_request *request);

struct cpkt_postgres_oauth_bearer_request {
  const char *openid_configuration;
  const char *scope;
  cpkt_postgres_oauth_async async;
  cpkt_postgres_oauth_cleanup cleanup;
  char *token;
  void *user;
};

/* The data argument is a device prompt or bearer request, by kind. */
/** Receives authentication data during connection establishment. */
typedef int (*cpkt_postgres_auth_data_hook)(
    cpkt_postgres_auth_data_kind kind, cpkt_postgres_connection *connection,
    void *data, void *context);

/** Server command, tuple, copy, pipeline, or error outcome. */
typedef enum cpkt_postgres_result_status {
  CPKT_POSTGRES_RESULT_EMPTY_QUERY = 0,
  CPKT_POSTGRES_RESULT_COMMAND_OK = 1,
  CPKT_POSTGRES_RESULT_TUPLES_OK = 2,
  CPKT_POSTGRES_RESULT_COPY_OUT = 3,
  CPKT_POSTGRES_RESULT_COPY_IN = 4,
  CPKT_POSTGRES_RESULT_BAD_RESPONSE = 5,
  CPKT_POSTGRES_RESULT_NONFATAL_ERROR = 6,
  CPKT_POSTGRES_RESULT_FATAL_ERROR = 7,
  CPKT_POSTGRES_RESULT_COPY_BOTH = 8,
  CPKT_POSTGRES_RESULT_SINGLE_TUPLE = 9,
  CPKT_POSTGRES_RESULT_PIPELINE_SYNC = 10,
  CPKT_POSTGRES_RESULT_PIPELINE_ABORTED = 11,
  CPKT_POSTGRES_RESULT_TUPLES_CHUNK = 12
} cpkt_postgres_result_status;

/** Outcome of one checked asynchronous result retrieval. A drained boundary
 * ends the current native result sequence; pipeline mode can have more later.
 */
typedef enum cpkt_postgres_retrieval_status {
  CPKT_POSTGRES_RETRIEVAL_FAILED = -1,
  CPKT_POSTGRES_RETRIEVAL_DRAINED = 0,
  CPKT_POSTGRES_RETRIEVAL_RESULT = 1
} cpkt_postgres_retrieval_status;

/** Allocation and transport failures reported without another allocation. */
typedef enum cpkt_postgres_retrieval_error_code {
  CPKT_POSTGRES_RETRIEVAL_ERROR_NONE = 0,
  CPKT_POSTGRES_RETRIEVAL_ERROR_ARGUMENT = 1,
  CPKT_POSTGRES_RETRIEVAL_ERROR_CONNECTION = 2,
  CPKT_POSTGRES_RETRIEVAL_ERROR_NOTICE_BINDING = 3,
  CPKT_POSTGRES_RETRIEVAL_ERROR_EVENT_PREPARATION = 4
} cpkt_postgres_retrieval_error_code;

/** Caller-owned, allocation-free diagnostic for checked retrieval. SQLSTATE
 * and server_message come from a discarded native result if available. Empty
 * SQLSTATE means the server supplied none. Text may be truncated. */
typedef struct cpkt_postgres_retrieval_error {
  cpkt_postgres_retrieval_error_code code;
  cpkt_postgres_result_status discarded_status;
  int result_discarded;
  int message_truncated;
  char sqlstate[6];
  char server_message[512];
} cpkt_postgres_retrieval_error;

/** Transaction state of a live connection. */
typedef enum cpkt_postgres_transaction_status {
  CPKT_POSTGRES_TRANSACTION_IDLE = 0,
  CPKT_POSTGRES_TRANSACTION_ACTIVE = 1,
  CPKT_POSTGRES_TRANSACTION_IN_TRANSACTION = 2,
  CPKT_POSTGRES_TRANSACTION_IN_ERROR = 3,
  CPKT_POSTGRES_TRANSACTION_UNKNOWN = 4
} cpkt_postgres_transaction_status;

/** Controls how much detail is included in client error text. */
typedef enum cpkt_postgres_verbosity {
  CPKT_POSTGRES_ERRORS_TERSE = 0,
  CPKT_POSTGRES_ERRORS_DEFAULT = 1,
  CPKT_POSTGRES_ERRORS_VERBOSE = 2,
  CPKT_POSTGRES_ERRORS_SQLSTATE = 3
} cpkt_postgres_verbosity;

/** Controls inclusion of server CONTEXT fields in error text. */
typedef enum cpkt_postgres_context_visibility {
  CPKT_POSTGRES_CONTEXT_NEVER = 0,
  CPKT_POSTGRES_CONTEXT_ERRORS = 1,
  CPKT_POSTGRES_CONTEXT_ALWAYS = 2
} cpkt_postgres_context_visibility;

/** Outcome of a server reachability probe without retaining a connection. */
typedef enum cpkt_postgres_ping_status {
  CPKT_POSTGRES_PING_OK = 0,
  CPKT_POSTGRES_PING_REJECT = 1,
  CPKT_POSTGRES_PING_NO_RESPONSE = 2,
  CPKT_POSTGRES_PING_NO_ATTEMPT = 3
} cpkt_postgres_ping_status;

/** Whether pipeline mode is active or aborted. */
typedef enum cpkt_postgres_pipeline_status {
  CPKT_POSTGRES_PIPELINE_OFF = 0,
  CPKT_POSTGRES_PIPELINE_ON = 1,
  CPKT_POSTGRES_PIPELINE_ABORTED = 2
} cpkt_postgres_pipeline_status;

/** One connection keyword with its current, default, and environment values. */
typedef struct cpkt_postgres_connection_option {
  char *keyword;
  char *environment_variable;
  char *compiled_default;
  char *value;
  char *label;
  char *display_character;
  int display_size;
} cpkt_postgres_connection_option;

/** Column name, type, size, and format for a constructed result. */
typedef struct cpkt_postgres_result_attribute {
  char *name;
  cpkt_postgres_oid table_oid;
  int column_id;
  int format;
  cpkt_postgres_oid type_oid;
  int type_size;
  int type_modifier;
} cpkt_postgres_result_attribute;

/** One binary or integer argument for a server fast-path function. */
typedef struct cpkt_postgres_fastpath_argument {
  int length;
  int is_integer;
  int *integer_pointer;
  int integer_value;
} cpkt_postgres_fastpath_argument;

/** Owned asynchronous notification; free with
 * cpkt_postgres_notification_free(). */
typedef struct cpkt_postgres_notification {
  char *channel;
  int backend_pid;
  char *payload;
} cpkt_postgres_notification;

/** Formatting controls for the legacy result printer. */
typedef struct cpkt_postgres_print_options {
  char header;
  char align;
  char standard;
  char html;
  char expanded;
  char pager;
  char *field_separator;
  char *table_options;
  char *caption;
  char **field_names;
} cpkt_postgres_print_options;

/**
 * Receiver shell owning one PostgreSQL connection. Every method receives this
 * receiver explicitly, for example pg->tx(pg, "select 1"). Close with
 * pg->close(pg). Query results outlive the receiver until freed separately.
 */
struct cpkt_postgres {
  /** Runs a query and returns an owned result, or NULL if none can be produced.
   */
  cpkt_postgres_result *(*tx)(cpkt_postgres *self, const char *query);
  /** Runs a parameterized query and returns an independently owned result. */
  cpkt_postgres_result *(*tx_params)(cpkt_postgres *self, const char *command,
                                     int parameter_count,
                                     const cpkt_postgres_oid *parameter_types,
                                     const char *const *parameter_values,
                                     const int *parameter_lengths,
                                     const int *parameter_formats,
                                     int result_format);
  /** Queues a query; use receive() to collect each owned result. */
  int (*send)(cpkt_postgres *self, const char *query);
  /** Queues a query with typed parameters. */
  int (*send_params)(cpkt_postgres *self, const char *command,
                     int parameter_count,
                     const cpkt_postgres_oid *parameter_types,
                     const char *const *parameter_values,
                     const int *parameter_lengths, const int *parameter_formats,
                     int result_format);
  /** Legacy retrieval: NULL also covers client/facade failure. Prefer
   * cpkt_postgres_receive_checked(self, ...) for async work. */
  cpkt_postgres_result *(*receive)(cpkt_postgres *self);
  /** Reads available network input into connection state. */
  int (*consume)(cpkt_postgres *self);
  /** Reports whether receive() needs more network input. */
  int (*busy)(cpkt_postgres *self);
  /** Attempts to send queued output; retry when the socket is writable. */
  int (*flush)(cpkt_postgres *self);
  /** Changes whether connection I/O may block. */
  int (*set_nonblocking)(cpkt_postgres *self, int enabled);
  /** Returns the current nonblocking I/O setting. */
  int (*is_nonblocking)(const cpkt_postgres *self);
  /** Enables pipeline mode for overlapping requests. */
  int (*begin_pipeline)(cpkt_postgres *self);
  /** Leaves pipeline mode after its queued work is complete. */
  int (*end_pipeline)(cpkt_postgres *self);
  /** Queues a pipeline synchronization point. */
  int (*pipeline_sync)(cpkt_postgres *self);
  /** Sends bytes to an active COPY IN operation. */
  int (*copy_write)(cpkt_postgres *self, const char *bytes, int byte_count);
  /** Ends COPY IN, optionally reporting client-side failure text. */
  int (*copy_finish)(cpkt_postgres *self, const char *error_message);
  /** Reads one COPY OUT chunk; release returned bytes with text_free(). */
  int (*copy_read)(cpkt_postgres *self, char **bytes_out, int asynchronous);
  /** Starts a nonblocking connection reset. */
  int (*reset_start)(cpkt_postgres *self);
  /** Advances a reset after the socket becomes ready. */
  cpkt_postgres_poll_status (*reset_poll)(cpkt_postgres *self);
  /** Resets the connection synchronously; inspect status afterward. */
  void (*reset)(cpkt_postgres *self);
  /** Returns the connection's current state. */
  cpkt_postgres_connection_status (*status)(const cpkt_postgres *self);
  /** Returns connection-owned diagnostic text; do not free it. */
  char *(*error)(const cpkt_postgres *self);
  /** Closes the receiver; separately owned results remain valid. */
  void (*close)(cpkt_postgres *self);
  /** Borrowed native connection; do not free it before close(). */
  cpkt_postgres_connection *connection;
};

#define CPKT_POSTGRES_COPY_RESULT_ATTRIBUTES 1
#define CPKT_POSTGRES_COPY_RESULT_TUPLES 2
#define CPKT_POSTGRES_COPY_RESULT_EVENTS 4
#define CPKT_POSTGRES_COPY_RESULT_NOTICE_HOOKS 8
#define CPKT_POSTGRES_QUERY_PARAMETER_MAXIMUM 65535
#define CPKT_POSTGRES_TRACE_SUPPRESS_TIMESTAMPS 1
#define CPKT_POSTGRES_TRACE_REGRESSION_MODE 2
/* Pinned PostgreSQL 18.6 client capabilities, error-field selectors, and
 * large-object access modes. Values are part of the C89 facade; no native
 * header needed. */
#define CPKT_POSTGRES_HAS_PIPELINING 1
#define CPKT_POSTGRES_HAS_TRACE_FLAGS 1
#define CPKT_POSTGRES_HAS_SSL_LIBRARY_DETECTION 1
#define CPKT_POSTGRES_HAS_ASYNC_CANCEL 1
#define CPKT_POSTGRES_HAS_CHANGE_PASSWORD 1
#define CPKT_POSTGRES_HAS_CHUNK_MODE 1
#define CPKT_POSTGRES_HAS_CLOSE_PREPARED 1
#define CPKT_POSTGRES_HAS_SEND_PIPELINE_SYNC 1
#define CPKT_POSTGRES_HAS_SOCKET_POLL 1
#define CPKT_POSTGRES_HAS_FULL_PROTOCOL_VERSION 1
#define CPKT_POSTGRES_HAS_PROMPT_OAUTH_DEVICE 1
#define CPKT_POSTGRES_DIAG_SEVERITY 'S'
#define CPKT_POSTGRES_DIAG_SEVERITY_NONLOCALIZED 'V'
#define CPKT_POSTGRES_DIAG_SQLSTATE 'C'
#define CPKT_POSTGRES_DIAG_MESSAGE_PRIMARY 'M'
#define CPKT_POSTGRES_DIAG_MESSAGE_DETAIL 'D'
#define CPKT_POSTGRES_DIAG_MESSAGE_HINT 'H'
#define CPKT_POSTGRES_DIAG_STATEMENT_POSITION 'P'
#define CPKT_POSTGRES_DIAG_INTERNAL_POSITION 'p'
#define CPKT_POSTGRES_DIAG_INTERNAL_QUERY 'q'
#define CPKT_POSTGRES_DIAG_CONTEXT 'W'
#define CPKT_POSTGRES_DIAG_SCHEMA_NAME 's'
#define CPKT_POSTGRES_DIAG_TABLE_NAME 't'
#define CPKT_POSTGRES_DIAG_COLUMN_NAME 'c'
#define CPKT_POSTGRES_DIAG_DATATYPE_NAME 'd'
#define CPKT_POSTGRES_DIAG_CONSTRAINT_NAME 'n'
#define CPKT_POSTGRES_DIAG_SOURCE_FILE 'F'
#define CPKT_POSTGRES_DIAG_SOURCE_LINE 'L'
#define CPKT_POSTGRES_DIAG_SOURCE_FUNCTION 'R'
#define CPKT_POSTGRES_LARGE_OBJECT_WRITE 0x00020000
#define CPKT_POSTGRES_LARGE_OBJECT_READ 0x00040000
#define CPKT_POSTGRES_INVALID_OID 0UL
#define CPKT_POSTGRES_OID_MAXIMUM 0xffffffffUL
#define CPKT_POSTGRES_NO_PASSWORD_SUPPLIED_MESSAGE                             \
  "fe_sendauth: no password supplied\n"

/** Opens a connection and returns an owned receiver; close with self->close().
 */
cpkt_postgres *cpkt_postgres_new(const char *connection_info);
/** Opens a connection from NULL-terminated keyword/value arrays. */
cpkt_postgres *cpkt_postgres_new_params(const char *const *keywords,
                                        const char *const *values,
                                        int expand_database_name);
/** Closes and frees a receiver; does not free separately owned query results.
 */
void cpkt_postgres_close(cpkt_postgres *self);

/** Starts a nonblocking connection; finish with connect_poll and then free it.
 */
cpkt_postgres_connection *
cpkt_postgres_connect_start(const char *connection_info);
/** Starts a nonblocking connection from keyword/value arrays. */
cpkt_postgres_connection *
cpkt_postgres_connect_start_params(const char *const *keywords,
                                   const char *const *values,
                                   int expand_database_name);
/** Advances a connection after the socket becomes ready as requested. */
cpkt_postgres_poll_status
cpkt_postgres_connect_poll(cpkt_postgres_connection *connection);
/** Opens an owned connection using a connection string. */
cpkt_postgres_connection *cpkt_postgres_connect(const char *connection_info);
/** Opens an owned connection from keyword/value arrays. */
cpkt_postgres_connection *
cpkt_postgres_connect_params(const char *const *keywords,
                             const char *const *values,
                             int expand_database_name);
/** Opens an owned connection from explicit login fields. */
cpkt_postgres_connection *cpkt_postgres_connect_login(
    const char *host, const char *port, const char *options, const char *tty,
    const char *database, const char *user, const char *password);
/** Closes an owned native connection; separately owned results survive. */
void cpkt_postgres_connection_free(cpkt_postgres_connection *connection);

/** Returns an owned array of connection options; free with
 * connection_options_free(). */
cpkt_postgres_connection_option *cpkt_postgres_connection_defaults(void);
/** Parses connection text into owned options; free options and any error text.
 */
cpkt_postgres_connection_option *
cpkt_postgres_connection_parse(const char *connection_info,
                               char **error_message);
/** Returns an owned snapshot of a connection's options. */
cpkt_postgres_connection_option *
cpkt_postgres_connection_options(cpkt_postgres_connection *connection);
/** Releases an option array returned by the provider. */
void cpkt_postgres_connection_options_free(
    cpkt_postgres_connection_option *options);

/** Starts a nonblocking connection reset; continue with reset_poll(). */
int cpkt_postgres_reset_start(cpkt_postgres_connection *connection);
/** Advances a reset after the socket becomes ready as requested. */
cpkt_postgres_poll_status
cpkt_postgres_reset_poll(cpkt_postgres_connection *connection);
/** Resets a connection synchronously; inspect status afterward. */
void cpkt_postgres_reset(cpkt_postgres_connection *connection);

/** Creates an owned asynchronous cancel connection tied to the query
 * connection. */
cpkt_postgres_cancel_connection *
cpkt_postgres_cancel_connection_create(cpkt_postgres_connection *connection);
/** Starts a cancellation request on a cancel connection. */
int cpkt_postgres_cancel_start(
    cpkt_postgres_cancel_connection *cancel_connection);
/** Completes a cancellation request synchronously. */
int cpkt_postgres_cancel_blocking(
    cpkt_postgres_cancel_connection *cancel_connection);
/** Advances cancellation after the socket becomes ready as requested. */
cpkt_postgres_poll_status
cpkt_postgres_cancel_poll(cpkt_postgres_cancel_connection *cancel_connection);
/** Reports the current state of a cancel connection. */
cpkt_postgres_connection_status cpkt_postgres_cancel_status(
    const cpkt_postgres_cancel_connection *cancel_connection);
/** Returns the descriptor to poll for asynchronous cancellation. */
int cpkt_postgres_cancel_socket(
    const cpkt_postgres_cancel_connection *cancel_connection);
/** Returns error text borrowed from the cancel connection. */
char *cpkt_postgres_cancel_error_message(
    cpkt_postgres_cancel_connection *cancel_connection);
/** Resets a cancel connection for another cancellation request. */
void cpkt_postgres_cancel_reset(
    cpkt_postgres_cancel_connection *cancel_connection);
/** Closes an asynchronous cancel connection. */
void cpkt_postgres_cancel_connection_free(
    cpkt_postgres_cancel_connection *cancel_connection);
/** Creates an owned legacy cancel handle for a live connection. */
cpkt_postgres_cancel *
cpkt_postgres_cancel_create(cpkt_postgres_connection *connection);
/** Releases a legacy cancel handle. */
void cpkt_postgres_cancel_free(cpkt_postgres_cancel *cancel);
/** Requests cancellation and writes failure detail to the supplied buffer. */
int cpkt_postgres_cancel_request(cpkt_postgres_cancel *cancel,
                                 char *error_buffer, int error_buffer_size);
/** Requests cancellation directly from the query connection. */
int cpkt_postgres_request_cancel(cpkt_postgres_connection *connection);

/** Returns the connection-owned database name; do not free it. */
char *cpkt_postgres_database(const cpkt_postgres_connection *connection);
/** Returns the connection-owned user name; do not free it. */
char *cpkt_postgres_user(const cpkt_postgres_connection *connection);
/** Returns the connection-owned password; treat it as sensitive. */
char *cpkt_postgres_password(const cpkt_postgres_connection *connection);
/** Returns the connection-owned host name; do not free it. */
char *cpkt_postgres_host(const cpkt_postgres_connection *connection);
/** Returns the connection-owned numeric host address; do not free it. */
char *cpkt_postgres_host_address(const cpkt_postgres_connection *connection);
/** Returns the connection-owned port text; do not free it. */
char *cpkt_postgres_port(const cpkt_postgres_connection *connection);
/** Returns the connection-owned legacy tty option; do not free it. */
char *cpkt_postgres_tty(const cpkt_postgres_connection *connection);
/** Returns connection-owned startup options; do not free them. */
char *cpkt_postgres_options(const cpkt_postgres_connection *connection);
/** Returns the live connection state; BAD requires error inspection or reset.
 */
cpkt_postgres_connection_status
cpkt_postgres_connection_status_get(const cpkt_postgres_connection *connection);
/** Returns the current transaction state of a connection. */
cpkt_postgres_transaction_status cpkt_postgres_transaction_status_get(
    const cpkt_postgres_connection *connection);
/** Returns a borrowed server parameter value, or NULL when absent. */
const char *
cpkt_postgres_parameter_status(const cpkt_postgres_connection *connection,
                               const char *name);
/** Returns the negotiated PostgreSQL wire protocol major version. */
int cpkt_postgres_protocol_version(const cpkt_postgres_connection *connection);
/** Returns the complete negotiated wire protocol version. */
int cpkt_postgres_full_protocol_version(
    const cpkt_postgres_connection *connection);
/** Returns the server version encoded as an integer. */
int cpkt_postgres_server_version(const cpkt_postgres_connection *connection);
/** Returns connection-owned diagnostic text; do not free it. */
char *cpkt_postgres_error_message(const cpkt_postgres_connection *connection);
/** Returns the live connection socket descriptor for polling. */
int cpkt_postgres_socket(const cpkt_postgres_connection *connection);
/** Returns the server process ID for this connection. */
int cpkt_postgres_backend_pid(const cpkt_postgres_connection *connection);
/** Returns whether the connection is in or has aborted pipeline mode. */
cpkt_postgres_pipeline_status
cpkt_postgres_pipeline_status_get(const cpkt_postgres_connection *connection);
/** Reports whether authentication needs a password. */
int cpkt_postgres_connection_needs_password(
    const cpkt_postgres_connection *connection);
/** Reports whether this connection authenticated with a password. */
int cpkt_postgres_connection_used_password(
    const cpkt_postgres_connection *connection);
/** Reports whether this connection authenticated with GSSAPI. */
int cpkt_postgres_connection_used_gssapi(
    const cpkt_postgres_connection *connection);
/** Returns the current client encoding identifier. */
int cpkt_postgres_client_encoding(const cpkt_postgres_connection *connection);
/** Changes client encoding; returns zero on success. */
int cpkt_postgres_set_client_encoding(cpkt_postgres_connection *connection,
                                      const char *encoding);

/** Reports whether this connection currently uses TLS. */
int cpkt_postgres_tls_in_use(cpkt_postgres_connection *connection);
/** Returns a borrowed implementation TLS object selected by name. */
cpkt_postgres_tls_object *
cpkt_postgres_tls_object_get(cpkt_postgres_connection *connection,
                             const char *object_name);
/** Returns a borrowed TLS attribute value, or NULL if unavailable. */
const char *cpkt_postgres_tls_attribute(cpkt_postgres_connection *connection,
                                        const char *attribute_name);
/** Returns borrowed names of TLS attributes supported by the provider. */
const char *const *
cpkt_postgres_tls_attribute_names(cpkt_postgres_connection *connection);
/** Returns the borrowed OpenSSL connection object, when available. */
cpkt_postgres_tls_object *
cpkt_postgres_openssl_get(cpkt_postgres_connection *connection);
/** Selects whether the client initializes its TLS library automatically. */
void cpkt_postgres_init_tls(int initialize);
/** Selects automatic TLS and crypto-library initialization. */
void cpkt_postgres_init_openssl(int initialize_tls, int initialize_crypto);
/** Reports whether this connection uses GSS-encrypted transport. */
int cpkt_postgres_gss_encryption_in_use(cpkt_postgres_connection *connection);
/** Returns a borrowed transport GSS context, when present. */
cpkt_postgres_gss_context *
cpkt_postgres_gss_context_get(cpkt_postgres_connection *connection);
/** Changes client error detail level and returns the previous level. */
cpkt_postgres_verbosity
cpkt_postgres_set_error_verbosity(cpkt_postgres_connection *connection,
                                  cpkt_postgres_verbosity verbosity);
/** Changes CONTEXT display and returns the previous mode. */
cpkt_postgres_context_visibility cpkt_postgres_set_error_context_visibility(
    cpkt_postgres_connection *connection,
    cpkt_postgres_context_visibility visibility);

/*
 * Callback registration is connection-affine.  The old callback and context
 * are returned when their output pointers are non-NULL, including on failure
 * after finding the previous binding. Otherwise those outputs are NULL.
 * Check the return value: these outputs do not imply successful replacement.
 * Passing a NULL
 * callback selects the native client's no-op callback behavior. A result
 * retains the callback and context present when the client creates it. Keep
 * that context valid until the result is freed. If a result outlives its
 * connection, the callback receives a NULL connection pointer.
 */
/** Replaces the connection notice-result callback. Returns 1 on success, 0
 * for a NULL connection or allocation failure, leaving the previous binding
 * installed. Optional outputs report the previous callback/context, or NULL
 * if none was found. NULL callback disables delivery for future results;
 * existing results and notice-hook copies retain their frozen binding. Keep
 * each context alive until all results using it are freed, even after a
 * successful replacement. A callback for a result outliving its connection
 * receives a NULL connection pointer. */
int cpkt_postgres_set_notice_receiver(
    cpkt_postgres_connection *connection,
    cpkt_postgres_notice_receiver callback, void *context,
    cpkt_postgres_notice_receiver *old_callback_out, void **old_context_out);
/** Replaces the connection notice-text callback. Returns 1 on success, 0
 * for a NULL connection or allocation failure, leaving the previous binding
 * installed. Optional outputs report the previous callback/context, or NULL
 * if none was found. NULL callback disables delivery for future results;
 * existing results and notice-hook copies retain their frozen binding. Keep
 * each context alive until all results using it are freed, even after a
 * successful replacement. A callback for a result outliving its connection
 * receives a NULL connection pointer. */
int cpkt_postgres_set_notice_processor(
    cpkt_postgres_connection *connection,
    cpkt_postgres_notice_processor callback, void *context,
    cpkt_postgres_notice_processor *old_callback_out, void **old_context_out);

/** Sets the process default before constructing connections or worker threads.
 * Startup warnings are delivered even while connection options are parsed.
 * Existing connections without an override use the current process default.
 * NULL restores the provider's stderr default. Keep the context alive while
 * set. */
void cpkt_postgres_set_default_diagnostic_sink(
    cpkt_postgres_diagnostic_sink callback, void *context);

/** Replaces a connection's diagnostic sink after construction. Returns zero
 * only if callback bookkeeping could not be allocated. NULL restores the
 * current process default, or the provider's stderr default if none is
 * installed. Keep context alive until replacement or connection destruction. */
int cpkt_postgres_set_diagnostic_sink(cpkt_postgres_connection *connection,
                                      cpkt_postgres_diagnostic_sink callback,
                                      void *context);

/* Process-global client hooks.  Register them before starting worker threads.
 */
/** Installs a process-wide thread-lock callback and returns the prior callback.
 */
cpkt_postgres_thread_lock
cpkt_postgres_register_thread_lock(cpkt_postgres_thread_lock callback);
/** Installs a process-wide private-key password callback; returns the prior
 * hook. */
cpkt_postgres_ssl_key_password_hook cpkt_postgres_set_ssl_key_password_hook(
    cpkt_postgres_ssl_key_password_hook callback);
/** Returns the currently installed private-key password callback. */
cpkt_postgres_ssl_key_password_hook
cpkt_postgres_get_ssl_key_password_hook(void);
/** Invokes the provider's default private-key password handling. */
int cpkt_postgres_default_ssl_key_password_hook(
    char *buffer, int buffer_size, cpkt_postgres_connection *connection);
/** Installs a process-wide authentication-data callback and context. */
void cpkt_postgres_set_auth_data_hook(cpkt_postgres_auth_data_hook callback,
                                      void *context);
/** Returns the current authentication-data callback and context. */
cpkt_postgres_auth_data_hook
cpkt_postgres_get_auth_data_hook(void **context_out);

/** Writes protocol trace records to the supplied FILE until disabled. */
void cpkt_postgres_trace(cpkt_postgres_connection *connection, FILE *stream);
/** Selects a callback destination for formatted protocol trace records.
 * Replaces a prior file or callback destination. Returns zero only when
 * callback bookkeeping allocation fails. NULL disables tracing. Keep the
 * context alive until replacement or connection destruction. */
int cpkt_postgres_set_trace_sink(cpkt_postgres_connection *connection,
                                 cpkt_postgres_trace_sink callback,
                                 void *context);
/** Stops writing protocol trace records for this connection. */
void cpkt_postgres_untrace(cpkt_postgres_connection *connection);
/** Changes protocol trace formatting flags for this connection. */
void cpkt_postgres_set_trace_flags(cpkt_postgres_connection *connection,
                                   int flags);

/** Runs a query synchronously and returns an owned result. */
cpkt_postgres_result *
cpkt_postgres_execute(cpkt_postgres_connection *connection, const char *query);
/** Runs a parameterized query synchronously; returns an owned result. */
cpkt_postgres_result *cpkt_postgres_execute_params(
    cpkt_postgres_connection *connection, const char *command,
    int parameter_count, const cpkt_postgres_oid *parameter_types,
    const char *const *parameter_values, const int *parameter_lengths,
    const int *parameter_formats, int result_format);
/** Creates a named server-side prepared statement; returns an owned result. */
cpkt_postgres_result *
cpkt_postgres_prepare(cpkt_postgres_connection *connection,
                      const char *statement_name, const char *query,
                      int parameter_count,
                      const cpkt_postgres_oid *parameter_types);
/** Runs a named prepared statement and returns an owned result. */
cpkt_postgres_result *
cpkt_postgres_execute_prepared(cpkt_postgres_connection *connection,
                               const char *statement_name, int parameter_count,
                               const char *const *parameter_values,
                               const int *parameter_lengths,
                               const int *parameter_formats, int result_format);
/** Queues a query for asynchronous execution; retrieve results with
 * get_result(). */
int cpkt_postgres_send_query(cpkt_postgres_connection *connection,
                             const char *query);
/** Queues a parameterized query for asynchronous execution. */
int cpkt_postgres_send_query_params(cpkt_postgres_connection *connection,
                                    const char *command, int parameter_count,
                                    const cpkt_postgres_oid *parameter_types,
                                    const char *const *parameter_values,
                                    const int *parameter_lengths,
                                    const int *parameter_formats,
                                    int result_format);
/** Queues creation of a named prepared statement. */
int cpkt_postgres_send_prepare(cpkt_postgres_connection *connection,
                               const char *statement_name, const char *query,
                               int parameter_count,
                               const cpkt_postgres_oid *parameter_types);
/** Queues execution of a named prepared statement. */
int cpkt_postgres_send_query_prepared(cpkt_postgres_connection *connection,
                                      const char *statement_name,
                                      int parameter_count,
                                      const char *const *parameter_values,
                                      const int *parameter_lengths,
                                      const int *parameter_formats,
                                      int result_format);
/** Requests one row per result for the active async query. */
int cpkt_postgres_set_single_row_mode(cpkt_postgres_connection *connection);
/** Requests results in bounded row chunks for the active async query. */
int cpkt_postgres_set_chunked_rows_mode(cpkt_postgres_connection *connection,
                                        int chunk_size);
/** Legacy retrieval: NULL can mean drainage or an unrecoverable client/facade
 * retrieval failure. Prefer the checked operation for async queries. */
cpkt_postgres_result *
cpkt_postgres_get_result(cpkt_postgres_connection *connection);
/** Retrieve one owned result, a native drainage boundary, or a failure. Call
 * consume_input() and require is_busy() == 0 first for nonblocking connections;
 * retrieval may otherwise block waiting for a complete result. Set
 * *result_out to NULL for both non-result outcomes. error_out may be NULL;
 * when supplied it is fully reset on each call. A discarded result's status,
 * SQLSTATE and server message are copied before destruction. After FAILED,
 * retrying may fetch a later result but cannot recover the discarded one; the
 * caller must not treat that sequence as successful. Drain to a native NULL
 * boundary, reset, or close the connection. Failure is per-call, not latched.
 * The same function accepts receiver->connection without changing shell ABI. */
cpkt_postgres_retrieval_status
cpkt_postgres_get_result_checked(cpkt_postgres_connection *connection,
                                 cpkt_postgres_result **result_out,
                                 cpkt_postgres_retrieval_error *error_out);
/** Receiver form of checked retrieval with the same result and error contract.
 * No receiver shell layout change is required. */
cpkt_postgres_retrieval_status
cpkt_postgres_receive_checked(cpkt_postgres *receiver,
                              cpkt_postgres_result **result_out,
                              cpkt_postgres_retrieval_error *error_out);
/** Reports whether more input is needed before get_result() can proceed. */
int cpkt_postgres_is_busy(cpkt_postgres_connection *connection);
/** Reads pending socket data into the provider's connection state. */
int cpkt_postgres_consume_input(cpkt_postgres_connection *connection);
/** Enables submission of multiple queries without waiting between them. */
int cpkt_postgres_enter_pipeline_mode(cpkt_postgres_connection *connection);
/** Leaves pipeline mode after its queued work is complete. */
int cpkt_postgres_exit_pipeline_mode(cpkt_postgres_connection *connection);
/** Queues a pipeline synchronization point. */
int cpkt_postgres_pipeline_sync(cpkt_postgres_connection *connection);
/** Queues a request for the server to flush pending output. */
int cpkt_postgres_send_flush_request(cpkt_postgres_connection *connection);
/** Queues a protocol synchronization message in pipeline mode. */
int cpkt_postgres_send_pipeline_sync(cpkt_postgres_connection *connection);
/** Returns the next owned asynchronous server notification, if any. */
cpkt_postgres_notification *
cpkt_postgres_notification_next(cpkt_postgres_connection *connection);
/** Releases an owned asynchronous server notification. */
void cpkt_postgres_notification_free(cpkt_postgres_notification *notification);

/** Sends bytes while a COPY IN operation is active. */
int cpkt_postgres_put_copy_data(cpkt_postgres_connection *connection,
                                const char *buffer, int byte_count);
/** Ends COPY IN; optional text reports a client-side failure. */
int cpkt_postgres_put_copy_end(cpkt_postgres_connection *connection,
                               const char *error_message);
/** Receives one COPY OUT chunk; release allocated bytes with text_free(). */
int cpkt_postgres_get_copy_data(cpkt_postgres_connection *connection,
                                char **buffer_out, int asynchronous);
/** Reads one legacy COPY OUT line into a caller buffer. */
int cpkt_postgres_getline(cpkt_postgres_connection *connection, char *buffer,
                          int length);
/** Writes one legacy COPY IN line from caller memory. */
int cpkt_postgres_putline(cpkt_postgres_connection *connection,
                          const char *line);
/** Reads available legacy COPY OUT bytes without blocking. */
int cpkt_postgres_getline_async(cpkt_postgres_connection *connection,
                                char *buffer, int buffer_size);
/** Writes raw bytes in the legacy COPY protocol. */
int cpkt_postgres_put_bytes(cpkt_postgres_connection *connection,
                            const char *buffer, int byte_count);
/** Completes a legacy COPY operation and reports its status. */
int cpkt_postgres_end_copy(cpkt_postgres_connection *connection);
/** Enables or disables nonblocking socket operation. */
int cpkt_postgres_set_nonblocking(cpkt_postgres_connection *connection,
                                  int enabled);
/** Reports whether nonblocking socket operation is active. */
int cpkt_postgres_is_nonblocking(const cpkt_postgres_connection *connection);
/** Reports whether the bundled client supports concurrent calls. */
int cpkt_postgres_is_threadsafe(void);
/** Probes a server without retaining a connection. */
cpkt_postgres_ping_status cpkt_postgres_ping(const char *connection_info);
/** Probes a server using keyword/value connection options. */
cpkt_postgres_ping_status cpkt_postgres_ping_params(const char *const *keywords,
                                                    const char *const *values,
                                                    int expand_database_name);
/** Attempts to send queued output; caller may need socket writability. */
int cpkt_postgres_flush(cpkt_postgres_connection *connection);
/** Calls a server fast-path function and returns an owned result. */
cpkt_postgres_result *cpkt_postgres_fastpath(
    cpkt_postgres_connection *connection, int function_id, int *result_buffer,
    int *result_length_out, int result_is_integer,
    const cpkt_postgres_fastpath_argument *arguments, int argument_count);

/** Returns the server outcome code carried by this result. */
cpkt_postgres_result_status
cpkt_postgres_result_status_get(const cpkt_postgres_result *result);
/** Returns provider-owned text naming a result status; do not free it. */
char *cpkt_postgres_result_status_text(cpkt_postgres_result_status status);
/** Returns result-owned diagnostic text; do not free it. */
char *cpkt_postgres_result_error_message(const cpkt_postgres_result *result);
/** Builds detailed error text; free it with text_free(). */
char *cpkt_postgres_result_verbose_error_message(
    const cpkt_postgres_result *result, cpkt_postgres_verbosity verbosity,
    cpkt_postgres_context_visibility visibility);
/** Returns a borrowed diagnostic field selected by its field code. */
char *cpkt_postgres_result_error_field(const cpkt_postgres_result *result,
                                       int field_code);
/** Returns the number of rows in this result. */
int cpkt_postgres_result_row_count(const cpkt_postgres_result *result);
/** Returns the number of columns in this result. */
int cpkt_postgres_result_field_count(const cpkt_postgres_result *result);
/** Reports whether all result columns use binary format. */
int cpkt_postgres_result_is_binary(const cpkt_postgres_result *result);
/** Returns a borrowed column name for a zero-based field index. */
char *cpkt_postgres_result_field_name(const cpkt_postgres_result *result,
                                      int field_index);
/** Finds a zero-based column index by name; returns -1 if absent. */
int cpkt_postgres_result_field_number(const cpkt_postgres_result *result,
                                      const char *field_name);
/** Returns the originating table OID for a column, if available. */
cpkt_postgres_oid
cpkt_postgres_result_field_table_oid(const cpkt_postgres_result *result,
                                     int field_index);
/** Returns the originating table's column number, if available. */
int cpkt_postgres_result_field_table_column(const cpkt_postgres_result *result,
                                            int field_index);
/** Returns the wire format of a result column: text or binary. */
int cpkt_postgres_result_field_format(const cpkt_postgres_result *result,
                                      int field_index);
/** Returns the type OID of a result column. */
cpkt_postgres_oid
cpkt_postgres_result_field_type(const cpkt_postgres_result *result,
                                int field_index);
/** Returns the declared size of a result column's type. */
int cpkt_postgres_result_field_size(const cpkt_postgres_result *result,
                                    int field_index);
/** Returns the server type modifier for a result column. */
int cpkt_postgres_result_field_modifier(const cpkt_postgres_result *result,
                                        int field_index);
/** Returns result-owned command-tag text; do not free it. */
char *cpkt_postgres_result_command_status(cpkt_postgres_result *result);
/** Returns result-owned text for the legacy inserted OID. */
char *cpkt_postgres_result_oid_status(const cpkt_postgres_result *result);
/** Returns the legacy inserted OID, if the command reports one. */
cpkt_postgres_oid
cpkt_postgres_result_oid_value(const cpkt_postgres_result *result);
/** Returns result-owned text for the affected-row count. */
char *cpkt_postgres_result_command_tuples(cpkt_postgres_result *result);
/** Returns result-owned bytes for a row and column; inspect null and length
 * separately. */
char *cpkt_postgres_result_value(const cpkt_postgres_result *result,
                                 int row_index, int field_index);
/** Returns the byte length of one result cell. */
int cpkt_postgres_result_value_length(const cpkt_postgres_result *result,
                                      int row_index, int field_index);
/** Reports whether one result cell is SQL NULL. */
int cpkt_postgres_result_value_is_null(const cpkt_postgres_result *result,
                                       int row_index, int field_index);
/** Returns the number of parameters of a prepared statement description. */
int cpkt_postgres_result_parameter_count(const cpkt_postgres_result *result);
/** Returns the type OID of a described statement parameter. */
cpkt_postgres_oid
cpkt_postgres_result_parameter_type(const cpkt_postgres_result *result,
                                    int parameter_index);
/** Describes a named prepared statement; returns an owned result. */
cpkt_postgres_result *
cpkt_postgres_describe_prepared(cpkt_postgres_connection *connection,
                                const char *statement_name);
/** Describes a named portal; returns an owned result. */
cpkt_postgres_result *
cpkt_postgres_describe_portal(cpkt_postgres_connection *connection,
                              const char *portal_name);
/** Queues a description request for a named prepared statement. */
int cpkt_postgres_send_describe_prepared(cpkt_postgres_connection *connection,
                                         const char *statement_name);
/** Queues a description request for a named portal. */
int cpkt_postgres_send_describe_portal(cpkt_postgres_connection *connection,
                                       const char *portal_name);
/** Closes a named prepared statement; returns an owned result. */
cpkt_postgres_result *
cpkt_postgres_close_prepared(cpkt_postgres_connection *connection,
                             const char *statement_name);
/** Closes a named portal; returns an owned result. */
cpkt_postgres_result *
cpkt_postgres_close_portal(cpkt_postgres_connection *connection,
                           const char *portal_name);
/** Queues closure of a named prepared statement. */
int cpkt_postgres_send_close_prepared(cpkt_postgres_connection *connection,
                                      const char *statement_name);
/** Queues closure of a named portal. */
int cpkt_postgres_send_close_portal(cpkt_postgres_connection *connection,
                                    const char *portal_name);
/** Releases an owned result and its borrowed rows and fields. */
void cpkt_postgres_result_free(cpkt_postgres_result *result);
/** Releases provider-allocated text and COPY buffers. */
void cpkt_postgres_text_free(char *memory);
/** Releases provider-allocated escaped or decoded bytea data. */
void cpkt_postgres_bytea_free(unsigned char *memory);
/** Constructs an owned result with the requested status. */
cpkt_postgres_result *
cpkt_postgres_result_new_empty(cpkt_postgres_connection *connection,
                               cpkt_postgres_result_status status);
/** Copy native result fields selected by flags. A copy made inside a result
 * event callback may outlive its source and connection; copied notice hooks
 * retain their original callback context until that copy is freed. */
cpkt_postgres_result *
cpkt_postgres_result_copy(const cpkt_postgres_result *source, int flags);
/** Sets column metadata on a constructed result. */
int cpkt_postgres_result_set_attributes(
    cpkt_postgres_result *result, int attribute_count,
    cpkt_postgres_result_attribute *attributes);
/** Allocates bytes from a constructed result's lifetime arena. */
unsigned char *cpkt_postgres_result_allocate(cpkt_postgres_result *result,
                                             size_t byte_count);
/** Returns the memory occupied by a result and its owned data. */
size_t cpkt_postgres_result_memory_size(const cpkt_postgres_result *result);
/** Copies a value into a constructed result cell. */
int cpkt_postgres_result_set_value(cpkt_postgres_result *result, int row_index,
                                   int field_index, char *value,
                                   int value_length);

/** Escapes a string into caller storage using connection encoding. */
size_t
cpkt_postgres_escape_string_connection(cpkt_postgres_connection *connection,
                                       char *destination, const char *source,
                                       size_t source_length, int *error_out);
/** Quotes and escapes a SQL literal; free returned text with text_free(). */
char *cpkt_postgres_escape_literal(cpkt_postgres_connection *connection,
                                   const char *source, size_t source_length);
/** Quotes and escapes a SQL identifier; free returned text with text_free(). */
char *cpkt_postgres_escape_identifier(cpkt_postgres_connection *connection,
                                      const char *source, size_t source_length);
/** Escapes binary data for SQL text; free returned bytes with bytea_free(). */
unsigned char *cpkt_postgres_escape_bytea_connection(
    cpkt_postgres_connection *connection, const unsigned char *source,
    size_t source_length, size_t *destination_length_out);
/** Decodes escaped bytea text; free returned bytes with bytea_free(). */
unsigned char *cpkt_postgres_unescape_bytea(const unsigned char *source,
                                            size_t *destination_length_out);
/** Escapes text without a connection; caller supplies destination storage. */
size_t cpkt_postgres_escape_string(char *destination, const char *source,
                                   size_t source_length);
/** Escapes binary data without a connection; free with bytea_free(). */
unsigned char *cpkt_postgres_escape_bytea(const unsigned char *source,
                                          size_t source_length,
                                          size_t *destination_length_out);

/** Writes a result to a FILE using legacy formatting options. */
void cpkt_postgres_print(FILE *stream, const cpkt_postgres_result *result,
                         const cpkt_postgres_print_options *options);
/** Writes result tuples to a FILE using display formatting. */
void cpkt_postgres_display_tuples(const cpkt_postgres_result *result,
                                  FILE *stream, int fill_align,
                                  const char *field_separator, int print_header,
                                  int quiet);
/** Writes result tuples to a FILE using legacy tuple formatting. */
void cpkt_postgres_print_tuples(const cpkt_postgres_result *result,
                                FILE *stream, int print_attribute_names,
                                int terse_output, int column_width);

/** Opens a server large object and returns a descriptor for this connection. */
int cpkt_postgres_large_object_open(cpkt_postgres_connection *connection,
                                    cpkt_postgres_oid object_oid, int mode);
/** Closes an open large-object descriptor. */
int cpkt_postgres_large_object_close(cpkt_postgres_connection *connection,
                                     int descriptor);
/** Reads large-object bytes into a caller buffer. */
int cpkt_postgres_large_object_read(cpkt_postgres_connection *connection,
                                    int descriptor, char *buffer,
                                    size_t byte_count);
/** Writes caller bytes to an open large object. */
int cpkt_postgres_large_object_write(cpkt_postgres_connection *connection,
                                     int descriptor, const char *buffer,
                                     size_t byte_count);
/** Moves a large-object cursor using a 32-bit offset. */
int cpkt_postgres_large_object_seek(cpkt_postgres_connection *connection,
                                    int descriptor, int offset, int whence);
/** Moves a large-object cursor using a split 64-bit offset. */
cpkt_postgres_i64
cpkt_postgres_large_object_seek64(cpkt_postgres_connection *connection,
                                  int descriptor, cpkt_postgres_i64 offset,
                                  int whence);
/** Creates a large object using the legacy default-OID API. */
cpkt_postgres_oid
cpkt_postgres_large_object_create_legacy(cpkt_postgres_connection *connection,
                                         int mode);
/** Creates a large object with an optional requested OID. */
cpkt_postgres_oid
cpkt_postgres_large_object_create(cpkt_postgres_connection *connection,
                                  cpkt_postgres_oid object_oid);
/** Returns the current 32-bit large-object cursor position. */
int cpkt_postgres_large_object_tell(cpkt_postgres_connection *connection,
                                    int descriptor);
/** Returns the current split 64-bit large-object cursor position. */
cpkt_postgres_i64
cpkt_postgres_large_object_tell64(cpkt_postgres_connection *connection,
                                  int descriptor);
/** Changes large-object length using a 32-bit value. */
int cpkt_postgres_large_object_truncate(cpkt_postgres_connection *connection,
                                        int descriptor, size_t byte_count);
/** Changes large-object length using a split 64-bit value. */
int cpkt_postgres_large_object_truncate64(cpkt_postgres_connection *connection,
                                          int descriptor,
                                          cpkt_postgres_i64 byte_count);
/** Deletes a large object by OID. */
int cpkt_postgres_large_object_unlink(cpkt_postgres_connection *connection,
                                      cpkt_postgres_oid object_oid);
/** Imports a local file into a new server large object. */
cpkt_postgres_oid
cpkt_postgres_large_object_import(cpkt_postgres_connection *connection,
                                  const char *file_name);
/** Imports a local file using a requested large-object OID. */
cpkt_postgres_oid
cpkt_postgres_large_object_import_with_oid(cpkt_postgres_connection *connection,
                                           const char *file_name,
                                           cpkt_postgres_oid object_oid);
/** Exports a server large object to a local file. */
int cpkt_postgres_large_object_export(cpkt_postgres_connection *connection,
                                      cpkt_postgres_oid object_oid,
                                      const char *file_name);

/** Returns the bundled PostgreSQL client version encoded as an integer. */
int cpkt_postgres_library_version(void);
/** Waits for socket readability or writability using provider polling rules. */
int cpkt_postgres_socket_poll(int socket_descriptor, int wait_for_read,
                              int wait_for_write, cpkt_postgres_i64 end_time);
/** Returns the current time in split 64-bit microseconds. */
cpkt_postgres_i64 cpkt_postgres_current_time_microseconds(void);
/** Returns the byte width of the next character in the specified encoding. */
int cpkt_postgres_multibyte_length(const char *text, int encoding);
/** Returns a character's byte width bounded by the input's terminator. */
int cpkt_postgres_multibyte_length_bounded(const char *text, int encoding);
/** Returns the display width of the next encoded character. */
int cpkt_postgres_multibyte_display_length(const char *text, int encoding);
/** Returns the encoding selected from the process environment. */
int cpkt_postgres_environment_encoding(void);
/** Produces an encrypted password string; free it with text_free(). */
char *cpkt_postgres_encrypt_password(const char *password, const char *user);
/** Encrypts a password using the connection's selected algorithm. */
char *cpkt_postgres_encrypt_password_connection(
    cpkt_postgres_connection *connection, const char *password,
    const char *user, const char *algorithm);
/** Requests a server-side password change; returns an owned result. */
cpkt_postgres_result *
cpkt_postgres_change_password(cpkt_postgres_connection *connection,
                              const char *user, const char *password);
/** Looks up a PostgreSQL encoding identifier by name. */
int cpkt_postgres_encoding_from_name(const char *name);
/** Returns a static name for an encoding identifier. */
const char *cpkt_postgres_encoding_name(int encoding);
/** Reports whether an encoding identifier is valid for server storage. */
int cpkt_postgres_server_encoding_is_valid(int encoding);

/** @} */
#endif
