#ifndef CPKT_SQLITE_H
#define CPKT_SQLITE_H

/**
 * @defgroup cpkt_sqlite SQLite C89 facade
 *
 * The supported embedded SQLite interface.  It deliberately does not expose
 * the native upstream header, native SQLite objects, or non-C89 integer types.
 * All operations
 * return a CPKT_SQLITE_* result unless their receiver or return type states a
 * different contract.  Function families below document ownership and
 * callback lifetime; docs/sqlite-c89-facade-spec.md supplies the complete
 * cross-family contract.
 * @{
 */

#include <stdarg.h>
#include <stddef.h>

/** Opaque database receiver; close it with cpkt_sqlite_close(). */
typedef struct cpkt_sqlite cpkt_sqlite;
/** Opaque prepared-statement receiver; finalize it before its database. */
typedef struct cpkt_sqlite_statement cpkt_sqlite_statement;
/** Opaque SQLite value view or owned duplicate; free owned duplicates only. */
typedef struct cpkt_sqlite_value cpkt_sqlite_value;
/** Callback-local SQL function context; never retain it after the callback. */
typedef struct cpkt_sqlite_context cpkt_sqlite_context;
/** Opaque incremental BLOB receiver; close it before its database. */
typedef struct cpkt_sqlite_blob cpkt_sqlite_blob;
/** Opaque online-backup receiver; close it before either database. */
typedef struct cpkt_sqlite_backup cpkt_sqlite_backup;
/** Opaque WAL snapshot; release it with cpkt_sqlite_snapshot_free(). */
typedef struct cpkt_sqlite_snapshot cpkt_sqlite_snapshot;
/** Opaque session-extension receiver; close it before its database. */
typedef struct cpkt_sqlite_session cpkt_sqlite_session;
/** Opaque owned changeset buffer; release it through its free receiver. */
typedef struct cpkt_sqlite_changeset cpkt_sqlite_changeset;
/** Opaque changeset iteration receiver; close it when iteration finishes. */
typedef struct cpkt_sqlite_changeset_iterator cpkt_sqlite_changeset_iterator;
/** Opaque rebase receiver; close it when no longer needed. */
typedef struct cpkt_sqlite_rebaser cpkt_sqlite_rebaser;
/** Opaque SQLite string-builder receiver; close or finish it. */
typedef struct cpkt_sqlite_string cpkt_sqlite_string;
/** Opaque changeset merge receiver; close it when no longer needed. */
typedef struct cpkt_sqlite_changegroup cpkt_sqlite_changegroup;
/** Opaque filename helper; close it after SQLite has consumed it. */
typedef struct cpkt_sqlite_filename cpkt_sqlite_filename;
/** Opaque FTS5 registration receiver scoped to one database. */
typedef struct cpkt_sqlite_fts5_api cpkt_sqlite_fts5_api;
/** Opaque tokenizer shell created only by an FTS5 create callback. */
typedef struct cpkt_sqlite_fts5_tokenizer cpkt_sqlite_fts5_tokenizer;
typedef struct cpkt_sqlite_fts5_found_tokenizer
    cpkt_sqlite_fts5_found_tokenizer;
typedef struct cpkt_sqlite_fts5_tokenizer_instance
    cpkt_sqlite_fts5_tokenizer_instance;
/** Callback-local FTS5 auxiliary context; never retain it. */
typedef struct cpkt_sqlite_fts5_context cpkt_sqlite_fts5_context;
/** FTS5 phrase iterator valid only during its enclosing callback. */
typedef struct cpkt_sqlite_fts5_phrase_iterator
    cpkt_sqlite_fts5_phrase_iterator;
/** Opaque SQLite mutex receiver; close dynamic mutexes when finished. */
typedef struct cpkt_sqlite_mutex cpkt_sqlite_mutex;
/** Application-owned page-cache shell used by global page-cache hooks. */
typedef struct cpkt_sqlite_page_cache cpkt_sqlite_page_cache;
/** Application-owned page shell used by global page-cache hooks. */
typedef struct cpkt_sqlite_page cpkt_sqlite_page;
/** Owned table result returned by cpkt_sqlite_get_table(). */
typedef struct cpkt_sqlite_table cpkt_sqlite_table;
/** Callback record for an RTree geometry constraint. */
typedef struct cpkt_sqlite_rtree_geometry cpkt_sqlite_rtree_geometry;
/** Callback record for an RTree query constraint. */
typedef struct cpkt_sqlite_rtree_query cpkt_sqlite_rtree_query;
/** Module-owned virtual-table shell allocated by module create/connect. */
typedef struct cpkt_sqlite_virtual_table cpkt_sqlite_virtual_table;
/** Module-owned virtual-cursor shell allocated by module open. */
typedef struct cpkt_sqlite_virtual_cursor cpkt_sqlite_virtual_cursor;
/** Callback-local virtual-table planning record. */
typedef struct cpkt_sqlite_index_info cpkt_sqlite_index_info;
/** Complete callback table for a registered virtual-table module. */
typedef struct cpkt_sqlite_module_methods cpkt_sqlite_module_methods;
/** Application-owned virtual filesystem receiver. */
typedef struct cpkt_sqlite_vfs cpkt_sqlite_vfs;
/** Application-owned open-file receiver used by a virtual filesystem. */
typedef struct cpkt_sqlite_file cpkt_sqlite_file;
typedef struct cpkt_sqlite_file_handle cpkt_sqlite_file_handle;
typedef struct cpkt_sqlite_vfs_handle cpkt_sqlite_vfs_handle;
/** Complete callback table for one virtual-filesystem file receiver. */
typedef struct cpkt_sqlite_io_methods cpkt_sqlite_io_methods;
/** Complete callback table for one virtual filesystem receiver. */
typedef struct cpkt_sqlite_vfs_methods cpkt_sqlite_vfs_methods;
/** Process-wide automatic extension receiver. */
typedef struct cpkt_sqlite_auto_extension cpkt_sqlite_auto_extension;

/** Exact signed 64-bit two's-complement bits without a non-C89 scalar. */
typedef struct cpkt_sqlite_i64 {
  unsigned long high;
  unsigned long low;
} cpkt_sqlite_i64;

/** Exact unsigned 64-bit bits without a non-C89 scalar. */
typedef struct cpkt_sqlite_u64 {
  unsigned long high;
  unsigned long low;
} cpkt_sqlite_u64;

/** Global allocator callbacks; install only before SQLite initialization.
 * Allocation callbacks and initialize are required; shutdown may be NULL. */
typedef struct cpkt_sqlite_memory_methods {
  void *(*allocate)(int byte_count);
  void (*free)(void *memory);
  void *(*reallocate)(void *memory, int byte_count);
  int (*size)(void *memory);
  int (*roundup)(int byte_count);
  int (*initialize)(void *context);
  void (*shutdown)(void *context);
  void *context;
} cpkt_sqlite_memory_methods;

/** Custom SQLite mutex backend. Install before initialization and keep its
 * context alive through shutdown. Static mutex state belongs to the backend;
 * dynamic mutex state is passed to free exactly once. */
typedef struct cpkt_sqlite_mutex_methods {
  void *context;
  int (*initialize)(void *context);
  int (*shutdown)(void *context);
  void *(*allocate)(void *context, int mutex_type);
  void (*free)(void *context, void *mutex);
  void (*enter)(void *context, void *mutex);
  int (*try_enter)(void *context, void *mutex);
  void (*leave)(void *context, void *mutex);
  int (*held)(void *context, void *mutex);
  int (*not_held)(void *context, void *mutex);
} cpkt_sqlite_mutex_methods;

/** Output fields from cpkt_sqlite_table_column_metadata(). */
typedef struct cpkt_sqlite_column_metadata {
  const char *declared_type;
  const char *collation;
  int not_null;
  int primary_key;
  int auto_increment;
} cpkt_sqlite_column_metadata;

/** Read-only constraint entry presented to virtual-table best-index callbacks.
 */
typedef struct cpkt_sqlite_index_constraint {
  int column;
  unsigned long operation;
  int usable;
} cpkt_sqlite_index_constraint;

/** Read-only ordering entry presented to virtual-table best-index callbacks. */
typedef struct cpkt_sqlite_index_order {
  int column;
  int descending;
} cpkt_sqlite_index_order;

/** Writable virtual-table best-index constraint-use decision. */
typedef struct cpkt_sqlite_index_constraint_usage {
  int argument_index;
  int omit;
} cpkt_sqlite_index_constraint_usage;

/**
 * SQLite owns calls into these records after global configuration succeeds.
 * The caller owns cache and page state. A page may remain in the cache after
 * unpin(discard=0) and be fetched again; it may also be evicted then without
 * another callback. Allocate the requested extra_byte_count for every page,
 * aligned for pointer-sized values. Its leading bytes are reserved for the
 * facade's native-page adapter and are not application metadata. Do not use
 * cache or page pointers after the backend has destroyed those objects.
 */
typedef struct cpkt_sqlite_page_cache_methods {
  void *context;
  int (*initialize)(void *context);
  void (*shutdown)(void *context);
  cpkt_sqlite_page_cache *(*create)(void *context, int page_byte_count,
                                    int extra_byte_count, int purgeable);
  void (*cache_size)(cpkt_sqlite_page_cache *cache, int suggested_page_count);
  int (*page_count)(cpkt_sqlite_page_cache *cache);
  cpkt_sqlite_page *(*fetch)(cpkt_sqlite_page_cache *cache, unsigned long key,
                             int create_flag);
  void (*unpin)(cpkt_sqlite_page_cache *cache, cpkt_sqlite_page *page,
                int discard);
  void (*rekey)(cpkt_sqlite_page_cache *cache, cpkt_sqlite_page *page,
                unsigned long old_key, unsigned long new_key);
  void (*truncate)(cpkt_sqlite_page_cache *cache, unsigned long limit);
  void (*destroy)(cpkt_sqlite_page_cache *cache);
  void (*shrink)(cpkt_sqlite_page_cache *cache);
} cpkt_sqlite_page_cache_methods;

struct cpkt_sqlite_page_cache {
  void *state;
};

struct cpkt_sqlite_page {
  void *buffer;
  void *extra;
  void *state;
};

struct cpkt_sqlite_rtree_geometry {
  int parameter_count;
  double *parameters;
  void *user;
  void (*user_destroy)(void *context);
};

struct cpkt_sqlite_rtree_query {
  int parameter_count;
  const double *parameters;
  void *user;
  void (*user_destroy)(void *context);
  const double *coordinates;
  const unsigned int *pending_entries;
  int coordinate_count;
  int level;
  int maximum_level;
  cpkt_sqlite_i64 rowid;
  double parent_score;
  int parent_within;
  int within;
  double score;
  cpkt_sqlite_value *const *sql_parameters;
};

/** Receives one exec row; return nonzero to abort evaluation. */
typedef int (*cpkt_sqlite_row_callback)(void *context, int column_count,
                                        const char *const *values,
                                        const char *const *names);
/** Decides whether SQLite retries a busy operation for retry_count. */
typedef int (*cpkt_sqlite_busy_callback)(void *context, int retry_count);
/** Authorizes an SQL action; return the documented SQLite authorizer code. */
typedef int (*cpkt_sqlite_authorizer_callback)(void *context, int action,
                                               const char *detail1,
                                               const char *detail2,
                                               const char *database,
                                               const char *trigger);
/** Compares two encoded collation values. */
typedef int (*cpkt_sqlite_collation_callback)(void *context,
                                              int left_byte_count,
                                              const void *left,
                                              int right_byte_count,
                                              const void *right);
/** Resolves a requested UTF-8 collation name for a database. */
typedef void (*cpkt_sqlite_collation_needed_callback)(
    void *context, cpkt_sqlite *database, unsigned long text_representation,
    const char *name);
/** Resolves a requested UTF-16 collation name for a database. */
typedef void (*cpkt_sqlite_collation_needed16_callback)(
    void *context, cpkt_sqlite *database, unsigned long text_representation,
    const void *name);
/** Selects the auto-vacuum page limit for a schema. */
typedef unsigned long (*cpkt_sqlite_autovacuum_callback)(
    void *context, const char *schema_name, unsigned long database_page_count,
    unsigned long free_page_count, unsigned long page_byte_count);
/** Receives trace/profile events; SQL text is SQLite-owned callback data. */
typedef void (*cpkt_sqlite_trace_callback)(void *context, cpkt_sqlite *database,
                                           unsigned long event, const char *sql,
                                           cpkt_sqlite_u64 elapsed_nanoseconds);
/** Legacy statement-start tracing callback. */
typedef void (*cpkt_sqlite_legacy_trace_callback)(void *context,
                                                  const char *sql);
/** Legacy statement-completion profiling callback. */
typedef void (*cpkt_sqlite_legacy_profile_callback)(
    void *context, const char *sql, cpkt_sqlite_u64 elapsed_nanoseconds);
/**
 * SQLite may coalesce registrations into one notification. `contexts`
 * contains the registered facade contexts for this public callback,
 * including `context`; it is valid only during this callback. The facade
 * invokes this callback for each registration in a coalesced notification.
 */
typedef void (*cpkt_sqlite_unlock_notify_callback)(void *context,
                                                   int context_count,
                                                   void *const *contexts);
typedef int (*cpkt_sqlite_module_connect_callback)(
    void *context, cpkt_sqlite *database, int argument_count,
    const char *const *arguments, cpkt_sqlite_virtual_table **out,
    char **error_out);
typedef int (*cpkt_sqlite_module_best_index_callback)(
    void *context, cpkt_sqlite_virtual_table *table,
    cpkt_sqlite_index_info *index_info);
typedef int (*cpkt_sqlite_module_table_callback)(
    void *context, cpkt_sqlite_virtual_table *table);
typedef int (*cpkt_sqlite_module_open_callback)(
    void *context, cpkt_sqlite_virtual_table *table,
    cpkt_sqlite_virtual_cursor **out);
typedef int (*cpkt_sqlite_module_cursor_callback)(
    void *context, cpkt_sqlite_virtual_cursor *cursor);
typedef int (*cpkt_sqlite_module_filter_callback)(
    void *context, cpkt_sqlite_virtual_cursor *cursor, int index_number,
    const char *index_string, int argument_count,
    cpkt_sqlite_value *const *arguments);
typedef int (*cpkt_sqlite_module_column_callback)(
    void *context, cpkt_sqlite_virtual_cursor *cursor,
    cpkt_sqlite_context *result, int column);
typedef int (*cpkt_sqlite_module_rowid_callback)(
    void *context, cpkt_sqlite_virtual_cursor *cursor,
    cpkt_sqlite_i64 *rowid_out);
typedef int (*cpkt_sqlite_module_update_callback)(
    void *context, cpkt_sqlite_virtual_table *table, int argument_count,
    cpkt_sqlite_value *const *arguments, cpkt_sqlite_i64 *rowid_out);
typedef int (*cpkt_sqlite_module_rename_callback)(
    void *context, cpkt_sqlite_virtual_table *table, const char *name);
typedef int (*cpkt_sqlite_module_savepoint_callback)(
    void *context, cpkt_sqlite_virtual_table *table, int savepoint);
typedef int (*cpkt_sqlite_module_integrity_callback)(
    void *context, cpkt_sqlite_virtual_table *table, const char *schema,
    const char *name, int flags, char **error_out);
/** Identifies a module-owned shadow table name for SQLite schema handling. */
typedef int (*cpkt_sqlite_module_shadow_name_callback)(const char *name);
typedef int (*cpkt_sqlite_rtree_geometry_callback)(
    void *context, cpkt_sqlite_rtree_geometry *geometry, int coordinate_count,
    double *coordinates, int *within_out);
typedef int (*cpkt_sqlite_rtree_query_callback)(void *context,
                                                cpkt_sqlite_rtree_query *query);
typedef int (*cpkt_sqlite_progress_callback)(void *context);
typedef void (*cpkt_sqlite_destroy_callback)(void *context);
typedef int (*cpkt_sqlite_commit_callback)(void *context);
typedef void (*cpkt_sqlite_rollback_callback)(void *context);
typedef void (*cpkt_sqlite_update_callback)(void *context, int operation,
                                            const char *database_name,
                                            const char *table_name,
                                            cpkt_sqlite_i64 row_id);
typedef int (*cpkt_sqlite_wal_callback)(void *context, cpkt_sqlite *database,
                                        const char *database_name,
                                        int page_count);
typedef void (*cpkt_sqlite_preupdate_callback)(
    void *context, cpkt_sqlite *database, int operation,
    const char *database_name, const char *table_name,
    cpkt_sqlite_i64 old_row_id, cpkt_sqlite_i64 new_row_id);
typedef void (*cpkt_sqlite_scalar_callback)(cpkt_sqlite_context *context,
                                            int argument_count,
                                            cpkt_sqlite_value *const *arguments,
                                            void *user_data);
/** Callback/context pairs remain usable by prepared statements until table
 * disconnect. Repeated lookups of the same pair reuse the facade binding. */
typedef int (*cpkt_sqlite_module_find_function_callback)(
    void *context, cpkt_sqlite_virtual_table *table, int argument_count,
    const char *name, cpkt_sqlite_scalar_callback *function_out,
    void **user_data_out);
typedef int (*cpkt_sqlite_changeset_filter_callback)(void *context,
                                                     const char *table_name);
typedef int (*cpkt_sqlite_changeset_iterator_filter_callback)(
    void *context, cpkt_sqlite_changeset_iterator *iterator);
typedef int (*cpkt_sqlite_session_filter_callback)(void *context,
                                                   const char *table_name);
typedef int (*cpkt_sqlite_changeset_conflict_callback)(
    void *context, int conflict_kind, cpkt_sqlite_changeset_iterator *iterator);
typedef int (*cpkt_sqlite_stream_input_callback)(void *context, void *buffer,
                                                 int *byte_count);
typedef int (*cpkt_sqlite_stream_output_callback)(void *context,
                                                  const void *buffer,
                                                  int byte_count);
typedef void (*cpkt_sqlite_log_callback)(void *context, int error_code,
                                         const char *message);
/** Receives a legacy global memory-pressure notification. */
typedef void (*cpkt_sqlite_memory_alarm_callback)(
    void *context, cpkt_sqlite_i64 requested_bytes,
    int prior_allocation_failed);
/** Initializes one newly opened database through the C89 facade. */
typedef int (*cpkt_sqlite_auto_extension_callback)(cpkt_sqlite *database,
                                                   char **error_out,
                                                   void *context);
typedef int (*cpkt_sqlite_fts5_token_callback)(void *context, int flags,
                                               const char *token,
                                               int token_byte_count,
                                               int start_offset,
                                               int end_offset);
typedef int (*cpkt_sqlite_fts5_tokenizer_create_callback)(
    void *context, const char *const *arguments, int argument_count,
    cpkt_sqlite_fts5_tokenizer **out);
typedef void (*cpkt_sqlite_fts5_tokenizer_destroy_callback)(
    cpkt_sqlite_fts5_tokenizer *tokenizer, void *context);
typedef int (*cpkt_sqlite_fts5_tokenizer_tokenize_callback)(
    cpkt_sqlite_fts5_tokenizer *tokenizer, void *context, int flags,
    const char *text, int text_byte_count, const char *locale,
    int locale_byte_count, cpkt_sqlite_fts5_token_callback token);
typedef int (*cpkt_sqlite_fts5_query_phrase_callback)(
    cpkt_sqlite_fts5_context *context, void *user_data);
typedef void (*cpkt_sqlite_fts5_auxiliary_callback)(
    cpkt_sqlite_fts5_context *fts_context, cpkt_sqlite_context *sql_context,
    int argument_count, cpkt_sqlite_value *const *arguments, void *user_data);
/** Opens a file receiver. Set file->methods before returning success. */
typedef int (*cpkt_sqlite_vfs_open_callback)(cpkt_sqlite_vfs *vfs,
                                             const char *name,
                                             cpkt_sqlite_file *file, int flags,
                                             int *flags_out);
typedef int (*cpkt_sqlite_vfs_delete_callback)(cpkt_sqlite_vfs *vfs,
                                               const char *name,
                                               int sync_directory);
typedef int (*cpkt_sqlite_vfs_access_callback)(cpkt_sqlite_vfs *vfs,
                                               const char *name, int flags,
                                               int *result_out);
typedef int (*cpkt_sqlite_vfs_full_path_callback)(cpkt_sqlite_vfs *vfs,
                                                  const char *name,
                                                  int output_byte_count,
                                                  char *output);
typedef void *(*cpkt_sqlite_vfs_dl_open_callback)(cpkt_sqlite_vfs *vfs,
                                                  const char *filename);
typedef void (*cpkt_sqlite_vfs_dl_error_callback)(cpkt_sqlite_vfs *vfs,
                                                  int byte_count,
                                                  char *message);
typedef void (*cpkt_sqlite_vfs_symbol_callback)(void);
typedef cpkt_sqlite_vfs_symbol_callback (*cpkt_sqlite_vfs_dl_symbol_callback)(
    cpkt_sqlite_vfs *vfs, void *handle, const char *symbol);
typedef void (*cpkt_sqlite_vfs_dl_close_callback)(cpkt_sqlite_vfs *vfs,
                                                  void *handle);
typedef int (*cpkt_sqlite_vfs_randomness_callback)(cpkt_sqlite_vfs *vfs,
                                                   int byte_count,
                                                   char *output);
typedef int (*cpkt_sqlite_vfs_sleep_callback)(cpkt_sqlite_vfs *vfs,
                                              int microseconds);
typedef int (*cpkt_sqlite_vfs_current_time_callback)(cpkt_sqlite_vfs *vfs,
                                                     double *julian_day_out);
typedef int (*cpkt_sqlite_vfs_last_error_callback)(cpkt_sqlite_vfs *vfs,
                                                   int byte_count,
                                                   char *message);
typedef int (*cpkt_sqlite_vfs_current_time_i64_callback)(
    cpkt_sqlite_vfs *vfs, cpkt_sqlite_i64 *milliseconds_out);
typedef int (*cpkt_sqlite_vfs_set_system_call_callback)(
    cpkt_sqlite_vfs *vfs, const char *name,
    cpkt_sqlite_vfs_symbol_callback symbol);
typedef cpkt_sqlite_vfs_symbol_callback (
    *cpkt_sqlite_vfs_get_system_call_callback)(cpkt_sqlite_vfs *vfs,
                                               const char *name);
typedef const char *(*cpkt_sqlite_vfs_next_system_call_callback)(
    cpkt_sqlite_vfs *vfs, const char *name);
typedef int (*cpkt_sqlite_file_close_callback)(cpkt_sqlite_file *file);
typedef int (*cpkt_sqlite_file_read_callback)(cpkt_sqlite_file *file,
                                              void *buffer, int byte_count,
                                              cpkt_sqlite_i64 offset);
typedef int (*cpkt_sqlite_file_write_callback)(cpkt_sqlite_file *file,
                                               const void *buffer,
                                               int byte_count,
                                               cpkt_sqlite_i64 offset);
typedef int (*cpkt_sqlite_file_truncate_callback)(cpkt_sqlite_file *file,
                                                  cpkt_sqlite_i64 size);
typedef int (*cpkt_sqlite_file_sync_callback)(cpkt_sqlite_file *file,
                                              int flags);
typedef int (*cpkt_sqlite_file_size_callback)(cpkt_sqlite_file *file,
                                              cpkt_sqlite_i64 *size_out);
typedef int (*cpkt_sqlite_file_lock_callback)(cpkt_sqlite_file *file,
                                              int level);
typedef int (*cpkt_sqlite_file_reserved_lock_callback)(cpkt_sqlite_file *file,
                                                       int *result_out);
/** SIZE_HINT, MMAP_SIZE, and SIZE_LIMIT use cpkt_sqlite_i64 * payloads.
 * Other operation payloads retain their SQLite-defined types. */
typedef int (*cpkt_sqlite_file_control_callback)(cpkt_sqlite_file *file,
                                                 int operation, void *argument);
typedef int (*cpkt_sqlite_file_sector_size_callback)(cpkt_sqlite_file *file);
typedef int (*cpkt_sqlite_file_characteristics_callback)(
    cpkt_sqlite_file *file);
typedef int (*cpkt_sqlite_file_shm_map_callback)(cpkt_sqlite_file *file,
                                                 int page, int page_byte_count,
                                                 int extend,
                                                 void volatile **out);
typedef int (*cpkt_sqlite_file_shm_lock_callback)(cpkt_sqlite_file *file,
                                                  int offset, int count,
                                                  int flags);
typedef void (*cpkt_sqlite_file_shm_barrier_callback)(cpkt_sqlite_file *file);
typedef int (*cpkt_sqlite_file_shm_unmap_callback)(cpkt_sqlite_file *file,
                                                   int delete_flag);
typedef int (*cpkt_sqlite_file_fetch_callback)(cpkt_sqlite_file *file,
                                               cpkt_sqlite_i64 offset,
                                               int byte_count, void **out);
typedef int (*cpkt_sqlite_file_unfetch_callback)(cpkt_sqlite_file *file,
                                                 cpkt_sqlite_i64 offset,
                                                 void *memory);

enum {
  CPKT_SQLITE_OK = 0,
  CPKT_SQLITE_ERROR = 1,
  CPKT_SQLITE_INTERNAL = 2,
  CPKT_SQLITE_PERM = 3,
  CPKT_SQLITE_ABORT = 4,
  CPKT_SQLITE_BUSY = 5,
  CPKT_SQLITE_LOCKED = 6,
  CPKT_SQLITE_NOMEM = 7,
  CPKT_SQLITE_READONLY = 8,
  CPKT_SQLITE_INTERRUPT = 9,
  CPKT_SQLITE_IOERR = 10,
  CPKT_SQLITE_CORRUPT = 11,
  CPKT_SQLITE_NOTFOUND = 12,
  CPKT_SQLITE_FULL = 13,
  CPKT_SQLITE_CANTOPEN = 14,
  CPKT_SQLITE_PROTOCOL = 15,
  CPKT_SQLITE_EMPTY = 16,
  CPKT_SQLITE_SCHEMA = 17,
  CPKT_SQLITE_TOOBIG = 18,
  CPKT_SQLITE_CONSTRAINT = 19,
  CPKT_SQLITE_MISMATCH = 20,
  CPKT_SQLITE_MISUSE = 21,
  CPKT_SQLITE_NOLFS = 22,
  CPKT_SQLITE_AUTH = 23,
  CPKT_SQLITE_FORMAT = 24,
  CPKT_SQLITE_RANGE = 25,
  CPKT_SQLITE_NOTADB = 26,
  CPKT_SQLITE_NOTICE = 27,
  CPKT_SQLITE_WARNING = 28,
  CPKT_SQLITE_ROW = 100,
  CPKT_SQLITE_DONE = 101
};

enum {
  CPKT_SQLITE_INTEGER = 1,
  CPKT_SQLITE_FLOAT = 2,
  CPKT_SQLITE_TEXT = 3,
  CPKT_SQLITE_BLOB = 4,
  CPKT_SQLITE_NULL = 5
};

enum {
  CPKT_SQLITE_UTF8 = 1,
  CPKT_SQLITE_UTF16LE = 2,
  CPKT_SQLITE_UTF16BE = 3,
  CPKT_SQLITE_UTF16 = 4,
  CPKT_SQLITE_UTF16_ALIGNED = 8,
  CPKT_SQLITE_ANY = 5,
  CPKT_SQLITE_UTF16_ALIGNED16 = 8,
  CPKT_SQLITE_DETERMINISTIC = 2048,
  CPKT_SQLITE_DIRECTONLY = 524288,
  CPKT_SQLITE_SUBTYPE = 1048576,
  CPKT_SQLITE_INNOCUOUS = 2097152,
  CPKT_SQLITE_RESULT_SUBTYPE = 16777216,
  CPKT_SQLITE_SELFORDER1 = 33554432
};

enum {
  CPKT_SQLITE_SERIALIZE_NOCOPY = 1,
  CPKT_SQLITE_DESERIALIZE_FREE_ON_CLOSE = 1,
  CPKT_SQLITE_DESERIALIZE_RESIZEABLE = 2,
  CPKT_SQLITE_DESERIALIZE_READONLY = 4
};

enum {
  CPKT_SQLITE_CARRAY_INT32 = 0,
  CPKT_SQLITE_CARRAY_I64 = 1,
  CPKT_SQLITE_CARRAY_DOUBLE = 2,
  CPKT_SQLITE_CARRAY_TEXT = 3,
  CPKT_SQLITE_CARRAY_BLOB = 4
};

enum {
  CPKT_SQLITE_CHANGESET_OMIT = 0,
  CPKT_SQLITE_CHANGESET_REPLACE = 1,
  CPKT_SQLITE_CHANGESET_ABORT = 2,
  CPKT_SQLITE_CHANGESET_DATA = 1,
  CPKT_SQLITE_CHANGESET_NOTFOUND = 2,
  CPKT_SQLITE_CHANGESET_CONFLICT = 3,
  CPKT_SQLITE_CHANGESET_CONSTRAINT = 4,
  CPKT_SQLITE_CHANGESET_FOREIGN_KEY = 5,
  CPKT_SQLITE_CHANGESET_START_INVERT = 2,
  CPKT_SQLITE_CHANGESET_APPLY_NO_SAVEPOINT = 1,
  CPKT_SQLITE_CHANGESET_APPLY_INVERT = 2,
  CPKT_SQLITE_CHANGESET_APPLY_IGNORE_NOOP = 4,
  CPKT_SQLITE_CHANGESET_APPLY_FK_NO_ACTION = 8,
  CPKT_SQLITE_CHANGESET_APPLY_NO_UPDATE_LOOP = 16
};

enum {
  CPKT_SQLITE_OPEN_READONLY = 1,
  CPKT_SQLITE_OPEN_READWRITE = 2,
  CPKT_SQLITE_OPEN_CREATE = 4,
  CPKT_SQLITE_OPEN_URI = 64,
  CPKT_SQLITE_OPEN_MEMORY = 128,
  CPKT_SQLITE_OPEN_NOMUTEX = 32768,
  CPKT_SQLITE_OPEN_FULLMUTEX = 65536,
  CPKT_SQLITE_OPEN_SHAREDCACHE = 131072,
  CPKT_SQLITE_OPEN_PRIVATECACHE = 262144,
  CPKT_SQLITE_OPEN_EXRESCODE = 33554432
};

enum {
  CPKT_SQLITE_PREPARE_PERSISTENT = 1,
  CPKT_SQLITE_PREPARE_NORMALIZE = 2,
  CPKT_SQLITE_PREPARE_NO_VTAB = 4,
  CPKT_SQLITE_PREPARE_DONT_LOG = 16
};

enum {
  CPKT_SQLITE_CHECKPOINT_NOOP = -1,
  CPKT_SQLITE_CHECKPOINT_PASSIVE = 0,
  CPKT_SQLITE_CHECKPOINT_FULL = 1,
  CPKT_SQLITE_CHECKPOINT_RESTART = 2,
  CPKT_SQLITE_CHECKPOINT_TRUNCATE = 3
};

enum {
  CPKT_SQLITE_DATABASE_CONFIG_MAIN_NAME = 1000,
  CPKT_SQLITE_DATABASE_CONFIG_LOOKASIDE = 1001,
  CPKT_SQLITE_DATABASE_CONFIG_ENABLE_FOREIGN_KEYS = 1002,
  CPKT_SQLITE_DATABASE_CONFIG_ENABLE_TRIGGERS = 1003,
  CPKT_SQLITE_DATABASE_CONFIG_ENABLE_FTS3_TOKENIZER = 1004,
  CPKT_SQLITE_DATABASE_CONFIG_ENABLE_EXTENSION_LOADING = 1005,
  CPKT_SQLITE_DATABASE_CONFIG_NO_CHECKPOINT_ON_CLOSE = 1006,
  CPKT_SQLITE_DATABASE_CONFIG_ENABLE_QPSG = 1007,
  CPKT_SQLITE_DATABASE_CONFIG_TRIGGER_EXPLAIN = 1008,
  CPKT_SQLITE_DATABASE_CONFIG_RESET_DATABASE = 1009,
  CPKT_SQLITE_DATABASE_CONFIG_DEFENSIVE = 1010,
  CPKT_SQLITE_DATABASE_CONFIG_WRITABLE_SCHEMA = 1011,
  CPKT_SQLITE_DATABASE_CONFIG_LEGACY_ALTER_TABLE = 1012,
  CPKT_SQLITE_DATABASE_CONFIG_DQS_DML = 1013,
  CPKT_SQLITE_DATABASE_CONFIG_DQS_DDL = 1014,
  CPKT_SQLITE_DATABASE_CONFIG_ENABLE_VIEW = 1015,
  CPKT_SQLITE_DATABASE_CONFIG_LEGACY_FILE_FORMAT = 1016,
  CPKT_SQLITE_DATABASE_CONFIG_TRUSTED_SCHEMA = 1017,
  CPKT_SQLITE_DATABASE_CONFIG_STATEMENT_SCAN_STATUS = 1018,
  CPKT_SQLITE_DATABASE_CONFIG_REVERSE_SCAN_ORDER = 1019,
  CPKT_SQLITE_DATABASE_CONFIG_ENABLE_ATTACH_CREATE = 1020,
  CPKT_SQLITE_DATABASE_CONFIG_ENABLE_ATTACH_WRITE = 1021,
  CPKT_SQLITE_DATABASE_CONFIG_ENABLE_COMMENTS = 1022,
  CPKT_SQLITE_DATABASE_CONFIG_FLOATING_POINT_DIGITS = 1023
};

enum {
  CPKT_SQLITE_GLOBAL_CONFIG_SINGLE_THREAD = 1,
  CPKT_SQLITE_GLOBAL_CONFIG_MULTI_THREAD = 2,
  CPKT_SQLITE_GLOBAL_CONFIG_SERIALIZED = 3,
  CPKT_SQLITE_GLOBAL_CONFIG_MALLOC = 4,
  CPKT_SQLITE_GLOBAL_CONFIG_GET_MALLOC = 5,
  CPKT_SQLITE_GLOBAL_CONFIG_PAGE_CACHE = 7,
  CPKT_SQLITE_GLOBAL_CONFIG_HEAP = 8,
  CPKT_SQLITE_GLOBAL_CONFIG_MEMORY_STATUS = 9,
  CPKT_SQLITE_GLOBAL_CONFIG_MUTEX = 10,
  CPKT_SQLITE_GLOBAL_CONFIG_GET_MUTEX = 11,
  CPKT_SQLITE_GLOBAL_CONFIG_LOOKASIDE = 13,
  CPKT_SQLITE_GLOBAL_CONFIG_LOG = 16,
  CPKT_SQLITE_GLOBAL_CONFIG_URI = 17,
  CPKT_SQLITE_GLOBAL_CONFIG_PCACHE2 = 18,
  CPKT_SQLITE_GLOBAL_CONFIG_GET_PCACHE2 = 19,
  CPKT_SQLITE_GLOBAL_CONFIG_COVERING_INDEX_SCAN = 20,
  CPKT_SQLITE_GLOBAL_CONFIG_MMAP_SIZE = 22,
  CPKT_SQLITE_GLOBAL_CONFIG_WIN32_HEAP_SIZE = 23,
  CPKT_SQLITE_GLOBAL_CONFIG_PCACHE_HEADER_SIZE = 24,
  CPKT_SQLITE_GLOBAL_CONFIG_PMASZ = 25,
  CPKT_SQLITE_GLOBAL_CONFIG_STATEMENT_JOURNAL_SPILL = 26,
  CPKT_SQLITE_GLOBAL_CONFIG_SMALL_MALLOC = 27,
  CPKT_SQLITE_GLOBAL_CONFIG_SORTER_REFERENCE_SIZE = 28,
  CPKT_SQLITE_GLOBAL_CONFIG_MEMORY_DATABASE_MAX_SIZE = 29,
  CPKT_SQLITE_GLOBAL_CONFIG_ROWID_IN_VIEW = 30
};

enum {
  CPKT_SQLITE_MUTEX_FAST = 0,
  CPKT_SQLITE_MUTEX_RECURSIVE = 1,
  CPKT_SQLITE_MUTEX_STATIC_MAIN = 2,
  CPKT_SQLITE_MUTEX_STATIC_MEMORY = 3,
  CPKT_SQLITE_MUTEX_STATIC_OPEN = 4,
  CPKT_SQLITE_MUTEX_STATIC_PRNG = 5,
  CPKT_SQLITE_MUTEX_STATIC_LRU = 6,
  CPKT_SQLITE_MUTEX_STATIC_PAGE_CACHE = 7,
  CPKT_SQLITE_MUTEX_STATIC_APPLICATION_1 = 8,
  CPKT_SQLITE_MUTEX_STATIC_APPLICATION_2 = 9,
  CPKT_SQLITE_MUTEX_STATIC_APPLICATION_3 = 10,
  CPKT_SQLITE_MUTEX_STATIC_VFS_1 = 11,
  CPKT_SQLITE_MUTEX_STATIC_VFS_2 = 12,
  CPKT_SQLITE_MUTEX_STATIC_VFS_3 = 13
};

enum {
  CPKT_SQLITE_FTS5_TOKENIZE_QUERY = 1,
  CPKT_SQLITE_FTS5_TOKENIZE_PREFIX = 2,
  CPKT_SQLITE_FTS5_TOKENIZE_DOCUMENT = 4,
  CPKT_SQLITE_FTS5_TOKENIZE_AUXILIARY = 8,
  CPKT_SQLITE_FTS5_TOKEN_COLOCATED = 1
};

enum {
  CPKT_SQLITE_SCAN_STATUS_LOOP_COUNT = 0,
  CPKT_SQLITE_SCAN_STATUS_VISIT_COUNT = 1,
  CPKT_SQLITE_SCAN_STATUS_ESTIMATE = 2,
  CPKT_SQLITE_SCAN_STATUS_NAME = 3,
  CPKT_SQLITE_SCAN_STATUS_EXPLAIN = 4,
  CPKT_SQLITE_SCAN_STATUS_SELECT_ID = 5,
  CPKT_SQLITE_SCAN_STATUS_PARENT_ID = 6,
  CPKT_SQLITE_SCAN_STATUS_CYCLE_COUNT = 7,
  CPKT_SQLITE_SCAN_STATUS_COMPLEX = 1
};

enum {
  CPKT_SQLITE_TRACE_STATEMENT = 1,
  CPKT_SQLITE_TRACE_PROFILE = 2,
  CPKT_SQLITE_TRACE_ROW = 4,
  CPKT_SQLITE_TRACE_CLOSE = 8
};

enum {
  CPKT_SQLITE_RTREE_NOT_WITHIN = 0,
  CPKT_SQLITE_RTREE_PARTLY_WITHIN = 1,
  CPKT_SQLITE_RTREE_FULLY_WITHIN = 2
};

enum {
  CPKT_SQLITE_STATEMENT_STATUS_FULLSCAN_STEP = 1,
  CPKT_SQLITE_STATEMENT_STATUS_SORT = 2,
  CPKT_SQLITE_STATEMENT_STATUS_AUTOINDEX = 3,
  CPKT_SQLITE_STATEMENT_STATUS_VM_STEP = 4,
  CPKT_SQLITE_STATEMENT_STATUS_REPREPARE = 5,
  CPKT_SQLITE_STATEMENT_STATUS_RUN = 6,
  CPKT_SQLITE_STATEMENT_STATUS_FILTER_MISS = 7,
  CPKT_SQLITE_STATEMENT_STATUS_FILTER_HIT = 8,
  CPKT_SQLITE_STATEMENT_STATUS_MEMORY_USED = 99
};

enum {
  CPKT_SQLITE_VTAB_CONSTRAINT_SUPPORT = 1,
  CPKT_SQLITE_VTAB_INNOCUOUS = 2,
  CPKT_SQLITE_VTAB_DIRECTONLY = 3,
  CPKT_SQLITE_VTAB_USES_ALL_SCHEMAS = 4
};

/* Explicit receiver shell: db->tx(db, sql, callback, context); db->close(db).
 */
struct cpkt_sqlite {
  int (*tx)(cpkt_sqlite *self, const char *sql,
            cpkt_sqlite_row_callback callback, void *context);
  int (*prepare)(cpkt_sqlite *self, const char *sql, int byte_count,
                 unsigned long flags, cpkt_sqlite_statement **statement_out,
                 const char **tail_out);
  int (*prepare16)(cpkt_sqlite *self, const void *sql, int byte_count,
                   unsigned long flags, cpkt_sqlite_statement **statement_out,
                   const void **tail_out);
  int (*busy_timeout)(cpkt_sqlite *self, int milliseconds);
  int (*extended_result_codes)(cpkt_sqlite *self, int enabled);
  int (*wal_auto_checkpoint)(cpkt_sqlite *self, int page_count);
  int (*wal_checkpoint)(cpkt_sqlite *self, const char *schema, int mode,
                        int *log_frames_out, int *checkpointed_frames_out);
  int (*load_extension)(cpkt_sqlite *self, const char *path,
                        const char *entry_point, char **error_out);
  int (*enable_extension_loading)(cpkt_sqlite *self, int enabled);
  int (*unlock_notify)(cpkt_sqlite *self,
                       cpkt_sqlite_unlock_notify_callback callback,
                       void *context);
  int (*overload_function)(cpkt_sqlite *self, const char *name,
                           int argument_count);
  int (*changes)(const cpkt_sqlite *self);
  cpkt_sqlite_i64 (*last_insert_rowid)(const cpkt_sqlite *self);
  int (*preupdate_blob_write)(const cpkt_sqlite *self);
  const char *(*error)(const cpkt_sqlite *self);
  int (*error_code)(const cpkt_sqlite *self);
  int (*set_error)(cpkt_sqlite *self, int code, const char *message);
  void (*close)(cpkt_sqlite *self);
  void *database;
  void *facade_state;
};

struct cpkt_sqlite_statement {
  /* Borrowed database receiver; valid until this statement is finalized. */
  cpkt_sqlite *(*database)(const cpkt_sqlite_statement *self);
  int (*bind_null)(cpkt_sqlite_statement *self, int parameter_index);
  int (*bind_int)(cpkt_sqlite_statement *self, int parameter_index, int value);
  int (*bind_i64)(cpkt_sqlite_statement *self, int parameter_index,
                  cpkt_sqlite_i64 value);
  int (*bind_double)(cpkt_sqlite_statement *self, int parameter_index,
                     double value);
  int (*bind_text)(cpkt_sqlite_statement *self, int parameter_index,
                   const char *value, int byte_count);
  int (*bind_text16)(cpkt_sqlite_statement *self, int parameter_index,
                     const void *value, int byte_count);
  int (*bind_text_u64)(cpkt_sqlite_statement *self, int parameter_index,
                       const void *value, cpkt_sqlite_u64 byte_count,
                       unsigned long encoding);
  int (*bind_text_owned)(cpkt_sqlite_statement *self, int parameter_index,
                         const char *value, int byte_count,
                         cpkt_sqlite_destroy_callback destroy);
  int (*bind_blob)(cpkt_sqlite_statement *self, int parameter_index,
                   const void *value, int byte_count);
  int (*bind_blob_u64)(cpkt_sqlite_statement *self, int parameter_index,
                       const void *value, cpkt_sqlite_u64 byte_count);
  int (*bind_blob_owned)(cpkt_sqlite_statement *self, int parameter_index,
                         const void *value, int byte_count,
                         cpkt_sqlite_destroy_callback destroy);
  int (*bind_zero_blob)(cpkt_sqlite_statement *self, int parameter_index,
                        int byte_count);
  int (*bind_zero_blob_u64)(cpkt_sqlite_statement *self, int parameter_index,
                            cpkt_sqlite_u64 byte_count);
  int (*bind_value)(cpkt_sqlite_statement *self, int parameter_index,
                    const cpkt_sqlite_value *value);
  int (*bind_pointer)(cpkt_sqlite_statement *self, int parameter_index,
                      void *value, const char *type_name,
                      cpkt_sqlite_destroy_callback destroy);
  int (*bind_carray)(cpkt_sqlite_statement *self, int parameter_index,
                     void *data, int element_count, int element_type,
                     cpkt_sqlite_destroy_callback destroy);
  int (*bind_carray_with_context)(cpkt_sqlite_statement *self,
                                  int parameter_index, void *data,
                                  int element_count, int element_type,
                                  cpkt_sqlite_destroy_callback destroy,
                                  void *destroy_context);
  int (*step)(cpkt_sqlite_statement *self);
  int (*reset)(cpkt_sqlite_statement *self);
  int (*clear_bindings)(cpkt_sqlite_statement *self);
  int (*parameter_count)(const cpkt_sqlite_statement *self);
  const char *(*parameter_name)(const cpkt_sqlite_statement *self,
                                int parameter_index);
  int (*parameter_index)(const cpkt_sqlite_statement *self, const char *name);
  int (*column_count)(const cpkt_sqlite_statement *self);
  const char *(*column_name)(const cpkt_sqlite_statement *self, int column);
  const void *(*column_name16)(const cpkt_sqlite_statement *self, int column);
  int (*column_type)(const cpkt_sqlite_statement *self, int column);
  int (*column_int)(const cpkt_sqlite_statement *self, int column);
  cpkt_sqlite_i64 (*column_i64)(const cpkt_sqlite_statement *self, int column);
  double (*column_double)(const cpkt_sqlite_statement *self, int column);
  const unsigned char *(*column_text)(const cpkt_sqlite_statement *self,
                                      int column);
  const void *(*column_text16)(const cpkt_sqlite_statement *self, int column);
  const void *(*column_blob)(const cpkt_sqlite_statement *self, int column);
  int (*column_bytes)(const cpkt_sqlite_statement *self, int column);
  int (*column_bytes16)(const cpkt_sqlite_statement *self, int column);
  cpkt_sqlite_value *(*column_value)(const cpkt_sqlite_statement *self,
                                     int column);
  const char *(*column_database_name)(const cpkt_sqlite_statement *self,
                                      int column);
  const void *(*column_database_name16)(const cpkt_sqlite_statement *self,
                                        int column);
  const char *(*column_table_name)(const cpkt_sqlite_statement *self,
                                   int column);
  const void *(*column_table_name16)(const cpkt_sqlite_statement *self,
                                     int column);
  const char *(*column_origin_name)(const cpkt_sqlite_statement *self,
                                    int column);
  const void *(*column_origin_name16)(const cpkt_sqlite_statement *self,
                                      int column);
  const char *(*column_declared_type)(const cpkt_sqlite_statement *self,
                                      int column);
  const void *(*column_declared_type16)(const cpkt_sqlite_statement *self,
                                        int column);
  const char *(*sql)(const cpkt_sqlite_statement *self);
  char *(*expanded_sql)(const cpkt_sqlite_statement *self);
  const char *(*normalized_sql)(const cpkt_sqlite_statement *self);
  int (*readonly)(const cpkt_sqlite_statement *self);
  int (*busy)(const cpkt_sqlite_statement *self);
  int (*expired)(const cpkt_sqlite_statement *self);
  int (*is_explain)(const cpkt_sqlite_statement *self);
  int (*explain)(cpkt_sqlite_statement *self, int mode);
  int (*data_count)(const cpkt_sqlite_statement *self);
  int (*status)(const cpkt_sqlite_statement *self, int operation, int reset);
  int (*scan_status_i64)(const cpkt_sqlite_statement *self, int index,
                         int operation, unsigned long flags,
                         cpkt_sqlite_i64 *value_out);
  int (*scan_status_double)(const cpkt_sqlite_statement *self, int index,
                            int operation, unsigned long flags,
                            double *value_out);
  int (*scan_status_int)(const cpkt_sqlite_statement *self, int index,
                         int operation, unsigned long flags, int *value_out);
  int (*scan_status_text)(const cpkt_sqlite_statement *self, int index,
                          int operation, unsigned long flags,
                          const char **value_out);
  void (*scan_status_reset)(cpkt_sqlite_statement *self);
  int (*finalize)(cpkt_sqlite_statement *self);
  void *statement;
  cpkt_sqlite *owner;
  int borrowed;
};

/*
 * These handle shells own only the facade wrapper.  Their native resources
 * retain the documented SQLite lifetime: release each shell with close.
 */
struct cpkt_sqlite_blob {
  int (*read)(cpkt_sqlite_blob *self, void *buffer, int byte_count, int offset);
  int (*write)(cpkt_sqlite_blob *self, const void *buffer, int byte_count,
               int offset);
  int (*reopen)(cpkt_sqlite_blob *self, cpkt_sqlite_i64 row_id);
  int (*bytes)(const cpkt_sqlite_blob *self);
  int (*close)(cpkt_sqlite_blob *self);
  void *blob;
  cpkt_sqlite *database;
};

struct cpkt_sqlite_backup {
  int (*step)(cpkt_sqlite_backup *self, int page_count);
  int (*remaining)(const cpkt_sqlite_backup *self);
  int (*page_count)(const cpkt_sqlite_backup *self);
  int (*close)(cpkt_sqlite_backup *self);
  void *backup;
  cpkt_sqlite *destination;
  cpkt_sqlite *source;
};

struct cpkt_sqlite_table {
  int (*row_count)(const cpkt_sqlite_table *self);
  int (*column_count)(const cpkt_sqlite_table *self);
  const char *(*column_name)(const cpkt_sqlite_table *self, int column);
  const char *(*value)(const cpkt_sqlite_table *self, int row, int column);
  void (*close)(cpkt_sqlite_table *self);
  char **values;
  int rows;
  int columns;
};

struct cpkt_sqlite_value {
  void *value;
  int owned;
  int shell_owned;
};

struct cpkt_sqlite_context {
  void *context;
  cpkt_sqlite *database;
};

/*
 * Virtual-table and cursor shells are allocated by their module callbacks
 * with the constructors below.  SQLite owns their lifetime after a
 * successful callback; their `state` fields remain owned by the module.
 * On callback failure, release state before returning and leave the output
 * NULL. After success the facade calls disconnect/destroy_table or close
 * exactly once, including when it rejects an invalid returned shell.
 */
struct cpkt_sqlite_virtual_table {
  int (*set_error)(cpkt_sqlite_virtual_table *self, const char *message);
  void *state;
  void *internal;
};

struct cpkt_sqlite_virtual_cursor {
  cpkt_sqlite_virtual_table *table;
  void *state;
  void *internal;
};

struct cpkt_sqlite_index_info {
  int constraint_count;
  const cpkt_sqlite_index_constraint *constraints;
  int order_count;
  const cpkt_sqlite_index_order *orders;
  cpkt_sqlite_index_constraint_usage *usages;
  int index_number;
  const char *index_string;
  int order_by_consumed;
  double estimated_cost;
  cpkt_sqlite_i64 estimated_rows;
  unsigned long index_flags;
  cpkt_sqlite_u64 columns_used;
  void *internal;
};

struct cpkt_sqlite_module_methods {
  void *context;
  cpkt_sqlite_destroy_callback destroy;
  cpkt_sqlite_module_connect_callback create;
  cpkt_sqlite_module_connect_callback connect;
  cpkt_sqlite_module_best_index_callback best_index;
  cpkt_sqlite_module_table_callback disconnect;
  cpkt_sqlite_module_table_callback destroy_table;
  cpkt_sqlite_module_open_callback open;
  cpkt_sqlite_module_cursor_callback close;
  cpkt_sqlite_module_filter_callback filter;
  cpkt_sqlite_module_cursor_callback next;
  cpkt_sqlite_module_cursor_callback eof;
  cpkt_sqlite_module_column_callback column;
  cpkt_sqlite_module_rowid_callback rowid;
  cpkt_sqlite_module_update_callback update;
  cpkt_sqlite_module_table_callback begin;
  cpkt_sqlite_module_table_callback sync;
  cpkt_sqlite_module_table_callback commit;
  cpkt_sqlite_module_table_callback rollback;
  cpkt_sqlite_module_find_function_callback find_function;
  cpkt_sqlite_module_rename_callback rename;
  cpkt_sqlite_module_savepoint_callback savepoint;
  cpkt_sqlite_module_savepoint_callback release;
  cpkt_sqlite_module_savepoint_callback rollback_to;
  cpkt_sqlite_module_shadow_name_callback shadow_name;
  cpkt_sqlite_module_integrity_callback integrity;
};

/*
 * A virtual filesystem owns its receiver until close.  A file receiver is
 * created by its open callback and remains valid until its close callback.
 * File state is application-owned; the facade owns only its internal shell.
 */
struct cpkt_sqlite_io_methods {
  int version;
  cpkt_sqlite_file_close_callback close;
  cpkt_sqlite_file_read_callback read;
  cpkt_sqlite_file_write_callback write;
  cpkt_sqlite_file_truncate_callback truncate;
  cpkt_sqlite_file_sync_callback sync;
  cpkt_sqlite_file_size_callback size;
  cpkt_sqlite_file_lock_callback lock;
  cpkt_sqlite_file_lock_callback unlock;
  cpkt_sqlite_file_reserved_lock_callback check_reserved_lock;
  cpkt_sqlite_file_control_callback control;
  cpkt_sqlite_file_sector_size_callback sector_size;
  cpkt_sqlite_file_characteristics_callback characteristics;
  /* Leave shared-memory callbacks NULL when the VFS does not support WAL. */
  cpkt_sqlite_file_shm_map_callback shm_map;
  cpkt_sqlite_file_shm_lock_callback shm_lock;
  cpkt_sqlite_file_shm_barrier_callback shm_barrier;
  cpkt_sqlite_file_shm_unmap_callback shm_unmap;
  cpkt_sqlite_file_fetch_callback fetch;
  cpkt_sqlite_file_unfetch_callback unfetch;
};

struct cpkt_sqlite_file {
  const cpkt_sqlite_io_methods *methods;
  void *state;
  void *internal;
};

struct cpkt_sqlite_vfs_methods {
  int version;
  cpkt_sqlite_vfs_open_callback open;
  cpkt_sqlite_vfs_delete_callback delete_file;
  cpkt_sqlite_vfs_access_callback access;
  cpkt_sqlite_vfs_full_path_callback full_path;
  cpkt_sqlite_vfs_dl_open_callback dl_open;
  cpkt_sqlite_vfs_dl_error_callback dl_error;
  cpkt_sqlite_vfs_dl_symbol_callback dl_symbol;
  cpkt_sqlite_vfs_dl_close_callback dl_close;
  cpkt_sqlite_vfs_randomness_callback randomness;
  cpkt_sqlite_vfs_sleep_callback sleep;
  cpkt_sqlite_vfs_current_time_callback current_time;
  cpkt_sqlite_vfs_last_error_callback last_error;
  cpkt_sqlite_vfs_current_time_i64_callback current_time_i64;
  cpkt_sqlite_vfs_set_system_call_callback set_system_call;
  cpkt_sqlite_vfs_get_system_call_callback get_system_call;
  cpkt_sqlite_vfs_next_system_call_callback next_system_call;
};

struct cpkt_sqlite_vfs {
  int (*register_vfs)(cpkt_sqlite_vfs *self, int make_default);
  int (*unregister_vfs)(cpkt_sqlite_vfs *self);
  void (*close)(cpkt_sqlite_vfs *self);
  const char *name;
  int maximum_pathname_bytes;
  void *state;
  cpkt_sqlite_vfs_methods methods;
  void *internal;
};

/* Process-wide automatic extension registration receiver. */
struct cpkt_sqlite_auto_extension {
  int (*register_extension)(cpkt_sqlite_auto_extension *self);
  int (*cancel)(cpkt_sqlite_auto_extension *self);
  void (*close)(cpkt_sqlite_auto_extension *self);
  void *state;
  void *internal;
};

struct cpkt_sqlite_snapshot {
  void *snapshot;
};

struct cpkt_sqlite_session {
  int (*attach)(cpkt_sqlite_session *self, const char *table_name);
  int (*object_config)(cpkt_sqlite_session *self, int operation, int *value);
  void (*table_filter)(cpkt_sqlite_session *self,
                       cpkt_sqlite_session_filter_callback filter,
                       void *context);
  int (*enable)(cpkt_sqlite_session *self, int enabled);
  int (*indirect)(cpkt_sqlite_session *self, int indirect);
  int (*empty)(const cpkt_sqlite_session *self);
  int (*changeset)(cpkt_sqlite_session *self, cpkt_sqlite_changeset **out);
  int (*patchset)(cpkt_sqlite_session *self, cpkt_sqlite_changeset **out);
  cpkt_sqlite_i64 (*changeset_size)(const cpkt_sqlite_session *self);
  cpkt_sqlite_i64 (*memory_used)(const cpkt_sqlite_session *self);
  int (*diff)(cpkt_sqlite_session *self, const char *from_database,
              const char *table_name, char **error_out);
  int (*changeset_strm)(cpkt_sqlite_session *self,
                        cpkt_sqlite_stream_output_callback output,
                        void *context);
  int (*patchset_strm)(cpkt_sqlite_session *self,
                       cpkt_sqlite_stream_output_callback output,
                       void *context);
  void (*close)(cpkt_sqlite_session *self);
  void *session;
  cpkt_sqlite *database;
  cpkt_sqlite_session_filter_callback filter;
  void *filter_context;
};

struct cpkt_sqlite_changeset {
  /* An empty changeset has byte_count == 0 and may have data == NULL. */
  const void *data;
  int byte_count;
  int (*iterator)(const cpkt_sqlite_changeset *self,
                  cpkt_sqlite_changeset_iterator **out);
  void (*free)(cpkt_sqlite_changeset *self);
  void *owned_data;
};

struct cpkt_sqlite_changeset_iterator {
  int (*next)(cpkt_sqlite_changeset_iterator *self);
  int (*operation)(cpkt_sqlite_changeset_iterator *self,
                   const char **table_name, int *column_count, int *operation,
                   int *indirect);
  int (*primary_key)(cpkt_sqlite_changeset_iterator *self,
                     const unsigned char **columns, int *column_count);
  int (*old_value)(cpkt_sqlite_changeset_iterator *self, int column,
                   cpkt_sqlite_value **out);
  int (*new_value)(cpkt_sqlite_changeset_iterator *self, int column,
                   cpkt_sqlite_value **out);
  int (*conflict_value)(cpkt_sqlite_changeset_iterator *self, int column,
                        cpkt_sqlite_value **out);
  int (*foreign_key_conflicts)(cpkt_sqlite_changeset_iterator *self,
                               int *count_out);
  int (*close)(cpkt_sqlite_changeset_iterator *self);
  void *iterator;
  cpkt_sqlite_stream_input_callback input;
  void *input_context;
};

struct cpkt_sqlite_rebaser {
  /** Zero bytes may be supplied as NULL; nonempty input requires data. */
  int (*configure)(cpkt_sqlite_rebaser *self, const void *data, int byte_count);
  /** On success, even an empty output is owned and must be freed. */
  int (*rebase)(cpkt_sqlite_rebaser *self, const void *data, int byte_count,
                cpkt_sqlite_changeset **out);
  int (*rebase_strm)(cpkt_sqlite_rebaser *self,
                     cpkt_sqlite_stream_input_callback input,
                     void *input_context,
                     cpkt_sqlite_stream_output_callback output,
                     void *output_context);
  void (*close)(cpkt_sqlite_rebaser *self);
  void *rebaser;
};

struct cpkt_sqlite_changegroup {
  int (*config)(cpkt_sqlite_changegroup *self, int operation, int *value);
  int (*schema)(cpkt_sqlite_changegroup *self, cpkt_sqlite *database,
                const char *schema_name);
  int (*add)(cpkt_sqlite_changegroup *self,
             const cpkt_sqlite_changeset *changeset);
  int (*add_strm)(cpkt_sqlite_changegroup *self,
                  cpkt_sqlite_stream_input_callback input, void *context);
  int (*add_change)(cpkt_sqlite_changegroup *self,
                    cpkt_sqlite_changeset_iterator *iterator);
  int (*output)(cpkt_sqlite_changegroup *self, cpkt_sqlite_changeset **out);
  int (*output_strm)(cpkt_sqlite_changegroup *self,
                     cpkt_sqlite_stream_output_callback output, void *context);
  int (*change_begin)(cpkt_sqlite_changegroup *self, int operation,
                      const char *table_name, int indirect, char **error_out);
  int (*change_i64)(cpkt_sqlite_changegroup *self, int is_new, int column,
                    cpkt_sqlite_i64 value);
  int (*change_null)(cpkt_sqlite_changegroup *self, int is_new, int column);
  int (*change_double)(cpkt_sqlite_changegroup *self, int is_new, int column,
                       double value);
  int (*change_text)(cpkt_sqlite_changegroup *self, int is_new, int column,
                     const char *value, int byte_count);
  int (*change_blob)(cpkt_sqlite_changegroup *self, int is_new, int column,
                     const void *value, int byte_count);
  int (*change_finish)(cpkt_sqlite_changegroup *self, int discard,
                       char **error_out);
  void (*close)(cpkt_sqlite_changegroup *self);
  void *changegroup;
};

struct cpkt_sqlite_filename {
  const char *(*database)(const cpkt_sqlite_filename *self);
  const char *(*journal)(const cpkt_sqlite_filename *self);
  const char *(*wal)(const cpkt_sqlite_filename *self);
  const char *(*uri_parameter)(const cpkt_sqlite_filename *self,
                               const char *name);
  int (*uri_boolean)(const cpkt_sqlite_filename *self, const char *name,
                     int default_value);
  cpkt_sqlite_i64 (*uri_i64)(const cpkt_sqlite_filename *self, const char *name,
                             cpkt_sqlite_i64 default_value);
  const char *(*uri_key)(const cpkt_sqlite_filename *self, int index);
  void (*close)(cpkt_sqlite_filename *self);
  void *filename;
};

/* FTS5 registration receiver.  It borrows the database connection. */
struct cpkt_sqlite_fts5_api {
  int (*version)(const cpkt_sqlite_fts5_api *self);
  /** The returned factory borrows the registration until replacement or
   * database close. The caller closes the factory and each instance. */
  int (*find_tokenizer)(cpkt_sqlite_fts5_api *self, const char *name,
                        cpkt_sqlite_fts5_found_tokenizer **out);
  int (*create_tokenizer)(cpkt_sqlite_fts5_api *self, const char *name,
                          void *context,
                          cpkt_sqlite_fts5_tokenizer_create_callback create,
                          cpkt_sqlite_fts5_tokenizer_destroy_callback destroy,
                          cpkt_sqlite_fts5_tokenizer_tokenize_callback tokenize,
                          cpkt_sqlite_destroy_callback binding_destroy);
  int (*create_auxiliary)(cpkt_sqlite_fts5_api *self, const char *name,
                          void *user_data,
                          cpkt_sqlite_fts5_auxiliary_callback callback,
                          cpkt_sqlite_destroy_callback destroy);
  void (*close)(cpkt_sqlite_fts5_api *self);
  void *api;
  cpkt_sqlite *database;
};

struct cpkt_sqlite_fts5_found_tokenizer {
  int (*create)(cpkt_sqlite_fts5_found_tokenizer *self,
                const char *const *arguments, int argument_count,
                cpkt_sqlite_fts5_tokenizer_instance **out);
  void (*close)(cpkt_sqlite_fts5_found_tokenizer *self);
  void *internal;
};

struct cpkt_sqlite_fts5_tokenizer_instance {
  int (*tokenize)(cpkt_sqlite_fts5_tokenizer_instance *self, void *context,
                  int flags, const char *text, int text_byte_count,
                  const char *locale, int locale_byte_count,
                  cpkt_sqlite_fts5_token_callback token);
  void (*close)(cpkt_sqlite_fts5_tokenizer_instance *self);
  void *internal;
};

/* This shell is created by a tokenizer create callback and released by FTS5. */
struct cpkt_sqlite_fts5_tokenizer {
  void *state;
  void *facade;
};

/* Valid only during an FTS5 auxiliary or query-phrase callback. */
struct cpkt_sqlite_fts5_context {
  /** Native FTS5 extension API version for this callback invocation. */
  int version;
  void *(*user_data)(const cpkt_sqlite_fts5_context *self);
  int (*column_count)(const cpkt_sqlite_fts5_context *self);
  int (*row_count)(const cpkt_sqlite_fts5_context *self, cpkt_sqlite_i64 *out);
  int (*column_total_size)(const cpkt_sqlite_fts5_context *self, int column,
                           cpkt_sqlite_i64 *out);
  int (*tokenize)(cpkt_sqlite_fts5_context *self, const char *text,
                  int text_byte_count, void *context,
                  cpkt_sqlite_fts5_token_callback token);
  int (*tokenize_locale)(cpkt_sqlite_fts5_context *self, const char *text,
                         int text_byte_count, const char *locale,
                         int locale_byte_count, void *context,
                         cpkt_sqlite_fts5_token_callback token);
  int (*phrase_count)(const cpkt_sqlite_fts5_context *self);
  int (*phrase_size)(const cpkt_sqlite_fts5_context *self, int phrase);
  int (*instance_count)(const cpkt_sqlite_fts5_context *self, int *out);
  int (*instance)(const cpkt_sqlite_fts5_context *self, int index,
                  int *phrase_out, int *column_out, int *offset_out);
  cpkt_sqlite_i64 (*rowid)(const cpkt_sqlite_fts5_context *self);
  int (*column_text)(const cpkt_sqlite_fts5_context *self, int column,
                     const char **text_out, int *byte_count_out);
  int (*column_size)(const cpkt_sqlite_fts5_context *self, int column,
                     int *token_count_out);
  int (*query_phrase)(cpkt_sqlite_fts5_context *self, int phrase,
                      void *user_data,
                      cpkt_sqlite_fts5_query_phrase_callback callback);
  int (*set_auxdata)(cpkt_sqlite_fts5_context *self, void *data,
                     cpkt_sqlite_destroy_callback destroy);
  void *(*get_auxdata)(cpkt_sqlite_fts5_context *self, int clear);
  int (*phrase_first)(cpkt_sqlite_fts5_context *self, int phrase,
                      cpkt_sqlite_fts5_phrase_iterator *iterator,
                      int *column_out, int *offset_out);
  void (*phrase_next)(cpkt_sqlite_fts5_context *self,
                      cpkt_sqlite_fts5_phrase_iterator *iterator,
                      int *column_out, int *offset_out);
  int (*phrase_first_column)(cpkt_sqlite_fts5_context *self, int phrase,
                             cpkt_sqlite_fts5_phrase_iterator *iterator,
                             int *column_out);
  void (*phrase_next_column)(cpkt_sqlite_fts5_context *self,
                             cpkt_sqlite_fts5_phrase_iterator *iterator,
                             int *column_out);
  int (*query_token)(cpkt_sqlite_fts5_context *self, int phrase, int token,
                     const char **text_out, int *byte_count_out);
  int (*instance_token)(cpkt_sqlite_fts5_context *self, int instance, int token,
                        const char **text_out, int *byte_count_out);
  int (*column_locale)(cpkt_sqlite_fts5_context *self, int column,
                       const char **locale_out, int *byte_count_out);
  void *api;
  void *context;
  cpkt_sqlite *database;
};

struct cpkt_sqlite_fts5_phrase_iterator {
  const unsigned char *first;
  const unsigned char *second;
};

struct cpkt_sqlite_mutex {
  void (*enter)(cpkt_sqlite_mutex *self);
  int (*try_enter)(cpkt_sqlite_mutex *self);
  void (*leave)(cpkt_sqlite_mutex *self);
  int (*held)(const cpkt_sqlite_mutex *self);
  int (*not_held)(const cpkt_sqlite_mutex *self);
  void (*close)(cpkt_sqlite_mutex *self);
  void *mutex;
  int dynamic;
};

struct cpkt_sqlite_string {
  void (*append)(cpkt_sqlite_string *self, const char *text, int byte_count);
  void (*append_all)(cpkt_sqlite_string *self, const char *text);
  void (*append_char)(cpkt_sqlite_string *self, int count, char character);
  void (*append_format)(cpkt_sqlite_string *self, const char *format, ...);
  void (*append_format_v)(cpkt_sqlite_string *self, const char *format,
                          va_list arguments);
  void (*reset)(cpkt_sqlite_string *self);
  void (*truncate)(cpkt_sqlite_string *self, int byte_count);
  int (*error_code)(const cpkt_sqlite_string *self);
  int (*length)(const cpkt_sqlite_string *self);
  const char *(*value)(const cpkt_sqlite_string *self);
  char *(*finish)(cpkt_sqlite_string *self);
  void (*close)(cpkt_sqlite_string *self);
  void *string;
};

cpkt_sqlite_i64 cpkt_sqlite_i64_make(unsigned long high, unsigned long low);
cpkt_sqlite_u64 cpkt_sqlite_u64_make(unsigned long high, unsigned long low);
int cpkt_sqlite_i64_compare(cpkt_sqlite_i64 left, cpkt_sqlite_i64 right);
int cpkt_sqlite_u64_compare(cpkt_sqlite_u64 left, cpkt_sqlite_u64 right);
const char *cpkt_sqlite_library_version(void);
/** Borrowed SQLite temporary-directory value. Valid until the next native or
 * facade assignment. Read only while all other SQLite use is quiescent. */
const char *cpkt_sqlite_temp_directory_get(void);
/** Set SQLite's legacy temporary-directory global. Copies UTF-8 with SQLite's
 * allocator; NULL restores native directory search. Call during process setup
 * with no open databases and no concurrent SQLite calls. As with the native
 * variable, a later SQLite temp_store_directory pragma can replace it. */
int cpkt_sqlite_temp_directory_set(const char *directory);
const char *cpkt_sqlite_source_id(void);
int cpkt_sqlite_library_version_number(void);
int cpkt_sqlite_compile_option_used(const char *name);
const char *cpkt_sqlite_compile_option(int index);
int cpkt_sqlite_threadsafe(void);
int cpkt_sqlite_initialize(void);
int cpkt_sqlite_shutdown(void);
int cpkt_sqlite_os_initialize(void);
int cpkt_sqlite_os_shutdown(void);
int cpkt_sqlite_global_recover(void);
void cpkt_sqlite_thread_cleanup(void);
int cpkt_sqlite_sleep(int milliseconds);
int cpkt_sqlite_global_config_none(int operation);
int cpkt_sqlite_global_config_int(int operation, int value);
int cpkt_sqlite_global_config_two_int(int operation, int first, int second);
int cpkt_sqlite_global_config_pointer(int operation, void *value);
int cpkt_sqlite_global_config_pointer_int_int(int operation, void *buffer,
                                              int first, int second);
int cpkt_sqlite_global_config_i64(int operation, cpkt_sqlite_i64 value);
int cpkt_sqlite_global_config_two_i64(int operation, cpkt_sqlite_i64 first,
                                      cpkt_sqlite_i64 second);
int cpkt_sqlite_global_config_unsigned_int(int operation, unsigned long value);
int cpkt_sqlite_global_config_int_out(int operation, int *value_out);
int cpkt_sqlite_global_config_log(cpkt_sqlite_log_callback callback,
                                  void *context);
void cpkt_sqlite_log(int error_code, const char *format, ...);
/** Formats one signed 64-bit conversion for SQLite's log callback. */
void cpkt_sqlite_log_i64(int error_code, const char *format,
                         cpkt_sqlite_i64 value);
/** Formats one unsigned 64-bit conversion for SQLite's log callback. */
void cpkt_sqlite_log_u64(int error_code, const char *format,
                         cpkt_sqlite_u64 value);
int cpkt_sqlite_global_config_memory_methods_set(
    const cpkt_sqlite_memory_methods *methods);
int cpkt_sqlite_global_config_memory_methods_get(
    cpkt_sqlite_memory_methods *methods_out);
int cpkt_sqlite_global_config_mutex_methods_set(
    const cpkt_sqlite_mutex_methods *methods);
int cpkt_sqlite_global_config_mutex_methods_get(
    cpkt_sqlite_mutex_methods *methods_out);
/** Release a native GETMUTEX view after its callbacks and all derived mutex
 * objects are no longer in use. Custom application tables require no release.
 */
void cpkt_sqlite_mutex_methods_release(cpkt_sqlite_mutex_methods *methods);
int cpkt_sqlite_global_config_page_cache_methods_set(
    const cpkt_sqlite_page_cache_methods *methods);
int cpkt_sqlite_global_config_page_cache_methods_get(
    cpkt_sqlite_page_cache_methods *methods_out);
/** Release a native GETPCACHE2 view after its caches and pages are gone. */
void cpkt_sqlite_page_cache_methods_release(
    cpkt_sqlite_page_cache_methods *methods);
/** APPLICATION mutexes are not used by facade bookkeeping. */
cpkt_sqlite_mutex *cpkt_sqlite_mutex_new(int mutex_type);
cpkt_sqlite_mutex *cpkt_sqlite_database_mutex(const cpkt_sqlite *database);
int cpkt_sqlite_complete(const char *sql);
int cpkt_sqlite_complete_utf16(const void *sql);
char *cpkt_sqlite_format(const char *format, ...);
char *cpkt_sqlite_format_v(const char *format, va_list arguments);
/** Formats one %ll signed conversion; free the result with cpkt_sqlite_free().
 */
char *cpkt_sqlite_format_i64(const char *format, cpkt_sqlite_i64 value);
/** Formats one %ll unsigned conversion; free the result with
 * cpkt_sqlite_free(). */
char *cpkt_sqlite_format_u64(const char *format, cpkt_sqlite_u64 value);
char *cpkt_sqlite_format_into(int byte_count, char *buffer, const char *format,
                              ...);
char *cpkt_sqlite_format_into_v(int byte_count, char *buffer,
                                const char *format, va_list arguments);
/** Writes one signed 64-bit conversion into a caller-owned buffer. */
char *cpkt_sqlite_format_into_i64(int byte_count, char *buffer,
                                  const char *format, cpkt_sqlite_i64 value);
/** Writes one unsigned 64-bit conversion into a caller-owned buffer. */
char *cpkt_sqlite_format_into_u64(int byte_count, char *buffer,
                                  const char *format, cpkt_sqlite_u64 value);
void *cpkt_sqlite_allocate(int byte_count);
void *cpkt_sqlite_allocate_u64(cpkt_sqlite_u64 byte_count);
void *cpkt_sqlite_reallocate(void *memory, int byte_count);
void *cpkt_sqlite_reallocate_u64(void *memory, cpkt_sqlite_u64 byte_count);
void cpkt_sqlite_free(void *memory);
cpkt_sqlite_u64 cpkt_sqlite_allocation_size(void *memory);
cpkt_sqlite_i64 cpkt_sqlite_memory_used(void);
cpkt_sqlite_i64 cpkt_sqlite_memory_highwater(int reset);
int cpkt_sqlite_memory_alarm(cpkt_sqlite_memory_alarm_callback callback,
                             void *context, cpkt_sqlite_i64 threshold_bytes);
void cpkt_sqlite_randomness(int byte_count, void *buffer);
int cpkt_sqlite_enable_shared_cache(int enabled);
int cpkt_sqlite_release_memory(int byte_count);
cpkt_sqlite_i64 cpkt_sqlite_soft_heap_limit(cpkt_sqlite_i64 byte_count);
cpkt_sqlite_i64 cpkt_sqlite_hard_heap_limit(cpkt_sqlite_i64 byte_count);
const char *cpkt_sqlite_error_string(int code);
int cpkt_sqlite_keyword_count(void);
int cpkt_sqlite_keyword_name(int index, const char **name, int *byte_count);
int cpkt_sqlite_keyword_check(const char *name, int byte_count);
int cpkt_sqlite_case_compare(const char *left, const char *right);
int cpkt_sqlite_case_compare_n(const char *left, const char *right,
                               int byte_count);
int cpkt_sqlite_glob(const char *pattern, const char *text);
int cpkt_sqlite_like(const char *pattern, const char *text,
                     unsigned long escape_character);
int cpkt_sqlite_status(int category, int *current, int *highwater, int reset);
int cpkt_sqlite_status_i64(int category, cpkt_sqlite_i64 *current,
                           cpkt_sqlite_i64 *highwater, int reset);
cpkt_sqlite_string *cpkt_sqlite_string_new(cpkt_sqlite *database);
/** Appends one signed 64-bit conversion to an existing SQLite string. */
void cpkt_sqlite_string_append_format_i64(cpkt_sqlite_string *self,
                                          const char *format,
                                          cpkt_sqlite_i64 value);
/** Appends one unsigned 64-bit conversion to an existing SQLite string. */
void cpkt_sqlite_string_append_format_u64(cpkt_sqlite_string *self,
                                          const char *format,
                                          cpkt_sqlite_u64 value);
cpkt_sqlite *cpkt_sqlite_new(const char *filename);
cpkt_sqlite *cpkt_sqlite_open(const char *filename, int flags, const char *vfs);
cpkt_sqlite *cpkt_sqlite_open16(const void *filename);
/** Facade extensions initialize in successful registration order. Registering
 * an already registered extension is a no-op; re-registration after cancel
 * places it last. Context remains caller-owned through pending callbacks. */
cpkt_sqlite_auto_extension *
cpkt_sqlite_auto_extension_new(cpkt_sqlite_auto_extension_callback callback,
                               void *context);
void cpkt_sqlite_auto_extension_reset(void);
cpkt_sqlite_vfs *cpkt_sqlite_vfs_new(const char *name,
                                     int maximum_pathname_bytes, void *state,
                                     const cpkt_sqlite_vfs_methods *methods);
/* Returns an existing facade VFS by borrowed identity, or an owned view of
 * a native VFS. Close a native view when finished; closing never unregisters
 * its provider. Direct register/unregister and receiver methods act on that
 * provider. Restore a native default after unregistering it. The provider
 * must remain registered while the view is used. */
cpkt_sqlite_vfs *cpkt_sqlite_vfs_find(const char *name);
/* Valid only for a journal/WAL filename passed to a VFS open callback. A
 * native result is an independently owned metadata view: methods->close
 * releases it without closing SQLite's database file. A facade result is
 * borrowed. File-control handle views instead close through their handle. */
cpkt_sqlite_file *cpkt_sqlite_vfs_database_file_object(const char *name);
int cpkt_sqlite_vfs_register(cpkt_sqlite_vfs *self, int make_default);
int cpkt_sqlite_vfs_unregister(cpkt_sqlite_vfs *self);
void cpkt_sqlite_vfs_close(cpkt_sqlite_vfs *self);
/* Strict native close semantics: leaves self usable on SQLITE_BUSY. */
int cpkt_sqlite_close_strict(cpkt_sqlite *self);
void cpkt_sqlite_close(cpkt_sqlite *self);
int cpkt_sqlite_exec(cpkt_sqlite *self, const char *sql,
                     cpkt_sqlite_row_callback callback, void *context);
int cpkt_sqlite_get_table(cpkt_sqlite *self, const char *sql,
                          cpkt_sqlite_table **table_out, char **error_out);
int cpkt_sqlite_statement_transfer_bindings(cpkt_sqlite_statement *source,
                                            cpkt_sqlite_statement *target);
/* Returns a borrowed enumeration view. Finalize releases only that view. */
int cpkt_sqlite_next_statement(cpkt_sqlite *database,
                               const cpkt_sqlite_statement *previous,
                               cpkt_sqlite_statement **out);
int cpkt_sqlite_prepare(cpkt_sqlite *self, const char *sql, int byte_count,
                        unsigned long flags,
                        cpkt_sqlite_statement **statement_out,
                        const char **tail_out);
int cpkt_sqlite_prepare16(cpkt_sqlite *self, const void *sql, int byte_count,
                          unsigned long flags,
                          cpkt_sqlite_statement **statement_out,
                          const void **tail_out);
/** Uses SQLite's original prepare semantics (no automatic reprepare). */
int cpkt_sqlite_prepare_legacy(cpkt_sqlite *self, const char *sql,
                               int byte_count,
                               cpkt_sqlite_statement **statement_out,
                               const char **tail_out);
/** UTF-16 counterpart of cpkt_sqlite_prepare_legacy(). */
int cpkt_sqlite_prepare_legacy16(cpkt_sqlite *self, const void *sql,
                                 int byte_count,
                                 cpkt_sqlite_statement **statement_out,
                                 const void **tail_out);
int cpkt_sqlite_busy_timeout(cpkt_sqlite *self, int milliseconds);
int cpkt_sqlite_extended_result_codes(cpkt_sqlite *self, int enabled);
int cpkt_sqlite_wal_auto_checkpoint(cpkt_sqlite *self, int page_count);
int cpkt_sqlite_wal_checkpoint(cpkt_sqlite *self, const char *schema, int mode,
                               int *log_frames_out,
                               int *checkpointed_frames_out);
int cpkt_sqlite_load_extension(cpkt_sqlite *self, const char *path,
                               const char *entry_point, char **error_out);
int cpkt_sqlite_enable_extension_loading(cpkt_sqlite *self, int enabled);
int cpkt_sqlite_unlock_notify(cpkt_sqlite *self,
                              cpkt_sqlite_unlock_notify_callback callback,
                              void *context);
int cpkt_sqlite_overload_function(cpkt_sqlite *self, const char *name,
                                  int argument_count);
int cpkt_sqlite_drop_modules(cpkt_sqlite *self, int kept_name_count,
                             const char *const *kept_names);
cpkt_sqlite_virtual_table *cpkt_sqlite_virtual_table_new(void *state);
cpkt_sqlite_virtual_cursor *
cpkt_sqlite_virtual_cursor_new(cpkt_sqlite_virtual_table *table, void *state);
int cpkt_sqlite_declare_virtual_table(cpkt_sqlite *database,
                                      const char *schema);
int cpkt_sqlite_create_module(cpkt_sqlite *database, const char *name,
                              const cpkt_sqlite_module_methods *methods);
int cpkt_sqlite_virtual_table_config_none(cpkt_sqlite *database, int operation);
int cpkt_sqlite_virtual_table_config_int(cpkt_sqlite *database, int operation,
                                         int value);
int cpkt_sqlite_virtual_table_on_conflict(cpkt_sqlite *database);
int cpkt_sqlite_context_no_change(const cpkt_sqlite_context *context);
int cpkt_sqlite_index_info_rhs_value(const cpkt_sqlite_index_info *self,
                                     int constraint_index,
                                     cpkt_sqlite_value *value_out);
const char *cpkt_sqlite_index_info_collation(const cpkt_sqlite_index_info *self,
                                             int constraint_index);
int cpkt_sqlite_index_info_distinct(const cpkt_sqlite_index_info *self);
int cpkt_sqlite_index_info_set_in(cpkt_sqlite_index_info *self,
                                  int constraint_index, int enabled);
int cpkt_sqlite_virtual_table_in_first(const cpkt_sqlite_value *input,
                                       cpkt_sqlite_value *value_out);
int cpkt_sqlite_virtual_table_in_next(const cpkt_sqlite_value *input,
                                      cpkt_sqlite_value *value_out);
int cpkt_sqlite_database_config_int(cpkt_sqlite *self, int operation, int value,
                                    int *result_out);
int cpkt_sqlite_database_config_lookaside(cpkt_sqlite *self, void *buffer,
                                          int slot_byte_count, int slot_count);
int cpkt_sqlite_database_config_main_name(cpkt_sqlite *self, const char *name);
cpkt_sqlite_filename *cpkt_sqlite_filename_new(const char *database,
                                               const char *journal,
                                               const char *wal,
                                               int parameter_count,
                                               const char *const *parameters);
int cpkt_sqlite_fts5_api_open(cpkt_sqlite *database,
                              cpkt_sqlite_fts5_api **out);
cpkt_sqlite_fts5_tokenizer *cpkt_sqlite_fts5_tokenizer_new(void *state);
void *cpkt_sqlite_fts5_tokenizer_state(const cpkt_sqlite_fts5_tokenizer *self);
int cpkt_sqlite_changes(const cpkt_sqlite *self);
cpkt_sqlite_i64 cpkt_sqlite_changes64(const cpkt_sqlite *self);
int cpkt_sqlite_total_changes(const cpkt_sqlite *self);
cpkt_sqlite_i64 cpkt_sqlite_total_changes64(const cpkt_sqlite *self);
cpkt_sqlite_i64 cpkt_sqlite_last_insert_rowid(const cpkt_sqlite *self);
void cpkt_sqlite_set_last_insert_rowid(cpkt_sqlite *self,
                                       cpkt_sqlite_i64 value);
void cpkt_sqlite_interrupt(cpkt_sqlite *self);
int cpkt_sqlite_interrupted(const cpkt_sqlite *self);
int cpkt_sqlite_limit(cpkt_sqlite *self, int category, int new_value);
int cpkt_sqlite_autocommit(const cpkt_sqlite *self);
const char *cpkt_sqlite_database_name(const cpkt_sqlite *self, int index);
const char *cpkt_sqlite_database_filename(const cpkt_sqlite *self,
                                          const char *name);
int cpkt_sqlite_database_readonly(const cpkt_sqlite *self, const char *name);
int cpkt_sqlite_transaction_state(const cpkt_sqlite *self, const char *schema);
int cpkt_sqlite_table_column_metadata(
    cpkt_sqlite *self, const char *database_name, const char *table_name,
    const char *column_name, cpkt_sqlite_column_metadata *metadata_out);
int cpkt_sqlite_database_release_memory(cpkt_sqlite *self);
int cpkt_sqlite_cache_flush(cpkt_sqlite *self);
/** SIZE_HINT, MMAP_SIZE, and SIZE_LIMIT take cpkt_sqlite_i64 * arguments.
 * Use the typed operations below for native record and pointer outputs. */
int cpkt_sqlite_file_control(cpkt_sqlite *self, const char *database_name,
                             int operation, void *argument);
/** Restricts operation to the provider's int payload family. */
int cpkt_sqlite_file_control_int(cpkt_sqlite *self, const char *database_name,
                                 int operation, int *value);
int cpkt_sqlite_file_control_unsigned(cpkt_sqlite *self,
                                      const char *database_name, int operation,
                                      unsigned long *value);
/** Owned text for VFSNAME, TEMPFILENAME, or GET_LOCKPROXYFILE; free with
 * cpkt_sqlite_free(). Borrowed native lock-proxy text is copied. Its copy
 * returns NOMEM with a NULL output on allocation failure; provider errors
 * also leave that output NULL. A successful NULL proxy path stays NULL. */
int cpkt_sqlite_file_control_text(cpkt_sqlite *self, const char *database_name,
                                  int operation, char **text_out);
/** String input for TRACE or Darwin's lock-proxy file path. */
int cpkt_sqlite_file_control_text_input(cpkt_sqlite *self,
                                        const char *database_name,
                                        int operation, const char *text);
/** File controls with no application payload, such as RESET_CACHE. */
int cpkt_sqlite_file_control_none(cpkt_sqlite *self, const char *database_name,
                                  int operation);
/** FILE_POINTER/JOURNAL_POINTER return a borrowed file through a newly
 * allocated receiver. Close only the receiver before closing the database. */
int cpkt_sqlite_file_control_file(cpkt_sqlite *self, const char *database_name,
                                  int operation, cpkt_sqlite_file_handle **out);
/** Full callable file methods; borrowed until the handle is closed. The view's
 * close returns CPKT_SQLITE_MISUSE, including for custom facade VFS files.
 * Release its metadata with cpkt_sqlite_file_handle_close(). */
cpkt_sqlite_file *cpkt_sqlite_file_handle_view(cpkt_sqlite_file_handle *self);
int cpkt_sqlite_file_control_vfs(cpkt_sqlite *self, const char *database_name,
                                 cpkt_sqlite_vfs_handle **out);
/** Full callable VFS methods; borrowed until the handle is closed. The view
 * refers to the database's actual provider even after another VFS is registered
 * with the same name or that provider is unregistered. Keep the provider alive
 * and release the view metadata with cpkt_sqlite_vfs_handle_close(). */
cpkt_sqlite_vfs *cpkt_sqlite_vfs_handle_view(cpkt_sqlite_vfs_handle *self);
int cpkt_sqlite_file_control_filestat(cpkt_sqlite *self,
                                      const char *database_name,
                                      cpkt_sqlite_string *output);
int cpkt_sqlite_file_handle_read(cpkt_sqlite_file_handle *self, void *buffer,
                                 int byte_count, cpkt_sqlite_i64 offset);
int cpkt_sqlite_file_handle_write(cpkt_sqlite_file_handle *self,
                                  const void *buffer, int byte_count,
                                  cpkt_sqlite_i64 offset);
int cpkt_sqlite_file_handle_size(cpkt_sqlite_file_handle *self,
                                 cpkt_sqlite_i64 *out);
int cpkt_sqlite_file_handle_sync(cpkt_sqlite_file_handle *self, int flags);
void cpkt_sqlite_file_handle_close(cpkt_sqlite_file_handle *self);
const char *cpkt_sqlite_vfs_handle_name(const cpkt_sqlite_vfs_handle *self);
int cpkt_sqlite_vfs_handle_version(const cpkt_sqlite_vfs_handle *self);
int cpkt_sqlite_vfs_handle_access(cpkt_sqlite_vfs_handle *self,
                                  const char *path, int flags, int *out);
void cpkt_sqlite_vfs_handle_close(cpkt_sqlite_vfs_handle *self);
int cpkt_sqlite_set_lock_timeout(cpkt_sqlite *self, int milliseconds,
                                 unsigned long flags);
int cpkt_sqlite_status_database(const cpkt_sqlite *self, int category,
                                int *current, int *highwater, int reset);
int cpkt_sqlite_status_database_i64(const cpkt_sqlite *self, int category,
                                    cpkt_sqlite_i64 *current,
                                    cpkt_sqlite_i64 *highwater, int reset);
int cpkt_sqlite_system_error(const cpkt_sqlite *self);
const char *cpkt_sqlite_error(const cpkt_sqlite *self);
const void *cpkt_sqlite_error16(const cpkt_sqlite *self);
int cpkt_sqlite_error_code(const cpkt_sqlite *self);
/** SQLite's current result code, respecting extended-result-code mode. */
int cpkt_sqlite_result_code(const cpkt_sqlite *self);
int cpkt_sqlite_set_error(cpkt_sqlite *self, int code, const char *message);
int cpkt_sqlite_error_offset(const cpkt_sqlite *self);
int cpkt_sqlite_set_busy_handler(cpkt_sqlite *self,
                                 cpkt_sqlite_busy_callback callback,
                                 void *context);
int cpkt_sqlite_set_authorizer(cpkt_sqlite *self,
                               cpkt_sqlite_authorizer_callback callback,
                               void *context);
int cpkt_sqlite_set_collation_needed(
    cpkt_sqlite *self, cpkt_sqlite_collation_needed_callback callback,
    void *context);
int cpkt_sqlite_set_collation_needed16(
    cpkt_sqlite *self, cpkt_sqlite_collation_needed16_callback callback,
    void *context);
int cpkt_sqlite_set_autovacuum_callback(
    cpkt_sqlite *self, cpkt_sqlite_autovacuum_callback callback, void *context,
    cpkt_sqlite_destroy_callback destroy);
void *cpkt_sqlite_client_data(const cpkt_sqlite *self, const char *name);
int cpkt_sqlite_set_client_data(cpkt_sqlite *self, const char *name, void *data,
                                cpkt_sqlite_destroy_callback destroy);
int cpkt_sqlite_set_trace(cpkt_sqlite *self, unsigned long mask,
                          cpkt_sqlite_trace_callback callback, void *context);
void *cpkt_sqlite_set_legacy_trace(cpkt_sqlite *self,
                                   cpkt_sqlite_legacy_trace_callback callback,
                                   void *context);
/** Changes the legacy profiler without disabling v2 tracing; returns the
 * previous legacy profiler context. */
void *
cpkt_sqlite_set_legacy_profile(cpkt_sqlite *self,
                               cpkt_sqlite_legacy_profile_callback callback,
                               void *context);
int cpkt_sqlite_register_rtree_geometry(
    cpkt_sqlite *self, const char *name,
    cpkt_sqlite_rtree_geometry_callback callback, void *context);
int cpkt_sqlite_register_rtree_query(cpkt_sqlite *self, const char *name,
                                     cpkt_sqlite_rtree_query_callback callback,
                                     void *context,
                                     cpkt_sqlite_destroy_callback destroy);
void cpkt_sqlite_set_progress_handler(cpkt_sqlite *self, int instruction_count,
                                      cpkt_sqlite_progress_callback callback,
                                      void *context);
void cpkt_sqlite_set_commit_hook(cpkt_sqlite *self,
                                 cpkt_sqlite_commit_callback callback,
                                 void *context);
void cpkt_sqlite_set_rollback_hook(cpkt_sqlite *self,
                                   cpkt_sqlite_rollback_callback callback,
                                   void *context);
void cpkt_sqlite_set_update_hook(cpkt_sqlite *self,
                                 cpkt_sqlite_update_callback callback,
                                 void *context);
void cpkt_sqlite_set_wal_hook(cpkt_sqlite *self,
                              cpkt_sqlite_wal_callback callback, void *context);
int cpkt_sqlite_set_preupdate_hook(cpkt_sqlite *self,
                                   cpkt_sqlite_preupdate_callback callback,
                                   void *context);
int cpkt_sqlite_preupdate_count(const cpkt_sqlite *self);
int cpkt_sqlite_preupdate_depth(const cpkt_sqlite *self);
int cpkt_sqlite_preupdate_blob_write(const cpkt_sqlite *self);
int cpkt_sqlite_preupdate_old(cpkt_sqlite *self, int column,
                              cpkt_sqlite_value **out);
int cpkt_sqlite_preupdate_new(cpkt_sqlite *self, int column,
                              cpkt_sqlite_value **out);
int cpkt_sqlite_open_blob(cpkt_sqlite *self, const char *database_name,
                          const char *table_name, const char *column_name,
                          cpkt_sqlite_i64 row_id, int writable,
                          cpkt_sqlite_blob **out);
int cpkt_sqlite_backup_start(cpkt_sqlite *destination,
                             const char *destination_name, cpkt_sqlite *source,
                             const char *source_name, cpkt_sqlite_backup **out);
int cpkt_sqlite_snapshot_get(cpkt_sqlite *self, const char *schema,
                             cpkt_sqlite_snapshot **out);
int cpkt_sqlite_snapshot_open(cpkt_sqlite *self, const char *schema,
                              cpkt_sqlite_snapshot *snapshot);
int cpkt_sqlite_snapshot_compare(const cpkt_sqlite_snapshot *left,
                                 const cpkt_sqlite_snapshot *right);
void cpkt_sqlite_snapshot_free(cpkt_sqlite_snapshot *snapshot);
int cpkt_sqlite_snapshot_recover(cpkt_sqlite *self, const char *schema);
unsigned char *cpkt_sqlite_serialize(cpkt_sqlite *self, const char *schema,
                                     cpkt_sqlite_i64 *byte_count,
                                     unsigned long flags);
int cpkt_sqlite_deserialize(cpkt_sqlite *self, const char *schema,
                            unsigned char *data, cpkt_sqlite_i64 database_size,
                            cpkt_sqlite_i64 buffer_size, unsigned long flags);
/** NULL callbacks delete a function. destroy receives user_data on failure
 * and when the registration is replaced, deleted, or the connection closes. */
int cpkt_sqlite_create_function(
    cpkt_sqlite *self, const char *name, int argument_count,
    unsigned long text_representation, void *user_data,
    cpkt_sqlite_scalar_callback scalar, cpkt_sqlite_scalar_callback step,
    cpkt_sqlite_scalar_callback final, cpkt_sqlite_destroy_callback destroy);
/** Native-endian UTF-16 name. NULL callbacks delete the function; user_data
 * remains caller-owned. The facade releases replaced/deleted bindings. */
int cpkt_sqlite_create_function16(cpkt_sqlite *self, const void *name,
                                  int argument_count,
                                  unsigned long text_representation,
                                  void *user_data,
                                  cpkt_sqlite_scalar_callback scalar,
                                  cpkt_sqlite_scalar_callback step,
                                  cpkt_sqlite_scalar_callback final);
/** NULL value and inverse callbacks together register an aggregate. All four
 * NULL callbacks delete the named function. destroy receives user_data on
 * failure and when the registration is replaced, deleted, or closed. */
int cpkt_sqlite_create_window_function(
    cpkt_sqlite *self, const char *name, int argument_count,
    unsigned long text_representation, void *user_data,
    cpkt_sqlite_scalar_callback step, cpkt_sqlite_scalar_callback final,
    cpkt_sqlite_scalar_callback value, cpkt_sqlite_scalar_callback inverse,
    cpkt_sqlite_destroy_callback destroy);
int cpkt_sqlite_create_collation(cpkt_sqlite *self, const char *name,
                                 unsigned long text_representation,
                                 void *context,
                                 cpkt_sqlite_collation_callback compare,
                                 cpkt_sqlite_destroy_callback destroy);
int cpkt_sqlite_create_collation16(cpkt_sqlite *self, const void *name,
                                   unsigned long text_representation,
                                   void *context,
                                   cpkt_sqlite_collation_callback compare);
void *cpkt_sqlite_context_user_data(cpkt_sqlite_context *context);
void *cpkt_sqlite_context_aggregate(cpkt_sqlite_context *context,
                                    int byte_count);
int cpkt_sqlite_context_aggregate_count(cpkt_sqlite_context *context);
cpkt_sqlite *cpkt_sqlite_context_database(cpkt_sqlite_context *context);
void *cpkt_sqlite_context_auxdata(cpkt_sqlite_context *context,
                                  int argument_index);
int cpkt_sqlite_context_set_auxdata(cpkt_sqlite_context *context,
                                    int argument_index, void *data,
                                    cpkt_sqlite_destroy_callback destroy);
void cpkt_sqlite_context_result_null(cpkt_sqlite_context *context);
void cpkt_sqlite_context_result_int(cpkt_sqlite_context *context, int value);
void cpkt_sqlite_context_result_i64(cpkt_sqlite_context *context,
                                    cpkt_sqlite_i64 value);
void cpkt_sqlite_context_result_double(cpkt_sqlite_context *context,
                                       double value);
void cpkt_sqlite_context_result_text(cpkt_sqlite_context *context,
                                     const char *value, int byte_count);
void cpkt_sqlite_context_result_text16(cpkt_sqlite_context *context,
                                       const void *value, int byte_count);
void cpkt_sqlite_context_result_text16le(cpkt_sqlite_context *context,
                                         const void *value, int byte_count);
void cpkt_sqlite_context_result_text16be(cpkt_sqlite_context *context,
                                         const void *value, int byte_count);
void cpkt_sqlite_context_result_text_u64(cpkt_sqlite_context *context,
                                         const char *value,
                                         cpkt_sqlite_u64 byte_count,
                                         unsigned long encoding);
void cpkt_sqlite_context_result_blob(cpkt_sqlite_context *context,
                                     const void *value, int byte_count);
void cpkt_sqlite_context_result_blob_u64(cpkt_sqlite_context *context,
                                         const void *value,
                                         cpkt_sqlite_u64 byte_count);
int cpkt_sqlite_context_result_zero_blob(cpkt_sqlite_context *context,
                                         int byte_count);
int cpkt_sqlite_context_result_zero_blob_u64(cpkt_sqlite_context *context,
                                             cpkt_sqlite_u64 byte_count);
void cpkt_sqlite_context_result_pointer(cpkt_sqlite_context *context,
                                        void *value, const char *type_name,
                                        cpkt_sqlite_destroy_callback destroy);
void cpkt_sqlite_context_result_no_memory(cpkt_sqlite_context *context);
void cpkt_sqlite_context_result_too_big(cpkt_sqlite_context *context);
void cpkt_sqlite_context_result_error(cpkt_sqlite_context *context,
                                      const char *message, int byte_count);
void cpkt_sqlite_context_result_error16(cpkt_sqlite_context *context,
                                        const void *message, int byte_count);
void cpkt_sqlite_context_result_error_code(cpkt_sqlite_context *context,
                                           int code);
void cpkt_sqlite_context_result_value(cpkt_sqlite_context *context,
                                      const cpkt_sqlite_value *value);
void cpkt_sqlite_context_result_subtype(cpkt_sqlite_context *context,
                                        unsigned long subtype);
int cpkt_sqlite_value_type(const cpkt_sqlite_value *value);
int cpkt_sqlite_value_numeric_type(cpkt_sqlite_value *value);
int cpkt_sqlite_value_int(const cpkt_sqlite_value *value);
cpkt_sqlite_i64 cpkt_sqlite_value_i64(const cpkt_sqlite_value *value);
double cpkt_sqlite_value_double(const cpkt_sqlite_value *value);
const void *cpkt_sqlite_value_blob(const cpkt_sqlite_value *value);
const unsigned char *cpkt_sqlite_value_text(const cpkt_sqlite_value *value);
const void *cpkt_sqlite_value_text16(const cpkt_sqlite_value *value);
const void *cpkt_sqlite_value_text16le(const cpkt_sqlite_value *value);
const void *cpkt_sqlite_value_text16be(const cpkt_sqlite_value *value);
void *cpkt_sqlite_value_pointer(const cpkt_sqlite_value *value,
                                const char *type_name);
int cpkt_sqlite_value_bytes(const cpkt_sqlite_value *value);
int cpkt_sqlite_value_bytes16(const cpkt_sqlite_value *value);
int cpkt_sqlite_value_nochange(const cpkt_sqlite_value *value);
int cpkt_sqlite_value_from_bind(const cpkt_sqlite_value *value);
int cpkt_sqlite_value_encoding(const cpkt_sqlite_value *value);
unsigned long cpkt_sqlite_value_subtype(const cpkt_sqlite_value *value);
cpkt_sqlite_value *cpkt_sqlite_value_duplicate(const cpkt_sqlite_value *value);
void cpkt_sqlite_value_free(cpkt_sqlite_value *value);
/** @} */

/**
 * @name Session and changeset extension
 *
 * Changesets are owned facade buffers. The `_strm` operations forward input
 * and output callbacks to SQLite without materializing a whole changeset in
 * the facade; SQLite controls its own buffering and callback order. A NULL
 * rebase_out on extended apply operations disables native rebase collection.
 * A NULL conflict callback aborts an apply on conflict with CPKT_SQLITE_ABORT.
 * Callback and iterator views are valid only for their documented call or
 * receiver lifetime.
 * @{
 */
int cpkt_sqlite_session_new(cpkt_sqlite *database, const char *schema,
                            cpkt_sqlite_session **out);
int cpkt_sqlite_session_config(int operation, int *value);
int cpkt_sqlite_changeset_start(const cpkt_sqlite_changeset *changeset,
                                cpkt_sqlite_changeset_iterator **out);
int cpkt_sqlite_changeset_start_ex(const cpkt_sqlite_changeset *changeset,
                                   int flags,
                                   cpkt_sqlite_changeset_iterator **out);
int cpkt_sqlite_changeset_start_strm(cpkt_sqlite_stream_input_callback input,
                                     void *context, int flags,
                                     cpkt_sqlite_changeset_iterator **out);
int cpkt_sqlite_changeset_invert(const cpkt_sqlite_changeset *input,
                                 cpkt_sqlite_changeset **out);
int cpkt_sqlite_changeset_concat(const cpkt_sqlite_changeset *left,
                                 const cpkt_sqlite_changeset *right,
                                 cpkt_sqlite_changeset **out);
int cpkt_sqlite_changeset_invert_strm(cpkt_sqlite_stream_input_callback input,
                                      void *input_context,
                                      cpkt_sqlite_stream_output_callback output,
                                      void *output_context);
int cpkt_sqlite_changeset_concat_strm(
    cpkt_sqlite_stream_input_callback left_input, void *left_context,
    cpkt_sqlite_stream_input_callback right_input, void *right_context,
    cpkt_sqlite_stream_output_callback output, void *output_context);
int cpkt_sqlite_changeset_apply(
    cpkt_sqlite *database, const cpkt_sqlite_changeset *changeset,
    cpkt_sqlite_changeset_filter_callback filter,
    cpkt_sqlite_changeset_conflict_callback conflict, void *context);
int cpkt_sqlite_changeset_apply_strm(
    cpkt_sqlite *database, cpkt_sqlite_stream_input_callback input,
    void *input_context, cpkt_sqlite_changeset_filter_callback filter,
    cpkt_sqlite_changeset_conflict_callback conflict, void *context);
int cpkt_sqlite_changeset_apply_ex(
    cpkt_sqlite *database, const cpkt_sqlite_changeset *changeset,
    cpkt_sqlite_changeset_filter_callback filter,
    cpkt_sqlite_changeset_conflict_callback conflict, void *context, int flags,
    cpkt_sqlite_changeset **rebase_out);
int cpkt_sqlite_changeset_apply_ex_strm(
    cpkt_sqlite *database, cpkt_sqlite_stream_input_callback input,
    void *input_context, cpkt_sqlite_changeset_filter_callback filter,
    cpkt_sqlite_changeset_conflict_callback conflict, void *context, int flags,
    cpkt_sqlite_changeset **rebase_out);
int cpkt_sqlite_changeset_apply_v3(
    cpkt_sqlite *database, const cpkt_sqlite_changeset *changeset,
    cpkt_sqlite_changeset_iterator_filter_callback filter,
    cpkt_sqlite_changeset_conflict_callback conflict, void *context, int flags,
    cpkt_sqlite_changeset **rebase_out);
int cpkt_sqlite_changeset_apply_v3_strm(
    cpkt_sqlite *database, cpkt_sqlite_stream_input_callback input,
    void *input_context, cpkt_sqlite_changeset_iterator_filter_callback filter,
    cpkt_sqlite_changeset_conflict_callback conflict, void *context, int flags,
    cpkt_sqlite_changeset **rebase_out);
int cpkt_sqlite_rebaser_new(cpkt_sqlite_rebaser **out);
int cpkt_sqlite_changegroup_new(cpkt_sqlite_changegroup **out);
int cpkt_sqlite_statement_finalize(cpkt_sqlite_statement *self);

#include <cpkt/sqlite_constants.h>

/** @} */

#endif
