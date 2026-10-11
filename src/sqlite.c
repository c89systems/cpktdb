#include <cpkt/sqlite.h>

#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "sqlite_native_methods.h"
#include "sqlite_native_views.h"
#include <sqlite3.h>
#include <sqlite3rtree.h>
#include <sqlite3session.h>

typedef char cpkt_sqlite_word_is_at_least_32_bits
    [(sizeof(unsigned long) * CHAR_BIT >= 32) ? 1 : -1];
typedef char cpkt_sqlite_i64_is_64_bits[(sizeof(sqlite3_int64) * CHAR_BIT == 64)
                                            ? 1
                                            : -1];

typedef struct cpkt_sqlite_exec_context {
  cpkt_sqlite_row_callback callback;
  void *context;
} cpkt_sqlite_exec_context;

typedef struct cpkt_sqlite_state {
  cpkt_sqlite_busy_callback busy_callback;
  void *busy_context;
  cpkt_sqlite_authorizer_callback authorizer_callback;
  void *authorizer_context;
  cpkt_sqlite_collation_needed_callback collation_needed_callback;
  cpkt_sqlite_collation_needed16_callback collation_needed16_callback;
  void *collation_needed_context;
  cpkt_sqlite_trace_callback trace_callback;
  void *trace_context;
  cpkt_sqlite_legacy_trace_callback legacy_trace_callback;
  void *legacy_trace_context;
  cpkt_sqlite_legacy_profile_callback legacy_profile_callback;
  void *legacy_profile_context;
  struct cpkt_sqlite_unlock_notify_binding *unlock_notify_binding;
  struct cpkt_sqlite_rtree_geometry_binding *rtree_geometry_bindings;
  cpkt_sqlite_progress_callback progress_callback;
  void *progress_context;
  cpkt_sqlite_commit_callback commit_callback;
  void *commit_context;
  cpkt_sqlite_rollback_callback rollback_callback;
  void *rollback_context;
  cpkt_sqlite_update_callback update_callback;
  void *update_context;
  cpkt_sqlite_wal_callback wal_callback;
  void *wal_context;
  cpkt_sqlite_preupdate_callback preupdate_callback;
  void *preupdate_context;
  int session_count;
  int child_count;
  int closing;
  int finish_started;
  int listed_wrapper;
  int open_pending;
  struct cpkt_sqlite_state *next_wrapper;
  cpkt_sqlite *database;
} cpkt_sqlite_state;

typedef struct cpkt_sqlite_unlock_notify_binding {
  cpkt_sqlite_state *state;
  cpkt_sqlite_unlock_notify_callback callback;
  void *context;
  int delivered;
  int release_on_delivery;
} cpkt_sqlite_unlock_notify_binding;

typedef struct cpkt_sqlite_rtree_geometry_binding {
  void *context;
  cpkt_sqlite_rtree_geometry_callback callback;
  struct cpkt_sqlite_rtree_geometry_binding *next;
} cpkt_sqlite_rtree_geometry_binding;

typedef struct cpkt_sqlite_rtree_query_binding {
  void *context;
  cpkt_sqlite_rtree_query_callback callback;
  cpkt_sqlite_destroy_callback destroy;
} cpkt_sqlite_rtree_query_binding;

typedef struct cpkt_sqlite_function_binding {
  cpkt_sqlite *database;
  void *user_data;
  cpkt_sqlite_scalar_callback scalar;
  cpkt_sqlite_scalar_callback step;
  cpkt_sqlite_scalar_callback final;
  cpkt_sqlite_scalar_callback value;
  cpkt_sqlite_scalar_callback inverse;
  cpkt_sqlite_destroy_callback destroy;
} cpkt_sqlite_function_binding;

typedef struct cpkt_sqlite_auto_extension_binding {
  cpkt_sqlite_auto_extension *public_extension;
  cpkt_sqlite_auto_extension_callback callback;
  void *context;
  int registered;
  int active_callbacks;
  int close_requested;
  struct cpkt_sqlite_auto_extension_binding *next;
} cpkt_sqlite_auto_extension_binding;

typedef struct cpkt_sqlite_carray_i64_binding {
  sqlite3_int64 *values;
  void *destroy_argument;
  cpkt_sqlite_destroy_callback destroy;
} cpkt_sqlite_carray_i64_binding;

typedef struct cpkt_sqlite_memory_alarm_binding {
  cpkt_sqlite_memory_alarm_callback callback;
  void *context;
} cpkt_sqlite_memory_alarm_binding;

typedef struct cpkt_sqlite_auxdata_binding {
  void *data;
  cpkt_sqlite_destroy_callback destroy;
} cpkt_sqlite_auxdata_binding;

typedef struct cpkt_sqlite_collation_binding {
  void *context;
  cpkt_sqlite_collation_callback compare;
  cpkt_sqlite_destroy_callback destroy;
} cpkt_sqlite_collation_binding;

typedef struct cpkt_sqlite_autovacuum_binding {
  void *context;
  cpkt_sqlite_autovacuum_callback callback;
  cpkt_sqlite_destroy_callback destroy;
} cpkt_sqlite_autovacuum_binding;

typedef struct cpkt_sqlite_changeset_apply_context {
  cpkt_sqlite_changeset_filter_callback filter;
  cpkt_sqlite_changeset_iterator_filter_callback iterator_filter;
  cpkt_sqlite_changeset_conflict_callback conflict;
  void *user_context;
} cpkt_sqlite_changeset_apply_context;

typedef struct cpkt_sqlite_stream_input_context {
  cpkt_sqlite_stream_input_callback callback;
  void *user_context;
} cpkt_sqlite_stream_input_context;

typedef struct cpkt_sqlite_stream_output_context {
  cpkt_sqlite_stream_output_callback callback;
  void *user_context;
} cpkt_sqlite_stream_output_context;

typedef struct cpkt_sqlite_vfs_binding cpkt_sqlite_vfs_binding;
typedef struct cpkt_sqlite_vfs_file cpkt_sqlite_vfs_file;

struct cpkt_sqlite_file_handle {
  sqlite3_file *native;
  cpkt_sqlite_file *view;
  int owns_view;
};

struct cpkt_sqlite_vfs_handle {
  sqlite3_vfs *native;
  cpkt_sqlite_vfs *view;
  int owns_view;
};

struct cpkt_sqlite_vfs_binding {
  sqlite3_vfs native;
  cpkt_sqlite_vfs *public_vfs;
  int registered;
  struct cpkt_sqlite_vfs_binding *next;
};

struct cpkt_sqlite_vfs_file {
  sqlite3_file native;
  sqlite3_io_methods native_methods;
  cpkt_sqlite_file public_file;
  cpkt_sqlite_vfs_binding *binding;
};

typedef struct cpkt_sqlite_module_binding cpkt_sqlite_module_binding;

static cpkt_sqlite_state *cpkt_sqlite_wrapper_head;
static pthread_mutex_t cpkt_sqlite_bookkeeping_mutex =
    PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t cpkt_sqlite_auto_extension_transition_mutex =
    PTHREAD_MUTEX_INITIALIZER;
static const char cpkt_sqlite_open_clientdata_key[] =
    "cpkt_sqlite_open_wrapper";
static cpkt_sqlite_auto_extension_binding *cpkt_sqlite_auto_extension_head;
static cpkt_sqlite_vfs_binding *cpkt_sqlite_vfs_head;
static int cpkt_sqlite_auto_extension_trampoline_registered;
static cpkt_sqlite_memory_alarm_binding cpkt_sqlite_memory_alarm_state;

static cpkt_sqlite *cpkt_sqlite_wrap_database(sqlite3 *database,
                                              int close_on_failure);
typedef struct cpkt_sqlite_native_virtual_table
    cpkt_sqlite_native_virtual_table;
typedef struct cpkt_sqlite_native_virtual_cursor
    cpkt_sqlite_native_virtual_cursor;
typedef struct cpkt_sqlite_virtual_function_binding
    cpkt_sqlite_virtual_function_binding;

struct cpkt_sqlite_virtual_function_binding {
  cpkt_sqlite_function_binding function;
  cpkt_sqlite_virtual_function_binding *next;
};

struct cpkt_sqlite_module_binding {
  sqlite3_module module;
  cpkt_sqlite *database;
  cpkt_sqlite_module_methods methods;
};

struct cpkt_sqlite_native_virtual_table {
  sqlite3_vtab base;
  cpkt_sqlite_virtual_table *public_table;
  cpkt_sqlite_module_binding *module;
  cpkt_sqlite_virtual_function_binding *functions;
};

struct cpkt_sqlite_native_virtual_cursor {
  sqlite3_vtab_cursor base;
  cpkt_sqlite_virtual_cursor *public_cursor;
  cpkt_sqlite_native_virtual_table *table;
};

typedef struct cpkt_sqlite_fts5_tokenizer_binding {
  void *context;
  cpkt_sqlite_fts5_tokenizer_create_callback create;
  cpkt_sqlite_fts5_tokenizer_destroy_callback destroy;
  cpkt_sqlite_fts5_tokenizer_tokenize_callback tokenize;
  cpkt_sqlite_destroy_callback binding_destroy;
  fts5_tokenizer_v2 native_tokenizer;
} cpkt_sqlite_fts5_tokenizer_binding;

typedef struct cpkt_sqlite_fts5_token_context {
  void *context;
  int (*token)(void *, int, const char *, int, int, int);
} cpkt_sqlite_fts5_token_context;

typedef struct cpkt_sqlite_fts5_auxiliary_binding {
  cpkt_sqlite *database;
  void *user_data;
  cpkt_sqlite_fts5_auxiliary_callback callback;
  cpkt_sqlite_destroy_callback destroy;
} cpkt_sqlite_fts5_auxiliary_binding;

/* FTS5 owns sqlite3_user_data() for its SQL context. Keep callback-local
 * facade data separately without changing the public context layout. */
typedef struct cpkt_sqlite_fts5_sql_context_scope {
  cpkt_sqlite_context *context;
  void *user_data;
  struct cpkt_sqlite_fts5_sql_context_scope *next;
} cpkt_sqlite_fts5_sql_context_scope;

static cpkt_sqlite_fts5_sql_context_scope *cpkt_sqlite_fts5_sql_context_head;

typedef struct cpkt_sqlite_fts5_public_token_context {
  void *user_context;
  cpkt_sqlite_fts5_token_callback callback;
} cpkt_sqlite_fts5_public_token_context;

typedef struct cpkt_sqlite_fts5_query_phrase_context {
  cpkt_sqlite *database;
  void *user_data;
  cpkt_sqlite_fts5_query_phrase_callback callback;
} cpkt_sqlite_fts5_query_phrase_context;

typedef struct cpkt_sqlite_fts5_phrase_public_context {
  cpkt_sqlite_fts5_context public_context;
  void *user_data;
} cpkt_sqlite_fts5_phrase_public_context;

typedef struct cpkt_sqlite_page_cache_binding cpkt_sqlite_page_cache_binding;
typedef struct cpkt_sqlite_page_binding cpkt_sqlite_page_binding;

struct cpkt_sqlite_page_binding {
  sqlite3_pcache_page native_page;
  cpkt_sqlite_page *page;
};

struct cpkt_sqlite_page_cache_binding {
  cpkt_sqlite_page_cache *cache;
  cpkt_sqlite_page_cache_methods *methods;
};

static cpkt_sqlite_page_cache_methods cpkt_sqlite_page_cache_methods_current;
static int cpkt_sqlite_page_cache_methods_are_set;

static void cpkt_sqlite_destroy(cpkt_sqlite *self);
static void cpkt_sqlite_finish_close(cpkt_sqlite *self);

#define CPKT_SQLITE_WORD_MASK 0xffffffffUL

static sqlite3_int64 cpkt_sqlite_native_i64(cpkt_sqlite_i64 value);
static cpkt_sqlite_i64 cpkt_sqlite_public_i64(sqlite3_int64 value);
cpkt_sqlite_i64 cpkt_sqlite_i64_make(unsigned long high, unsigned long low);

static sqlite3 *cpkt_sqlite_native(const cpkt_sqlite *self) {
  return self == NULL ? NULL : (sqlite3 *)self->database;
}

static cpkt_sqlite_vfs_file *
cpkt_sqlite_vfs_file_from_native(sqlite3_file *file) {
  return (cpkt_sqlite_vfs_file *)file;
}

static int cpkt_sqlite_vfs_file_close(sqlite3_file *file) {
  cpkt_sqlite_vfs_file *native_file;
  native_file = cpkt_sqlite_vfs_file_from_native(file);
  if (native_file == NULL || native_file->public_file.methods == NULL ||
      native_file->public_file.methods->close == NULL)
    return SQLITE_OK;
  return native_file->public_file.methods->close(&native_file->public_file);
}

static int cpkt_sqlite_vfs_file_read(sqlite3_file *file, void *buffer,
                                     int byte_count, sqlite3_int64 offset) {
  cpkt_sqlite_vfs_file *native_file;
  native_file = cpkt_sqlite_vfs_file_from_native(file);
  if (native_file == NULL || native_file->public_file.methods == NULL ||
      native_file->public_file.methods->read == NULL)
    return SQLITE_IOERR_READ;
  return native_file->public_file.methods->read(&native_file->public_file,
                                                buffer, byte_count,
                                                cpkt_sqlite_public_i64(offset));
}

static int cpkt_sqlite_vfs_file_write(sqlite3_file *file, const void *buffer,
                                      int byte_count, sqlite3_int64 offset) {
  cpkt_sqlite_vfs_file *native_file;
  native_file = cpkt_sqlite_vfs_file_from_native(file);
  if (native_file == NULL || native_file->public_file.methods == NULL ||
      native_file->public_file.methods->write == NULL)
    return SQLITE_IOERR_WRITE;
  return native_file->public_file.methods->write(
      &native_file->public_file, buffer, byte_count,
      cpkt_sqlite_public_i64(offset));
}

static int cpkt_sqlite_vfs_file_truncate(sqlite3_file *file,
                                         sqlite3_int64 size) {
  cpkt_sqlite_vfs_file *native_file;
  native_file = cpkt_sqlite_vfs_file_from_native(file);
  if (native_file == NULL || native_file->public_file.methods == NULL ||
      native_file->public_file.methods->truncate == NULL)
    return SQLITE_IOERR_TRUNCATE;
  return native_file->public_file.methods->truncate(
      &native_file->public_file, cpkt_sqlite_public_i64(size));
}

static int cpkt_sqlite_vfs_file_sync(sqlite3_file *file, int flags) {
  cpkt_sqlite_vfs_file *native_file;
  native_file = cpkt_sqlite_vfs_file_from_native(file);
  if (native_file == NULL || native_file->public_file.methods == NULL ||
      native_file->public_file.methods->sync == NULL)
    return SQLITE_IOERR_FSYNC;
  return native_file->public_file.methods->sync(&native_file->public_file,
                                                flags);
}

static int cpkt_sqlite_vfs_file_size(sqlite3_file *file,
                                     sqlite3_int64 *size_out) {
  cpkt_sqlite_vfs_file *native_file;
  cpkt_sqlite_i64 public_size;
  int status;
  native_file = cpkt_sqlite_vfs_file_from_native(file);
  if (size_out != NULL)
    *size_out = 0;
  if (native_file == NULL || native_file->public_file.methods == NULL ||
      native_file->public_file.methods->size == NULL || size_out == NULL)
    return SQLITE_IOERR_FSTAT;
  public_size = cpkt_sqlite_i64_make(0, 0);
  status = native_file->public_file.methods->size(&native_file->public_file,
                                                  &public_size);
  if (status == SQLITE_OK)
    *size_out = cpkt_sqlite_native_i64(public_size);
  return status;
}

static int cpkt_sqlite_vfs_file_lock(sqlite3_file *file, int level) {
  cpkt_sqlite_vfs_file *native_file;
  native_file = cpkt_sqlite_vfs_file_from_native(file);
  if (native_file == NULL || native_file->public_file.methods == NULL ||
      native_file->public_file.methods->lock == NULL)
    return SQLITE_IOERR_LOCK;
  return native_file->public_file.methods->lock(&native_file->public_file,
                                                level);
}

static int cpkt_sqlite_vfs_file_unlock(sqlite3_file *file, int level) {
  cpkt_sqlite_vfs_file *native_file;
  native_file = cpkt_sqlite_vfs_file_from_native(file);
  if (native_file == NULL || native_file->public_file.methods == NULL ||
      native_file->public_file.methods->unlock == NULL)
    return SQLITE_IOERR_UNLOCK;
  return native_file->public_file.methods->unlock(&native_file->public_file,
                                                  level);
}

static int cpkt_sqlite_vfs_file_reserved_lock(sqlite3_file *file,
                                              int *result_out) {
  cpkt_sqlite_vfs_file *native_file;
  native_file = cpkt_sqlite_vfs_file_from_native(file);
  if (result_out != NULL)
    *result_out = 0;
  if (native_file == NULL || native_file->public_file.methods == NULL ||
      native_file->public_file.methods->check_reserved_lock == NULL)
    return SQLITE_IOERR_CHECKRESERVEDLOCK;
  return native_file->public_file.methods->check_reserved_lock(
      &native_file->public_file, result_out);
}

static int cpkt_sqlite_file_control_has_i64(int operation) {
  return operation == SQLITE_FCNTL_SIZE_HINT ||
         operation == SQLITE_FCNTL_MMAP_SIZE ||
         operation == SQLITE_FCNTL_SIZE_LIMIT;
}

static int cpkt_sqlite_vfs_file_control(sqlite3_file *file, int operation,
                                        void *argument) {
  cpkt_sqlite_vfs_file *native_file;
  cpkt_sqlite_i64 public_value;
  int status;
  native_file = cpkt_sqlite_vfs_file_from_native(file);
  if (native_file == NULL || native_file->public_file.methods == NULL ||
      native_file->public_file.methods->control == NULL)
    return SQLITE_NOTFOUND;
  if (argument == NULL || !cpkt_sqlite_file_control_has_i64(operation))
    return native_file->public_file.methods->control(&native_file->public_file,
                                                     operation, argument);
  public_value = cpkt_sqlite_public_i64(*(sqlite3_int64 *)argument);
  status = native_file->public_file.methods->control(&native_file->public_file,
                                                     operation, &public_value);
  if (status == SQLITE_OK)
    *(sqlite3_int64 *)argument = cpkt_sqlite_native_i64(public_value);
  return status;
}

static int cpkt_sqlite_vfs_file_sector_size(sqlite3_file *file) {
  cpkt_sqlite_vfs_file *native_file;
  native_file = cpkt_sqlite_vfs_file_from_native(file);
  if (native_file == NULL || native_file->public_file.methods == NULL ||
      native_file->public_file.methods->sector_size == NULL)
    return 0;
  return native_file->public_file.methods->sector_size(
      &native_file->public_file);
}

static int cpkt_sqlite_vfs_file_characteristics(sqlite3_file *file) {
  cpkt_sqlite_vfs_file *native_file;
  native_file = cpkt_sqlite_vfs_file_from_native(file);
  if (native_file == NULL || native_file->public_file.methods == NULL ||
      native_file->public_file.methods->characteristics == NULL)
    return 0;
  return native_file->public_file.methods->characteristics(
      &native_file->public_file);
}

static int cpkt_sqlite_vfs_file_shm_map(sqlite3_file *file, int page,
                                        int page_byte_count, int extend,
                                        void volatile **out) {
  cpkt_sqlite_vfs_file *native_file;
  native_file = cpkt_sqlite_vfs_file_from_native(file);
  if (out != NULL)
    *out = NULL;
  if (native_file == NULL || native_file->public_file.methods == NULL ||
      native_file->public_file.methods->shm_map == NULL)
    return SQLITE_IOERR_SHMMAP;
  return native_file->public_file.methods->shm_map(
      &native_file->public_file, page, page_byte_count, extend, out);
}

static int cpkt_sqlite_vfs_file_shm_lock(sqlite3_file *file, int offset,
                                         int count, int flags) {
  cpkt_sqlite_vfs_file *native_file;
  native_file = cpkt_sqlite_vfs_file_from_native(file);
  if (native_file == NULL || native_file->public_file.methods == NULL ||
      native_file->public_file.methods->shm_lock == NULL)
    return SQLITE_IOERR_SHMLOCK;
  return native_file->public_file.methods->shm_lock(&native_file->public_file,
                                                    offset, count, flags);
}

static void cpkt_sqlite_vfs_file_shm_barrier(sqlite3_file *file) {
  cpkt_sqlite_vfs_file *native_file;
  native_file = cpkt_sqlite_vfs_file_from_native(file);
  if (native_file != NULL && native_file->public_file.methods != NULL &&
      native_file->public_file.methods->shm_barrier != NULL) {
    native_file->public_file.methods->shm_barrier(&native_file->public_file);
  }
}

static int cpkt_sqlite_vfs_file_shm_unmap(sqlite3_file *file, int delete_flag) {
  cpkt_sqlite_vfs_file *native_file;
  native_file = cpkt_sqlite_vfs_file_from_native(file);
  if (native_file == NULL || native_file->public_file.methods == NULL ||
      native_file->public_file.methods->shm_unmap == NULL)
    return SQLITE_IOERR_SHMOPEN;
  return native_file->public_file.methods->shm_unmap(&native_file->public_file,
                                                     delete_flag);
}

static int cpkt_sqlite_vfs_file_fetch(sqlite3_file *file, sqlite3_int64 offset,
                                      int byte_count, void **out) {
  cpkt_sqlite_vfs_file *native_file;
  native_file = cpkt_sqlite_vfs_file_from_native(file);
  if (out != NULL)
    *out = NULL;
  if (native_file == NULL || native_file->public_file.methods == NULL ||
      native_file->public_file.methods->fetch == NULL)
    return SQLITE_OK;
  return native_file->public_file.methods->fetch(&native_file->public_file,
                                                 cpkt_sqlite_public_i64(offset),
                                                 byte_count, out);
}

static int cpkt_sqlite_vfs_file_unfetch(sqlite3_file *file,
                                        sqlite3_int64 offset, void *memory) {
  cpkt_sqlite_vfs_file *native_file;
  native_file = cpkt_sqlite_vfs_file_from_native(file);
  if (native_file == NULL || native_file->public_file.methods == NULL ||
      native_file->public_file.methods->unfetch == NULL)
    return SQLITE_OK;
  return native_file->public_file.methods->unfetch(
      &native_file->public_file, cpkt_sqlite_public_i64(offset), memory);
}

static int cpkt_sqlite_vfs_open_native(sqlite3_vfs *vfs, sqlite3_filename name,
                                       sqlite3_file *file, int flags,
                                       int *flags_out) {
  cpkt_sqlite_vfs_binding *binding;
  cpkt_sqlite_vfs_file *native_file;
  cpkt_sqlite_vfs *public_vfs;
  int status;
  binding = (cpkt_sqlite_vfs_binding *)vfs;
  native_file = cpkt_sqlite_vfs_file_from_native(file);
  public_vfs = binding == NULL ? NULL : binding->public_vfs;
  if (native_file == NULL || public_vfs == NULL ||
      public_vfs->methods.open == NULL) {
    return SQLITE_CANTOPEN;
  }
  memset(native_file, 0, sizeof(*native_file));
  native_file->binding = binding;
  /* Leave the receiver unbound so the callback can delegate to a native VFS. */
  status = public_vfs->methods.open(public_vfs, name, &native_file->public_file,
                                    flags, flags_out);
  if (native_file->public_file.methods == NULL)
    return status == SQLITE_OK ? SQLITE_MISUSE : status;
  native_file->native_methods.iVersion =
      native_file->public_file.methods->version;
  native_file->native_methods.xClose = cpkt_sqlite_vfs_file_close;
  native_file->native_methods.xRead = cpkt_sqlite_vfs_file_read;
  native_file->native_methods.xWrite = cpkt_sqlite_vfs_file_write;
  native_file->native_methods.xTruncate = cpkt_sqlite_vfs_file_truncate;
  native_file->native_methods.xSync = cpkt_sqlite_vfs_file_sync;
  native_file->native_methods.xFileSize = cpkt_sqlite_vfs_file_size;
  native_file->native_methods.xLock = cpkt_sqlite_vfs_file_lock;
  native_file->native_methods.xUnlock = cpkt_sqlite_vfs_file_unlock;
  native_file->native_methods.xCheckReservedLock =
      cpkt_sqlite_vfs_file_reserved_lock;
  native_file->native_methods.xFileControl = cpkt_sqlite_vfs_file_control;
  native_file->native_methods.xSectorSize = cpkt_sqlite_vfs_file_sector_size;
  native_file->native_methods.xDeviceCharacteristics =
      cpkt_sqlite_vfs_file_characteristics;
  native_file->native_methods.xShmMap =
      native_file->public_file.methods->shm_map == NULL
          ? NULL
          : cpkt_sqlite_vfs_file_shm_map;
  native_file->native_methods.xShmLock =
      native_file->public_file.methods->shm_lock == NULL
          ? NULL
          : cpkt_sqlite_vfs_file_shm_lock;
  native_file->native_methods.xShmBarrier =
      native_file->public_file.methods->shm_barrier == NULL
          ? NULL
          : cpkt_sqlite_vfs_file_shm_barrier;
  native_file->native_methods.xShmUnmap =
      native_file->public_file.methods->shm_unmap == NULL
          ? NULL
          : cpkt_sqlite_vfs_file_shm_unmap;
  native_file->native_methods.xFetch =
      native_file->public_file.methods->fetch == NULL
          ? NULL
          : cpkt_sqlite_vfs_file_fetch;
  native_file->native_methods.xUnfetch =
      native_file->public_file.methods->unfetch == NULL
          ? NULL
          : cpkt_sqlite_vfs_file_unfetch;
  native_file->native.pMethods = &native_file->native_methods;
  return status;
}

static cpkt_sqlite_vfs *cpkt_sqlite_vfs_public(sqlite3_vfs *vfs) {
  cpkt_sqlite_vfs_binding *binding;
  binding = (cpkt_sqlite_vfs_binding *)vfs;
  return binding == NULL ? NULL : binding->public_vfs;
}

static int cpkt_sqlite_vfs_delete_native(sqlite3_vfs *vfs, const char *name,
                                         int sync_directory) {
  cpkt_sqlite_vfs *public_vfs;
  public_vfs = cpkt_sqlite_vfs_public(vfs);
  if (public_vfs == NULL || public_vfs->methods.delete_file == NULL)
    return SQLITE_IOERR_DELETE;
  return public_vfs->methods.delete_file(public_vfs, name, sync_directory);
}

static int cpkt_sqlite_vfs_access_native(sqlite3_vfs *vfs, const char *name,
                                         int flags, int *result_out) {
  cpkt_sqlite_vfs *public_vfs;
  public_vfs = cpkt_sqlite_vfs_public(vfs);
  if (result_out != NULL)
    *result_out = 0;
  if (public_vfs == NULL || public_vfs->methods.access == NULL)
    return SQLITE_IOERR_ACCESS;
  return public_vfs->methods.access(public_vfs, name, flags, result_out);
}

static int cpkt_sqlite_vfs_full_path_native(sqlite3_vfs *vfs, const char *name,
                                            int output_byte_count,
                                            char *output) {
  cpkt_sqlite_vfs *public_vfs;
  public_vfs = cpkt_sqlite_vfs_public(vfs);
  if (public_vfs == NULL || public_vfs->methods.full_path == NULL)
    return SQLITE_CANTOPEN;
  return public_vfs->methods.full_path(public_vfs, name, output_byte_count,
                                       output);
}

static void *cpkt_sqlite_vfs_dl_open_native(sqlite3_vfs *vfs,
                                            const char *name) {
  cpkt_sqlite_vfs *public_vfs;
  public_vfs = cpkt_sqlite_vfs_public(vfs);
  return public_vfs == NULL || public_vfs->methods.dl_open == NULL
             ? NULL
             : public_vfs->methods.dl_open(public_vfs, name);
}

static void cpkt_sqlite_vfs_dl_error_native(sqlite3_vfs *vfs, int byte_count,
                                            char *message) {
  cpkt_sqlite_vfs *public_vfs;
  public_vfs = cpkt_sqlite_vfs_public(vfs);
  if (public_vfs != NULL && public_vfs->methods.dl_error != NULL) {
    public_vfs->methods.dl_error(public_vfs, byte_count, message);
  }
}

static void (*cpkt_sqlite_vfs_dl_symbol_native(sqlite3_vfs *vfs, void *handle,
                                               const char *symbol))(void) {
  cpkt_sqlite_vfs *public_vfs;
  public_vfs = cpkt_sqlite_vfs_public(vfs);
  return public_vfs == NULL || public_vfs->methods.dl_symbol == NULL
             ? NULL
             : public_vfs->methods.dl_symbol(public_vfs, handle, symbol);
}

static void cpkt_sqlite_vfs_dl_close_native(sqlite3_vfs *vfs, void *handle) {
  cpkt_sqlite_vfs *public_vfs;
  public_vfs = cpkt_sqlite_vfs_public(vfs);
  if (public_vfs != NULL && public_vfs->methods.dl_close != NULL) {
    public_vfs->methods.dl_close(public_vfs, handle);
  }
}

static int cpkt_sqlite_vfs_randomness_native(sqlite3_vfs *vfs, int byte_count,
                                             char *output) {
  cpkt_sqlite_vfs *public_vfs;
  public_vfs = cpkt_sqlite_vfs_public(vfs);
  return public_vfs == NULL || public_vfs->methods.randomness == NULL
             ? 0
             : public_vfs->methods.randomness(public_vfs, byte_count, output);
}

static int cpkt_sqlite_vfs_sleep_native(sqlite3_vfs *vfs, int microseconds) {
  cpkt_sqlite_vfs *public_vfs;
  public_vfs = cpkt_sqlite_vfs_public(vfs);
  return public_vfs == NULL || public_vfs->methods.sleep == NULL
             ? 0
             : public_vfs->methods.sleep(public_vfs, microseconds);
}

static int cpkt_sqlite_vfs_current_time_native(sqlite3_vfs *vfs, double *out) {
  cpkt_sqlite_vfs *public_vfs;
  public_vfs = cpkt_sqlite_vfs_public(vfs);
  return public_vfs == NULL || public_vfs->methods.current_time == NULL
             ? SQLITE_ERROR
             : public_vfs->methods.current_time(public_vfs, out);
}

static int cpkt_sqlite_vfs_last_error_native(sqlite3_vfs *vfs, int byte_count,
                                             char *message) {
  cpkt_sqlite_vfs *public_vfs;
  public_vfs = cpkt_sqlite_vfs_public(vfs);
  return public_vfs == NULL || public_vfs->methods.last_error == NULL
             ? 0
             : public_vfs->methods.last_error(public_vfs, byte_count, message);
}

static int cpkt_sqlite_vfs_current_time_i64_native(sqlite3_vfs *vfs,
                                                   sqlite3_int64 *out) {
  cpkt_sqlite_vfs *public_vfs;
  cpkt_sqlite_i64 public_time;
  int status;
  public_vfs = cpkt_sqlite_vfs_public(vfs);
  if (out != NULL)
    *out = 0;
  if (public_vfs == NULL || public_vfs->methods.current_time_i64 == NULL ||
      out == NULL) {
    return SQLITE_ERROR;
  }
  public_time = cpkt_sqlite_i64_make(0, 0);
  status = public_vfs->methods.current_time_i64(public_vfs, &public_time);
  if (status == SQLITE_OK)
    *out = cpkt_sqlite_native_i64(public_time);
  return status;
}

static int cpkt_sqlite_vfs_set_system_call_native(sqlite3_vfs *vfs,
                                                  const char *name,
                                                  sqlite3_syscall_ptr symbol) {
  cpkt_sqlite_vfs *public_vfs;
  public_vfs = cpkt_sqlite_vfs_public(vfs);
  if (public_vfs == NULL || public_vfs->methods.set_system_call == NULL) {
    return SQLITE_NOTFOUND;
  }
  return public_vfs->methods.set_system_call(public_vfs, name, symbol);
}

static sqlite3_syscall_ptr
cpkt_sqlite_vfs_get_system_call_native(sqlite3_vfs *vfs, const char *name) {
  cpkt_sqlite_vfs *public_vfs;
  public_vfs = cpkt_sqlite_vfs_public(vfs);
  return public_vfs == NULL || public_vfs->methods.get_system_call == NULL
             ? NULL
             : public_vfs->methods.get_system_call(public_vfs, name);
}

static const char *cpkt_sqlite_vfs_next_system_call_native(sqlite3_vfs *vfs,
                                                           const char *name) {
  cpkt_sqlite_vfs *public_vfs;
  public_vfs = cpkt_sqlite_vfs_public(vfs);
  return public_vfs == NULL || public_vfs->methods.next_system_call == NULL
             ? NULL
             : public_vfs->methods.next_system_call(public_vfs, name);
}

static sqlite3_stmt *
cpkt_sqlite_native_statement(const cpkt_sqlite_statement *self) {
  return self == NULL ? NULL : (sqlite3_stmt *)self->statement;
}

static sqlite3_blob *cpkt_sqlite_native_blob(const cpkt_sqlite_blob *self) {
  return self == NULL ? NULL : (sqlite3_blob *)self->blob;
}

static sqlite3_backup *
cpkt_sqlite_native_backup(const cpkt_sqlite_backup *self) {
  return self == NULL ? NULL : (sqlite3_backup *)self->backup;
}

static sqlite3_value *cpkt_sqlite_native_value(const cpkt_sqlite_value *self) {
  return self == NULL ? NULL : (sqlite3_value *)self->value;
}

static sqlite3_context *
cpkt_sqlite_native_context(const cpkt_sqlite_context *self) {
  return self == NULL ? NULL : (sqlite3_context *)self->context;
}

static sqlite3_session *
cpkt_sqlite_native_session(const cpkt_sqlite_session *self) {
  return self == NULL ? NULL : (sqlite3_session *)self->session;
}

static sqlite3_changeset_iter *
cpkt_sqlite_native_iterator(const cpkt_sqlite_changeset_iterator *self) {
  return self == NULL ? NULL : (sqlite3_changeset_iter *)self->iterator;
}

static sqlite3_rebaser *
cpkt_sqlite_native_rebaser(const cpkt_sqlite_rebaser *self) {
  return self == NULL ? NULL : (sqlite3_rebaser *)self->rebaser;
}

static sqlite3_changegroup *
cpkt_sqlite_native_changegroup(const cpkt_sqlite_changegroup *self) {
  return self == NULL ? NULL : (sqlite3_changegroup *)self->changegroup;
}

static sqlite3_str *cpkt_sqlite_native_string(const cpkt_sqlite_string *self) {
  return self == NULL ? NULL : (sqlite3_str *)self->string;
}

static sqlite3_filename
cpkt_sqlite_native_filename(const cpkt_sqlite_filename *self) {
  return self == NULL ? NULL : (sqlite3_filename)self->filename;
}

static sqlite3_mutex *cpkt_sqlite_native_mutex(const cpkt_sqlite_mutex *self) {
  return self == NULL ? NULL : (sqlite3_mutex *)self->mutex;
}

static fts5_api *cpkt_sqlite_native_fts5_api(const cpkt_sqlite_fts5_api *self) {
  return self == NULL ? NULL : (fts5_api *)self->api;
}

static const Fts5ExtensionApi *
cpkt_sqlite_native_fts5_extension_api(const cpkt_sqlite_fts5_context *self) {
  return self == NULL ? NULL : (const Fts5ExtensionApi *)self->api;
}

static Fts5Context *
cpkt_sqlite_native_fts5_context(const cpkt_sqlite_fts5_context *self) {
  return self == NULL ? NULL : (Fts5Context *)self->context;
}

static cpkt_sqlite_state *cpkt_sqlite_state_for(const cpkt_sqlite *self) {
  return self == NULL ? NULL : (cpkt_sqlite_state *)self->facade_state;
}

static pthread_mutex_t *cpkt_sqlite_global_mutex(void) {
  return &cpkt_sqlite_bookkeeping_mutex;
}

static void cpkt_sqlite_global_lock(pthread_mutex_t *mutex) {
  (void)pthread_mutex_lock(mutex);
}

static void cpkt_sqlite_global_unlock(pthread_mutex_t *mutex) {
  (void)pthread_mutex_unlock(mutex);
}

static sqlite3_mutex *cpkt_sqlite_connection_lock(cpkt_sqlite *self) {
  sqlite3_mutex *mutex;
  mutex = sqlite3_db_mutex(cpkt_sqlite_native(self));
  if (mutex != NULL)
    sqlite3_mutex_enter(mutex);
  return mutex;
}

static void cpkt_sqlite_connection_unlock(sqlite3_mutex *mutex) {
  if (mutex != NULL)
    sqlite3_mutex_leave(mutex);
}

static void cpkt_sqlite_memory_alarm_trampoline(void *context,
                                                sqlite3_int64 requested_bytes,
                                                int prior_allocation_failed) {
  cpkt_sqlite_memory_alarm_binding *binding;
  cpkt_sqlite_memory_alarm_callback callback;
  void *callback_context;
  pthread_mutex_t *mutex;
  binding = (cpkt_sqlite_memory_alarm_binding *)context;
  if (binding == NULL)
    return;
  mutex = cpkt_sqlite_global_mutex();
  cpkt_sqlite_global_lock(mutex);
  callback = binding->callback;
  callback_context = binding->context;
  cpkt_sqlite_global_unlock(mutex);
  if (callback != NULL) {
    callback(callback_context, cpkt_sqlite_public_i64(requested_bytes),
             prior_allocation_failed);
  }
}

static void
cpkt_sqlite_auto_extension_free(cpkt_sqlite_auto_extension_binding *binding) {
  if (binding == NULL)
    return;
  free(binding->public_extension);
  free(binding);
}

static int
cpkt_sqlite_auto_extension_trampoline(sqlite3 *native_database,
                                      char **error_out,
                                      const sqlite3_api_routines *api) {
  cpkt_sqlite_auto_extension_binding *binding;
  cpkt_sqlite_auto_extension_binding **snapshot;
  cpkt_sqlite *database;
  pthread_mutex_t *mutex;
  int count;
  int index;
  int status;
  (void)api;
  if (error_out != NULL)
    *error_out = NULL;
  database = cpkt_sqlite_wrap_database(native_database, 0);
  if (database == NULL)
    return SQLITE_NOMEM;
  mutex = cpkt_sqlite_global_mutex();
  cpkt_sqlite_global_lock(mutex);
  count = 0;
  binding = cpkt_sqlite_auto_extension_head;
  while (binding != NULL) {
    if (binding->registered)
      ++count;
    binding = binding->next;
  }
  snapshot = count == 0 ? NULL
                        : (cpkt_sqlite_auto_extension_binding **)calloc(
                              (size_t)count, sizeof(*snapshot));
  if (count != 0 && snapshot == NULL) {
    cpkt_sqlite_global_unlock(mutex);
    return SQLITE_NOMEM;
  }
  index = 0;
  binding = cpkt_sqlite_auto_extension_head;
  while (binding != NULL) {
    if (binding->registered) {
      ++binding->active_callbacks;
      snapshot[index] = binding;
      ++index;
    }
    binding = binding->next;
  }
  cpkt_sqlite_global_unlock(mutex);
  status = SQLITE_OK;
  for (index = 0; index < count && status == SQLITE_OK; ++index) {
    binding = snapshot[index];
    status = binding->callback(database, error_out, binding->context);
  }
  cpkt_sqlite_global_lock(mutex);
  for (index = 0; index < count; ++index) {
    binding = snapshot[index];
    if (binding->active_callbacks > 0)
      --binding->active_callbacks;
    if (binding->close_requested && binding->active_callbacks == 0) {
      cpkt_sqlite_auto_extension_free(binding);
    }
  }
  cpkt_sqlite_global_unlock(mutex);
  free(snapshot);
  return status;
}

static int
cpkt_sqlite_auto_extension_register(cpkt_sqlite_auto_extension *self) {
  cpkt_sqlite_auto_extension_binding *binding;
  cpkt_sqlite_auto_extension_binding **link;
  pthread_mutex_t *mutex;
  int needs_native_registration;
  int status;
  if (self == NULL || self->internal == NULL)
    return SQLITE_MISUSE;
  binding = (cpkt_sqlite_auto_extension_binding *)self->internal;
  mutex = cpkt_sqlite_global_mutex();
  cpkt_sqlite_global_lock(&cpkt_sqlite_auto_extension_transition_mutex);
  cpkt_sqlite_global_lock(mutex);
  if (binding->close_requested) {
    cpkt_sqlite_global_unlock(mutex);
    cpkt_sqlite_global_unlock(&cpkt_sqlite_auto_extension_transition_mutex);
    return SQLITE_MISUSE;
  }
  if (binding->registered) {
    cpkt_sqlite_global_unlock(mutex);
    cpkt_sqlite_global_unlock(&cpkt_sqlite_auto_extension_transition_mutex);
    return SQLITE_OK;
  }
  needs_native_registration = !cpkt_sqlite_auto_extension_trampoline_registered;
  cpkt_sqlite_global_unlock(mutex);
  if (needs_native_registration) {
    status = sqlite3_auto_extension(
        (void (*)(void))cpkt_sqlite_auto_extension_trampoline);
    if (status != SQLITE_OK) {
      cpkt_sqlite_global_unlock(&cpkt_sqlite_auto_extension_transition_mutex);
      return status;
    }
  }
  cpkt_sqlite_global_lock(mutex);
  if (needs_native_registration)
    cpkt_sqlite_auto_extension_trampoline_registered = 1;
  /* Constructors establish ownership, not callback order. A successful new
   * registration goes last; repeated registration above remains a no-op. */
  link = &cpkt_sqlite_auto_extension_head;
  while (*link != NULL && *link != binding)
    link = &(*link)->next;
  if (*link == binding)
    *link = binding->next;
  link = &cpkt_sqlite_auto_extension_head;
  while (*link != NULL)
    link = &(*link)->next;
  binding->next = NULL;
  *link = binding;
  binding->registered = 1;
  cpkt_sqlite_global_unlock(mutex);
  cpkt_sqlite_global_unlock(&cpkt_sqlite_auto_extension_transition_mutex);
  return SQLITE_OK;
}

static int cpkt_sqlite_auto_extension_cancel(cpkt_sqlite_auto_extension *self) {
  cpkt_sqlite_auto_extension_binding *binding;
  cpkt_sqlite_auto_extension_binding *current;
  pthread_mutex_t *mutex;
  int any_registered;
  int was_registered;
  int status;
  if (self == NULL || self->internal == NULL)
    return SQLITE_MISUSE;
  binding = (cpkt_sqlite_auto_extension_binding *)self->internal;
  mutex = cpkt_sqlite_global_mutex();
  cpkt_sqlite_global_lock(&cpkt_sqlite_auto_extension_transition_mutex);
  cpkt_sqlite_global_lock(mutex);
  was_registered = binding->registered;
  if (!was_registered) {
    cpkt_sqlite_global_unlock(mutex);
    cpkt_sqlite_global_unlock(&cpkt_sqlite_auto_extension_transition_mutex);
    return 0;
  }
  binding->registered = 0;
  any_registered = 0;
  current = cpkt_sqlite_auto_extension_head;
  while (current != NULL) {
    if (current->registered)
      any_registered = 1;
    current = current->next;
  }
  status = SQLITE_OK;
  if (!any_registered && cpkt_sqlite_auto_extension_trampoline_registered) {
    cpkt_sqlite_global_unlock(mutex);
    status = sqlite3_cancel_auto_extension(
        (void (*)(void))cpkt_sqlite_auto_extension_trampoline);
    cpkt_sqlite_global_lock(mutex);
    cpkt_sqlite_auto_extension_trampoline_registered = 0;
  }
  cpkt_sqlite_global_unlock(mutex);
  cpkt_sqlite_global_unlock(&cpkt_sqlite_auto_extension_transition_mutex);
  return status == SQLITE_OK ? 1 : status;
}

static void cpkt_sqlite_auto_extension_close(cpkt_sqlite_auto_extension *self) {
  cpkt_sqlite_auto_extension_binding **link;
  cpkt_sqlite_auto_extension_binding *binding;
  pthread_mutex_t *mutex;
  if (self == NULL || self->internal == NULL)
    return;
  (void)cpkt_sqlite_auto_extension_cancel(self);
  binding = (cpkt_sqlite_auto_extension_binding *)self->internal;
  mutex = cpkt_sqlite_global_mutex();
  cpkt_sqlite_global_lock(mutex);
  link = &cpkt_sqlite_auto_extension_head;
  while (*link != NULL && *link != binding)
    link = &(*link)->next;
  if (*link == binding)
    *link = binding->next;
  binding->close_requested = 1;
  self->internal = NULL;
  if (binding->active_callbacks == 0) {
    cpkt_sqlite_auto_extension_free(binding);
  }
  cpkt_sqlite_global_unlock(mutex);
}

static void cpkt_sqlite_retain_child(cpkt_sqlite *database) {
  cpkt_sqlite_state *state;
  pthread_mutex_t *mutex;
  mutex = cpkt_sqlite_global_mutex();
  cpkt_sqlite_global_lock(mutex);
  state = cpkt_sqlite_state_for(database);
  if (state != NULL)
    ++state->child_count;
  cpkt_sqlite_global_unlock(mutex);
}

static void cpkt_sqlite_release_child(cpkt_sqlite *database) {
  cpkt_sqlite_state *state;
  pthread_mutex_t *mutex;
  int finish;
  finish = 0;
  mutex = cpkt_sqlite_global_mutex();
  cpkt_sqlite_global_lock(mutex);
  state = cpkt_sqlite_state_for(database);
  if (state != NULL && state->child_count > 0) {
    --state->child_count;
    if (state->closing && state->child_count == 0 && !state->finish_started) {
      state->finish_started = 1;
      finish = 1;
    }
  }
  cpkt_sqlite_global_unlock(mutex);
  if (finish)
    cpkt_sqlite_finish_close(database);
}

static sqlite3_int64 cpkt_sqlite_native_i64(cpkt_sqlite_i64 value) {
  sqlite3_uint64 bits;
  bits = ((sqlite3_uint64)(value.high & CPKT_SQLITE_WORD_MASK) << 32) |
         (sqlite3_uint64)(value.low & CPKT_SQLITE_WORD_MASK);
  return (sqlite3_int64)bits;
}

static void cpkt_sqlite_carray_i64_destroy(void *argument) {
  cpkt_sqlite_carray_i64_binding *binding;
  binding = (cpkt_sqlite_carray_i64_binding *)argument;
  if (binding == NULL)
    return;
  if (binding->destroy != NULL)
    binding->destroy(binding->destroy_argument);
  free(binding->values);
  free(binding);
}

static cpkt_sqlite_carray_i64_binding *
cpkt_sqlite_carray_i64_binding_new(void *data, int element_count,
                                   cpkt_sqlite_destroy_callback destroy,
                                   void *destroy_argument) {
  cpkt_sqlite_carray_i64_binding *binding;
  cpkt_sqlite_i64 *public_values;
  int index;
  if (data == NULL || element_count < 1)
    return NULL;
  binding = (cpkt_sqlite_carray_i64_binding *)calloc(1, sizeof(*binding));
  if (binding == NULL)
    return NULL;
  if ((size_t)element_count > ((size_t)-1) / sizeof(*binding->values)) {
    free(binding);
    return NULL;
  }
  binding->values =
      (sqlite3_int64 *)malloc((size_t)element_count * sizeof(*binding->values));
  if (binding->values == NULL) {
    free(binding);
    return NULL;
  }
  public_values = (cpkt_sqlite_i64 *)data;
  for (index = 0; index < element_count; ++index)
    binding->values[index] = cpkt_sqlite_native_i64(public_values[index]);
  binding->destroy = destroy;
  binding->destroy_argument = destroy_argument;
  return binding;
}

static cpkt_sqlite_i64 cpkt_sqlite_public_i64(sqlite3_int64 value) {
  cpkt_sqlite_i64 public_value;
  sqlite3_uint64 bits;
  bits = (sqlite3_uint64)value;
  public_value.high = (unsigned long)((bits >> 32) & CPKT_SQLITE_WORD_MASK);
  public_value.low = (unsigned long)(bits & CPKT_SQLITE_WORD_MASK);
  return public_value;
}

static sqlite3_uint64 cpkt_sqlite_native_u64(cpkt_sqlite_u64 value) {
  return ((sqlite3_uint64)(value.high & CPKT_SQLITE_WORD_MASK) << 32) |
         (sqlite3_uint64)(value.low & CPKT_SQLITE_WORD_MASK);
}

static cpkt_sqlite_u64 cpkt_sqlite_public_u64(sqlite3_uint64 value) {
  cpkt_sqlite_u64 public_value;
  public_value.high = (unsigned long)((value >> 32) & CPKT_SQLITE_WORD_MASK);
  public_value.low = (unsigned long)(value & CPKT_SQLITE_WORD_MASK);
  return public_value;
}

static int cpkt_sqlite_busy_trampoline(void *context, int retry_count) {
  cpkt_sqlite_state *state;
  state = (cpkt_sqlite_state *)context;
  if (state == NULL || state->busy_callback == NULL)
    return 0;
  return state->busy_callback(state->busy_context, retry_count);
}

static int cpkt_sqlite_authorizer_trampoline(void *context, int action,
                                             const char *detail1,
                                             const char *detail2,
                                             const char *database,
                                             const char *trigger) {
  cpkt_sqlite_state *state;
  state = (cpkt_sqlite_state *)context;
  if (state == NULL || state->authorizer_callback == NULL)
    return SQLITE_OK;
  return state->authorizer_callback(state->authorizer_context, action, detail1,
                                    detail2, database, trigger);
}

static void cpkt_sqlite_collation_needed_trampoline(void *context,
                                                    sqlite3 *native_database,
                                                    int text_representation,
                                                    const char *name) {
  cpkt_sqlite_state *state;
  (void)native_database;
  state = (cpkt_sqlite_state *)context;
  if (state != NULL && state->collation_needed_callback != NULL) {
    state->collation_needed_callback(state->collation_needed_context,
                                     state->database,
                                     (unsigned long)text_representation, name);
  }
}

static void cpkt_sqlite_collation_needed16_trampoline(void *context,
                                                      sqlite3 *native_database,
                                                      int text_representation,
                                                      const void *name) {
  cpkt_sqlite_state *state;
  (void)native_database;
  state = (cpkt_sqlite_state *)context;
  if (state != NULL && state->collation_needed16_callback != NULL) {
    state->collation_needed16_callback(
        state->collation_needed_context, state->database,
        (unsigned long)text_representation, name);
  }
}

static int cpkt_sqlite_trace_trampoline(unsigned int event, void *context,
                                        void *first, void *second) {
  cpkt_sqlite_state *state;
  const char *sql;
  sqlite3_uint64 elapsed;
  state = (cpkt_sqlite_state *)context;
  if (state == NULL || state->trace_callback == NULL)
    return 0;
  sql = NULL;
  elapsed = 0;
  if (event == SQLITE_TRACE_STMT) {
    sql = (const char *)second;
  } else if (event == SQLITE_TRACE_PROFILE) {
    sql = sqlite3_sql((sqlite3_stmt *)first);
    if (second != NULL)
      elapsed = *(sqlite3_uint64 *)second;
  } else if (event == SQLITE_TRACE_ROW) {
    sql = sqlite3_sql((sqlite3_stmt *)first);
  }
  state->trace_callback(state->trace_context, state->database,
                        (unsigned long)event, sql,
                        cpkt_sqlite_public_u64(elapsed));
  return 0;
}

static void cpkt_sqlite_legacy_trace_trampoline(void *context,
                                                const char *sql) {
  cpkt_sqlite_state *state;
  state = (cpkt_sqlite_state *)context;
  if (state != NULL && state->legacy_trace_callback != NULL) {
    state->legacy_trace_callback(state->legacy_trace_context, sql);
  }
}

static void
cpkt_sqlite_legacy_profile_trampoline(void *context, const char *sql,
                                      sqlite3_uint64 elapsed_nanoseconds) {
  cpkt_sqlite_state *state;
  state = (cpkt_sqlite_state *)context;
  if (state != NULL && state->legacy_profile_callback != NULL) {
    state->legacy_profile_callback(state->legacy_profile_context, sql,
                                   cpkt_sqlite_public_u64(elapsed_nanoseconds));
  }
}

static void cpkt_sqlite_unlock_notify_trampoline(void **arguments,
                                                 int argument_count) {
  cpkt_sqlite_unlock_notify_binding **bindings;
  void **contexts;
  cpkt_sqlite_unlock_notify_binding *binding;
  pthread_mutex_t *mutex;
  void *single_context;
  int context_count;
  int index;
  int group_index;
  if (arguments == NULL || argument_count <= 0)
    return;
  bindings = (cpkt_sqlite_unlock_notify_binding **)arguments;
  mutex = cpkt_sqlite_global_mutex();
  cpkt_sqlite_global_lock(mutex);
  /* A replacement owns the old binding until its native call returns.
   * Otherwise delivery clears and releases the current registration. */
  for (index = 0; index < argument_count; ++index) {
    binding = bindings[index];
    if (binding != NULL) {
      binding->delivered = 1;
      binding->release_on_delivery =
          binding->state != NULL &&
          binding->state->unlock_notify_binding == binding;
      if (binding->release_on_delivery)
        binding->state->unlock_notify_binding = NULL;
    }
  }
  cpkt_sqlite_global_unlock(mutex);
  contexts = (void **)malloc((size_t)argument_count * sizeof(*contexts));
  for (index = 0; index < argument_count; ++index) {
    binding = bindings[index];
    if (binding != NULL && binding->callback != NULL) {
      if (contexts == NULL) {
        single_context = binding->context;
        binding->callback(binding->context, 1, &single_context);
      } else {
        context_count = 0;
        for (group_index = 0; group_index < argument_count; ++group_index) {
          if (bindings[group_index] != NULL &&
              bindings[group_index]->callback == binding->callback) {
            contexts[context_count++] = bindings[group_index]->context;
          }
        }
        binding->callback(binding->context, context_count, contexts);
      }
    }
  }
  free(contexts);
  for (index = 0; index < argument_count; ++index) {
    binding = bindings[index];
    if (binding != NULL && binding->release_on_delivery)
      free(binding);
  }
}

static int cpkt_sqlite_rtree_geometry_trampoline(sqlite3_rtree_geometry *native,
                                                 int coordinate_count,
                                                 sqlite3_rtree_dbl *coordinates,
                                                 int *within_out) {
  cpkt_sqlite_rtree_geometry_binding *binding;
  cpkt_sqlite_rtree_geometry public_geometry;
  int status;
  if (native == NULL)
    return SQLITE_MISUSE;
  binding = (cpkt_sqlite_rtree_geometry_binding *)native->pContext;
  if (binding == NULL || binding->callback == NULL)
    return SQLITE_MISUSE;
  public_geometry.parameter_count = native->nParam;
  public_geometry.parameters = native->aParam;
  public_geometry.user = native->pUser;
  public_geometry.user_destroy = native->xDelUser;
  status = binding->callback(binding->context, &public_geometry,
                             coordinate_count, coordinates, within_out);
  native->pUser = public_geometry.user;
  native->xDelUser = public_geometry.user_destroy;
  return status;
}

static void cpkt_sqlite_rtree_query_destroy(void *context) {
  cpkt_sqlite_rtree_query_binding *binding;
  binding = (cpkt_sqlite_rtree_query_binding *)context;
  if (binding == NULL)
    return;
  if (binding->destroy != NULL)
    binding->destroy(binding->context);
  free(binding);
}

static int
cpkt_sqlite_rtree_query_trampoline(sqlite3_rtree_query_info *native) {
  cpkt_sqlite_rtree_query_binding *binding;
  cpkt_sqlite_rtree_query public_query;
  cpkt_sqlite_value *values;
  cpkt_sqlite_value **value_pointers;
  int index;
  int status;
  if (native == NULL)
    return SQLITE_MISUSE;
  binding = (cpkt_sqlite_rtree_query_binding *)native->pContext;
  if (binding == NULL || binding->callback == NULL)
    return SQLITE_MISUSE;
  values = NULL;
  value_pointers = NULL;
  if (native->nParam > 0 && native->apSqlParam != NULL) {
    values =
        (cpkt_sqlite_value *)calloc((size_t)native->nParam, sizeof(*values));
    value_pointers = (cpkt_sqlite_value **)calloc((size_t)native->nParam,
                                                  sizeof(*value_pointers));
    if (values == NULL || value_pointers == NULL) {
      free(values);
      free(value_pointers);
      return SQLITE_NOMEM;
    }
    for (index = 0; index < native->nParam; ++index) {
      values[index].value = native->apSqlParam[index];
      values[index].owned = 0;
      values[index].shell_owned = 0;
      value_pointers[index] = &values[index];
    }
  }
  public_query.parameter_count = native->nParam;
  public_query.parameters = native->aParam;
  public_query.user = native->pUser;
  public_query.user_destroy = native->xDelUser;
  public_query.coordinates = native->aCoord;
  public_query.pending_entries = native->anQueue;
  public_query.coordinate_count = native->nCoord;
  public_query.level = native->iLevel;
  public_query.maximum_level = native->mxLevel;
  public_query.rowid = cpkt_sqlite_public_i64(native->iRowid);
  public_query.parent_score = native->rParentScore;
  public_query.parent_within = native->eParentWithin;
  public_query.within = native->eWithin;
  public_query.score = native->rScore;
  public_query.sql_parameters = value_pointers;
  status = binding->callback(binding->context, &public_query);
  native->pUser = public_query.user;
  native->xDelUser = public_query.user_destroy;
  native->eWithin = public_query.within;
  native->rScore = public_query.score;
  free(values);
  free(value_pointers);
  return status;
}

static int cpkt_sqlite_progress_trampoline(void *context) {
  cpkt_sqlite_state *state;
  state = (cpkt_sqlite_state *)context;
  if (state == NULL || state->progress_callback == NULL)
    return 0;
  return state->progress_callback(state->progress_context);
}

static int cpkt_sqlite_commit_trampoline(void *context) {
  cpkt_sqlite_state *state;
  state = (cpkt_sqlite_state *)context;
  if (state == NULL || state->commit_callback == NULL)
    return 0;
  return state->commit_callback(state->commit_context);
}

static void cpkt_sqlite_rollback_trampoline(void *context) {
  cpkt_sqlite_state *state;
  state = (cpkt_sqlite_state *)context;
  if (state != NULL && state->rollback_callback != NULL) {
    state->rollback_callback(state->rollback_context);
  }
}

static void cpkt_sqlite_update_trampoline(void *context, int operation,
                                          const char *database_name,
                                          const char *table_name,
                                          sqlite3_int64 row_id) {
  cpkt_sqlite_state *state;
  state = (cpkt_sqlite_state *)context;
  if (state != NULL && state->update_callback != NULL) {
    state->update_callback(state->update_context, operation, database_name,
                           table_name, cpkt_sqlite_public_i64(row_id));
  }
}

static int cpkt_sqlite_wal_trampoline(void *context, sqlite3 *native_database,
                                      const char *database_name,
                                      int page_count) {
  cpkt_sqlite_state *state;
  (void)native_database;
  state = (cpkt_sqlite_state *)context;
  if (state == NULL || state->wal_callback == NULL)
    return SQLITE_OK;
  return state->wal_callback(state->wal_context, state->database, database_name,
                             page_count);
}

static void cpkt_sqlite_preupdate_trampoline(
    void *context, sqlite3 *native_database, int operation,
    const char *database_name, const char *table_name, sqlite3_int64 old_row_id,
    sqlite3_int64 new_row_id) {
  cpkt_sqlite_state *state;
  (void)native_database;
  state = (cpkt_sqlite_state *)context;
  if (state != NULL && state->preupdate_callback != NULL) {
    state->preupdate_callback(state->preupdate_context, state->database,
                              operation, database_name, table_name,
                              cpkt_sqlite_public_i64(old_row_id),
                              cpkt_sqlite_public_i64(new_row_id));
  }
}

static void cpkt_sqlite_function_destroy(void *context) {
  cpkt_sqlite_function_binding *binding;
  binding = (cpkt_sqlite_function_binding *)context;
  if (binding == NULL)
    return;
  if (binding->destroy != NULL)
    binding->destroy(binding->user_data);
  free(binding);
}

static void cpkt_sqlite_auxdata_destroy(void *context) {
  cpkt_sqlite_auxdata_binding *binding;
  binding = (cpkt_sqlite_auxdata_binding *)context;
  if (binding == NULL)
    return;
  if (binding->destroy != NULL)
    binding->destroy(binding->data);
  free(binding);
}

static void cpkt_sqlite_collation_destroy(void *context) {
  cpkt_sqlite_collation_binding *binding;
  binding = (cpkt_sqlite_collation_binding *)context;
  if (binding == NULL)
    return;
  if (binding->destroy != NULL)
    binding->destroy(binding->context);
  free(binding);
}

static int cpkt_sqlite_collation_trampoline(void *context, int left_byte_count,
                                            const void *left,
                                            int right_byte_count,
                                            const void *right) {
  cpkt_sqlite_collation_binding *binding;
  binding = (cpkt_sqlite_collation_binding *)context;
  if (binding == NULL || binding->compare == NULL)
    return 0;
  return binding->compare(binding->context, left_byte_count, left,
                          right_byte_count, right);
}

static void cpkt_sqlite_autovacuum_destroy(void *context) {
  cpkt_sqlite_autovacuum_binding *binding;
  binding = (cpkt_sqlite_autovacuum_binding *)context;
  if (binding == NULL)
    return;
  if (binding->destroy != NULL)
    binding->destroy(binding->context);
  free(binding);
}

static unsigned int cpkt_sqlite_autovacuum_trampoline(
    void *context, const char *schema_name, unsigned int database_page_count,
    unsigned int free_page_count, unsigned int page_byte_count) {
  cpkt_sqlite_autovacuum_binding *binding;
  unsigned long result;
  binding = (cpkt_sqlite_autovacuum_binding *)context;
  if (binding == NULL || binding->callback == NULL)
    return 0;
  result = binding->callback(
      binding->context, schema_name, (unsigned long)database_page_count,
      (unsigned long)free_page_count, (unsigned long)page_byte_count);
  return result > 0xffffffffUL ? 0xffffffffU : (unsigned int)result;
}

static void cpkt_sqlite_function_call(sqlite3_context *native_context,
                                      int argument_count,
                                      sqlite3_value **native_arguments,
                                      cpkt_sqlite_scalar_callback callback) {
  cpkt_sqlite_function_binding *binding;
  cpkt_sqlite_context public_context;
  cpkt_sqlite_value *values;
  cpkt_sqlite_value **arguments;
  int index;
  binding = (cpkt_sqlite_function_binding *)sqlite3_user_data(native_context);
  if (binding == NULL || callback == NULL)
    return;
  public_context.context = native_context;
  public_context.database = binding->database;
  values = NULL;
  arguments = NULL;
  if (argument_count > 0) {
    values =
        (cpkt_sqlite_value *)calloc((size_t)argument_count, sizeof(*values));
    arguments = (cpkt_sqlite_value **)calloc((size_t)argument_count,
                                             sizeof(*arguments));
    if (values == NULL || arguments == NULL) {
      free(values);
      free(arguments);
      sqlite3_result_error_nomem(native_context);
      return;
    }
    for (index = 0; index < argument_count; ++index) {
      values[index].value = native_arguments[index];
      values[index].owned = 0;
      arguments[index] = &values[index];
    }
  }
  callback(&public_context, argument_count, arguments, binding->user_data);
  free(arguments);
  free(values);
}

static void cpkt_sqlite_function_scalar_trampoline(sqlite3_context *context,
                                                   int argument_count,
                                                   sqlite3_value **arguments) {
  cpkt_sqlite_function_binding *binding;
  binding = (cpkt_sqlite_function_binding *)sqlite3_user_data(context);
  cpkt_sqlite_function_call(context, argument_count, arguments,
                            binding == NULL ? NULL : binding->scalar);
}

static void cpkt_sqlite_function_step_trampoline(sqlite3_context *context,
                                                 int argument_count,
                                                 sqlite3_value **arguments) {
  cpkt_sqlite_function_binding *binding;
  binding = (cpkt_sqlite_function_binding *)sqlite3_user_data(context);
  cpkt_sqlite_function_call(context, argument_count, arguments,
                            binding == NULL ? NULL : binding->step);
}

static void cpkt_sqlite_function_final_trampoline(sqlite3_context *context) {
  cpkt_sqlite_function_binding *binding;
  binding = (cpkt_sqlite_function_binding *)sqlite3_user_data(context);
  cpkt_sqlite_function_call(context, 0, NULL,
                            binding == NULL ? NULL : binding->final);
}

static void cpkt_sqlite_function_value_trampoline(sqlite3_context *context) {
  cpkt_sqlite_function_binding *binding;
  binding = (cpkt_sqlite_function_binding *)sqlite3_user_data(context);
  cpkt_sqlite_function_call(context, 0, NULL,
                            binding == NULL ? NULL : binding->value);
}

static void cpkt_sqlite_function_inverse_trampoline(sqlite3_context *context,
                                                    int argument_count,
                                                    sqlite3_value **arguments) {
  cpkt_sqlite_function_binding *binding;
  binding = (cpkt_sqlite_function_binding *)sqlite3_user_data(context);
  cpkt_sqlite_function_call(context, argument_count, arguments,
                            binding == NULL ? NULL : binding->inverse);
}

static void cpkt_sqlite_iterator_init(cpkt_sqlite_changeset_iterator *iterator,
                                      sqlite3_changeset_iter *native_iterator,
                                      int borrowed);

static int cpkt_sqlite_changeset_filter_trampoline(void *context,
                                                   const char *table_name) {
  cpkt_sqlite_changeset_apply_context *apply_context;
  apply_context = (cpkt_sqlite_changeset_apply_context *)context;
  if (apply_context == NULL || apply_context->filter == NULL)
    return 1;
  return apply_context->filter(apply_context->user_context, table_name);
}

static int cpkt_sqlite_changeset_iterator_filter_trampoline(
    void *context, sqlite3_changeset_iter *native_iterator) {
  cpkt_sqlite_changeset_apply_context *apply_context;
  cpkt_sqlite_changeset_iterator iterator;
  apply_context = (cpkt_sqlite_changeset_apply_context *)context;
  if (apply_context == NULL || apply_context->iterator_filter == NULL)
    return 1;
  cpkt_sqlite_iterator_init(&iterator, native_iterator, 1);
  return apply_context->iterator_filter(apply_context->user_context, &iterator);
}

static int
cpkt_sqlite_iterator_borrowed_close(cpkt_sqlite_changeset_iterator *self) {
  (void)self;
  return CPKT_SQLITE_MISUSE;
}

static int cpkt_sqlite_changeset_conflict_trampoline(
    void *context, int conflict_kind, sqlite3_changeset_iter *native_iterator) {
  cpkt_sqlite_changeset_apply_context *apply_context;
  cpkt_sqlite_changeset_iterator iterator;
  apply_context = (cpkt_sqlite_changeset_apply_context *)context;
  if (apply_context == NULL || apply_context->conflict == NULL)
    return SQLITE_CHANGESET_ABORT;
  cpkt_sqlite_iterator_init(&iterator, native_iterator, 1);
  return apply_context->conflict(apply_context->user_context, conflict_kind,
                                 &iterator);
}

static int cpkt_sqlite_stream_input_trampoline(void *context, void *buffer,
                                               int *byte_count) {
  cpkt_sqlite_stream_input_context *input_context;
  input_context = (cpkt_sqlite_stream_input_context *)context;
  if (input_context == NULL || input_context->callback == NULL)
    return CPKT_SQLITE_MISUSE;
  return input_context->callback(input_context->user_context, buffer,
                                 byte_count);
}

static int cpkt_sqlite_stream_output_trampoline(void *context,
                                                const void *buffer,
                                                int byte_count) {
  cpkt_sqlite_stream_output_context *output_context;
  output_context = (cpkt_sqlite_stream_output_context *)context;
  if (output_context == NULL || output_context->callback == NULL) {
    return CPKT_SQLITE_MISUSE;
  }
  return output_context->callback(output_context->user_context, buffer,
                                  byte_count);
}

static int cpkt_sqlite_iterator_stream_input_trampoline(void *context,
                                                        void *buffer,
                                                        int *byte_count) {
  cpkt_sqlite_changeset_iterator *iterator;
  iterator = (cpkt_sqlite_changeset_iterator *)context;
  if (iterator == NULL || iterator->input == NULL)
    return CPKT_SQLITE_MISUSE;
  return iterator->input(iterator->input_context, buffer, byte_count);
}

static int cpkt_sqlite_exec_callback(void *context, int column_count,
                                     char **values, char **names) {
  cpkt_sqlite_exec_context *callback_context;
  cpkt_sqlite_row_callback callback;
  callback_context = (cpkt_sqlite_exec_context *)context;
  callback = callback_context->callback;
  if (callback == NULL)
    return 0;
  return callback(callback_context->context, column_count,
                  (const char *const *)values, (const char *const *)names);
}

static int cpkt_sqlite_statement_bind_null(cpkt_sqlite_statement *self,
                                           int parameter_index) {
  return sqlite3_bind_null(cpkt_sqlite_native_statement(self), parameter_index);
}

static int cpkt_sqlite_statement_bind_int(cpkt_sqlite_statement *self,
                                          int parameter_index, int value) {
  return sqlite3_bind_int(cpkt_sqlite_native_statement(self), parameter_index,
                          value);
}

static int cpkt_sqlite_statement_bind_i64(cpkt_sqlite_statement *self,
                                          int parameter_index,
                                          cpkt_sqlite_i64 value) {
  return sqlite3_bind_int64(cpkt_sqlite_native_statement(self), parameter_index,
                            cpkt_sqlite_native_i64(value));
}

static int cpkt_sqlite_statement_bind_double(cpkt_sqlite_statement *self,
                                             int parameter_index,
                                             double value) {
  return sqlite3_bind_double(cpkt_sqlite_native_statement(self),
                             parameter_index, value);
}

static int cpkt_sqlite_statement_bind_text(cpkt_sqlite_statement *self,
                                           int parameter_index,
                                           const char *value, int byte_count) {
  return sqlite3_bind_text(cpkt_sqlite_native_statement(self), parameter_index,
                           value, byte_count, SQLITE_TRANSIENT);
}

static int cpkt_sqlite_statement_bind_text16(cpkt_sqlite_statement *self,
                                             int parameter_index,
                                             const void *value,
                                             int byte_count) {
  return sqlite3_bind_text16(cpkt_sqlite_native_statement(self),
                             parameter_index, value, byte_count,
                             SQLITE_TRANSIENT);
}

static int cpkt_sqlite_statement_bind_text_u64(cpkt_sqlite_statement *self,
                                               int parameter_index,
                                               const void *value,
                                               cpkt_sqlite_u64 byte_count,
                                               unsigned long encoding) {
  return sqlite3_bind_text64(cpkt_sqlite_native_statement(self),
                             parameter_index, (const char *)value,
                             cpkt_sqlite_native_u64(byte_count),
                             SQLITE_TRANSIENT, (unsigned char)encoding);
}

static int cpkt_sqlite_statement_bind_text_owned(
    cpkt_sqlite_statement *self, int parameter_index, const char *value,
    int byte_count, cpkt_sqlite_destroy_callback destroy) {
  return sqlite3_bind_text(cpkt_sqlite_native_statement(self), parameter_index,
                           value, byte_count, destroy);
}

static int cpkt_sqlite_statement_bind_blob(cpkt_sqlite_statement *self,
                                           int parameter_index,
                                           const void *value, int byte_count) {
  return sqlite3_bind_blob(cpkt_sqlite_native_statement(self), parameter_index,
                           value, byte_count, SQLITE_TRANSIENT);
}

static int cpkt_sqlite_statement_bind_blob_u64(cpkt_sqlite_statement *self,
                                               int parameter_index,
                                               const void *value,
                                               cpkt_sqlite_u64 byte_count) {
  return sqlite3_bind_blob64(
      cpkt_sqlite_native_statement(self), parameter_index, value,
      cpkt_sqlite_native_u64(byte_count), SQLITE_TRANSIENT);
}

static int cpkt_sqlite_statement_bind_blob_owned(
    cpkt_sqlite_statement *self, int parameter_index, const void *value,
    int byte_count, cpkt_sqlite_destroy_callback destroy) {
  return sqlite3_bind_blob(cpkt_sqlite_native_statement(self), parameter_index,
                           value, byte_count, destroy);
}

static int cpkt_sqlite_statement_bind_zero_blob(cpkt_sqlite_statement *self,
                                                int parameter_index,
                                                int byte_count) {
  return sqlite3_bind_zeroblob(cpkt_sqlite_native_statement(self),
                               parameter_index, byte_count);
}

static int
cpkt_sqlite_statement_bind_zero_blob_u64(cpkt_sqlite_statement *self,
                                         int parameter_index,
                                         cpkt_sqlite_u64 byte_count) {
  return sqlite3_bind_zeroblob64(cpkt_sqlite_native_statement(self),
                                 parameter_index,
                                 cpkt_sqlite_native_u64(byte_count));
}

static int cpkt_sqlite_statement_bind_value(cpkt_sqlite_statement *self,
                                            int parameter_index,
                                            const cpkt_sqlite_value *value) {
  if (cpkt_sqlite_native_value(value) == NULL)
    return CPKT_SQLITE_MISUSE;
  return sqlite3_bind_value(cpkt_sqlite_native_statement(self), parameter_index,
                            cpkt_sqlite_native_value(value));
}

static int cpkt_sqlite_statement_bind_pointer(
    cpkt_sqlite_statement *self, int parameter_index, void *value,
    const char *type_name, cpkt_sqlite_destroy_callback destroy) {
  if (type_name == NULL)
    return CPKT_SQLITE_MISUSE;
  return sqlite3_bind_pointer(cpkt_sqlite_native_statement(self),
                              parameter_index, value, type_name, destroy);
}

static int cpkt_sqlite_statement_bind_carray(
    cpkt_sqlite_statement *self, int parameter_index, void *data,
    int element_count, int element_type, cpkt_sqlite_destroy_callback destroy) {
  cpkt_sqlite_carray_i64_binding *binding;
  int status;
  if (element_type != SQLITE_CARRAY_INT64 || data == NULL ||
      element_count < 1) {
    return sqlite3_carray_bind(cpkt_sqlite_native_statement(self),
                               parameter_index, data, element_count,
                               element_type, destroy);
  }
  binding =
      cpkt_sqlite_carray_i64_binding_new(data, element_count, destroy, data);
  if (binding == NULL) {
    if (destroy != NULL)
      destroy(data);
    return SQLITE_NOMEM;
  }
  status = sqlite3_carray_bind_v2(
      cpkt_sqlite_native_statement(self), parameter_index, binding->values,
      element_count, element_type, cpkt_sqlite_carray_i64_destroy, binding);
  return status;
}

static int cpkt_sqlite_statement_bind_carray_with_context(
    cpkt_sqlite_statement *self, int parameter_index, void *data,
    int element_count, int element_type, cpkt_sqlite_destroy_callback destroy,
    void *destroy_context) {
  cpkt_sqlite_carray_i64_binding *binding;
  int status;
  if (element_type != SQLITE_CARRAY_INT64 || data == NULL ||
      element_count < 1) {
    return sqlite3_carray_bind_v2(cpkt_sqlite_native_statement(self),
                                  parameter_index, data, element_count,
                                  element_type, destroy, destroy_context);
  }
  binding = cpkt_sqlite_carray_i64_binding_new(data, element_count, destroy,
                                               destroy_context);
  if (binding == NULL) {
    if (destroy != NULL)
      destroy(destroy_context);
    return SQLITE_NOMEM;
  }
  status = sqlite3_carray_bind_v2(
      cpkt_sqlite_native_statement(self), parameter_index, binding->values,
      element_count, element_type, cpkt_sqlite_carray_i64_destroy, binding);
  return status;
}

static int cpkt_sqlite_statement_step(cpkt_sqlite_statement *self) {
  return sqlite3_step(cpkt_sqlite_native_statement(self));
}

static int cpkt_sqlite_statement_reset(cpkt_sqlite_statement *self) {
  return sqlite3_reset(cpkt_sqlite_native_statement(self));
}

static int cpkt_sqlite_statement_clear_bindings(cpkt_sqlite_statement *self) {
  return sqlite3_clear_bindings(cpkt_sqlite_native_statement(self));
}

static int
cpkt_sqlite_statement_parameter_count(const cpkt_sqlite_statement *self) {
  return sqlite3_bind_parameter_count(cpkt_sqlite_native_statement(self));
}

static const char *
cpkt_sqlite_statement_parameter_name(const cpkt_sqlite_statement *self,
                                     int parameter_index) {
  return sqlite3_bind_parameter_name(cpkt_sqlite_native_statement(self),
                                     parameter_index);
}

static int
cpkt_sqlite_statement_parameter_index(const cpkt_sqlite_statement *self,
                                      const char *name) {
  return sqlite3_bind_parameter_index(cpkt_sqlite_native_statement(self), name);
}

static int
cpkt_sqlite_statement_column_count(const cpkt_sqlite_statement *self) {
  return sqlite3_column_count(cpkt_sqlite_native_statement(self));
}

static const char *
cpkt_sqlite_statement_column_name(const cpkt_sqlite_statement *self,
                                  int column) {
  return sqlite3_column_name(cpkt_sqlite_native_statement(self), column);
}

static const void *
cpkt_sqlite_statement_column_name16(const cpkt_sqlite_statement *self,
                                    int column) {
  return sqlite3_column_name16(cpkt_sqlite_native_statement(self), column);
}

static int cpkt_sqlite_statement_column_type(const cpkt_sqlite_statement *self,
                                             int column) {
  return sqlite3_column_type(cpkt_sqlite_native_statement(self), column);
}

static int cpkt_sqlite_statement_column_int(const cpkt_sqlite_statement *self,
                                            int column) {
  return sqlite3_column_int(cpkt_sqlite_native_statement(self), column);
}

static cpkt_sqlite_i64
cpkt_sqlite_statement_column_i64(const cpkt_sqlite_statement *self,
                                 int column) {
  return cpkt_sqlite_public_i64(
      sqlite3_column_int64(cpkt_sqlite_native_statement(self), column));
}

static double
cpkt_sqlite_statement_column_double(const cpkt_sqlite_statement *self,
                                    int column) {
  return sqlite3_column_double(cpkt_sqlite_native_statement(self), column);
}

static const unsigned char *
cpkt_sqlite_statement_column_text(const cpkt_sqlite_statement *self,
                                  int column) {
  return sqlite3_column_text(cpkt_sqlite_native_statement(self), column);
}

static const void *
cpkt_sqlite_statement_column_text16(const cpkt_sqlite_statement *self,
                                    int column) {
  return sqlite3_column_text16(cpkt_sqlite_native_statement(self), column);
}

static const void *
cpkt_sqlite_statement_column_blob(const cpkt_sqlite_statement *self,
                                  int column) {
  return sqlite3_column_blob(cpkt_sqlite_native_statement(self), column);
}

static int cpkt_sqlite_statement_column_bytes(const cpkt_sqlite_statement *self,
                                              int column) {
  return sqlite3_column_bytes(cpkt_sqlite_native_statement(self), column);
}

static int
cpkt_sqlite_statement_column_bytes16(const cpkt_sqlite_statement *self,
                                     int column) {
  return sqlite3_column_bytes16(cpkt_sqlite_native_statement(self), column);
}

static cpkt_sqlite_value *
cpkt_sqlite_statement_column_value(const cpkt_sqlite_statement *self,
                                   int column) {
  cpkt_sqlite_value *value;
  sqlite3_value *native_value;
  native_value =
      sqlite3_column_value(cpkt_sqlite_native_statement(self), column);
  if (native_value == NULL)
    return NULL;
  value = (cpkt_sqlite_value *)calloc(1, sizeof(*value));
  if (value == NULL)
    return NULL;
  value->value = native_value;
  value->owned = 0;
  value->shell_owned = 1;
  return value;
}

static const char *
cpkt_sqlite_statement_column_database_name(const cpkt_sqlite_statement *self,
                                           int column) {
  return sqlite3_column_database_name(cpkt_sqlite_native_statement(self),
                                      column);
}

static const void *
cpkt_sqlite_statement_column_database_name16(const cpkt_sqlite_statement *self,
                                             int column) {
  return sqlite3_column_database_name16(cpkt_sqlite_native_statement(self),
                                        column);
}

static const char *
cpkt_sqlite_statement_column_table_name(const cpkt_sqlite_statement *self,
                                        int column) {
  return sqlite3_column_table_name(cpkt_sqlite_native_statement(self), column);
}

static const void *
cpkt_sqlite_statement_column_table_name16(const cpkt_sqlite_statement *self,
                                          int column) {
  return sqlite3_column_table_name16(cpkt_sqlite_native_statement(self),
                                     column);
}

static const char *
cpkt_sqlite_statement_column_origin_name(const cpkt_sqlite_statement *self,
                                         int column) {
  return sqlite3_column_origin_name(cpkt_sqlite_native_statement(self), column);
}

static const void *
cpkt_sqlite_statement_column_origin_name16(const cpkt_sqlite_statement *self,
                                           int column) {
  return sqlite3_column_origin_name16(cpkt_sqlite_native_statement(self),
                                      column);
}

static const char *
cpkt_sqlite_statement_column_declared_type(const cpkt_sqlite_statement *self,
                                           int column) {
  return sqlite3_column_decltype(cpkt_sqlite_native_statement(self), column);
}

static const void *
cpkt_sqlite_statement_column_declared_type16(const cpkt_sqlite_statement *self,
                                             int column) {
  return sqlite3_column_decltype16(cpkt_sqlite_native_statement(self), column);
}

static const char *
cpkt_sqlite_statement_sql(const cpkt_sqlite_statement *self) {
  return sqlite3_sql(cpkt_sqlite_native_statement(self));
}

static char *
cpkt_sqlite_statement_expanded_sql(const cpkt_sqlite_statement *self) {
  return sqlite3_expanded_sql(cpkt_sqlite_native_statement(self));
}

static const char *
cpkt_sqlite_statement_normalized_sql(const cpkt_sqlite_statement *self) {
  return sqlite3_normalized_sql(cpkt_sqlite_native_statement(self));
}

static int cpkt_sqlite_statement_readonly(const cpkt_sqlite_statement *self) {
  return sqlite3_stmt_readonly(cpkt_sqlite_native_statement(self));
}

static int cpkt_sqlite_statement_busy(const cpkt_sqlite_statement *self) {
  return sqlite3_stmt_busy(cpkt_sqlite_native_statement(self));
}

static int cpkt_sqlite_statement_expired(const cpkt_sqlite_statement *self) {
  return sqlite3_expired(cpkt_sqlite_native_statement(self));
}

static int cpkt_sqlite_statement_is_explain(const cpkt_sqlite_statement *self) {
  return sqlite3_stmt_isexplain(cpkt_sqlite_native_statement(self));
}

static int cpkt_sqlite_statement_explain(cpkt_sqlite_statement *self,
                                         int mode) {
  return sqlite3_stmt_explain(cpkt_sqlite_native_statement(self), mode);
}

static int cpkt_sqlite_statement_data_count(const cpkt_sqlite_statement *self) {
  return sqlite3_data_count(cpkt_sqlite_native_statement(self));
}

static int cpkt_sqlite_statement_status(const cpkt_sqlite_statement *self,
                                        int operation, int reset) {
  if (self == NULL || cpkt_sqlite_native_statement(self) == NULL)
    return 0;
  return sqlite3_stmt_status(cpkt_sqlite_native_statement(self), operation,
                             reset);
}

static int cpkt_sqlite_statement_scan_status_i64(
    const cpkt_sqlite_statement *self, int index, int operation,
    unsigned long flags, cpkt_sqlite_i64 *value_out) {
  sqlite3_int64 value;
  int status;
  if (self == NULL || value_out == NULL ||
      (flags & ~(unsigned long)SQLITE_SCANSTAT_COMPLEX) != 0 ||
      (operation != SQLITE_SCANSTAT_NLOOP &&
       operation != SQLITE_SCANSTAT_NVISIT &&
       operation != SQLITE_SCANSTAT_NCYCLE))
    return SQLITE_MISUSE;
  value = 0;
  status = sqlite3_stmt_scanstatus_v2(cpkt_sqlite_native_statement(self), index,
                                      operation, (int)flags, &value);
  if (status == SQLITE_OK)
    *value_out = cpkt_sqlite_public_i64(value);
  return status;
}

static int cpkt_sqlite_statement_scan_status_double(
    const cpkt_sqlite_statement *self, int index, int operation,
    unsigned long flags, double *value_out) {
  if (self == NULL || value_out == NULL ||
      (flags & ~(unsigned long)SQLITE_SCANSTAT_COMPLEX) != 0 ||
      operation != SQLITE_SCANSTAT_EST)
    return SQLITE_MISUSE;
  return sqlite3_stmt_scanstatus_v2(cpkt_sqlite_native_statement(self), index,
                                    operation, (int)flags, value_out);
}

static int
cpkt_sqlite_statement_scan_status_int(const cpkt_sqlite_statement *self,
                                      int index, int operation,
                                      unsigned long flags, int *value_out) {
  if (self == NULL || value_out == NULL ||
      (flags & ~(unsigned long)SQLITE_SCANSTAT_COMPLEX) != 0 ||
      (operation != SQLITE_SCANSTAT_SELECTID &&
       operation != SQLITE_SCANSTAT_PARENTID))
    return SQLITE_MISUSE;
  return sqlite3_stmt_scanstatus_v2(cpkt_sqlite_native_statement(self), index,
                                    operation, (int)flags, value_out);
}

static int cpkt_sqlite_statement_scan_status_text(
    const cpkt_sqlite_statement *self, int index, int operation,
    unsigned long flags, const char **value_out) {
  if (self == NULL || value_out == NULL ||
      (flags & ~(unsigned long)SQLITE_SCANSTAT_COMPLEX) != 0 ||
      (operation != SQLITE_SCANSTAT_NAME &&
       operation != SQLITE_SCANSTAT_EXPLAIN))
    return SQLITE_MISUSE;
  return sqlite3_stmt_scanstatus_v2(cpkt_sqlite_native_statement(self), index,
                                    operation, (int)flags, value_out);
}

static void
cpkt_sqlite_statement_scan_status_reset(cpkt_sqlite_statement *self) {
  if (self != NULL)
    sqlite3_stmt_scanstatus_reset(cpkt_sqlite_native_statement(self));
}

static cpkt_sqlite *
cpkt_sqlite_statement_database(const cpkt_sqlite_statement *self) {
  return self == NULL ? NULL : self->owner;
}

int cpkt_sqlite_statement_finalize(cpkt_sqlite_statement *self) {
  cpkt_sqlite *database;
  int status;
  if (self == NULL)
    return CPKT_SQLITE_MISUSE;
  database = self->owner;
  status = self->borrowed
               ? CPKT_SQLITE_OK
               : sqlite3_finalize(cpkt_sqlite_native_statement(self));
  self->statement = NULL;
  self->owner = NULL;
  free(self);
  cpkt_sqlite_release_child(database);
  return status;
}

static int cpkt_sqlite_table_row_count(const cpkt_sqlite_table *self) {
  return self == NULL ? 0 : self->rows;
}

static int cpkt_sqlite_table_column_count(const cpkt_sqlite_table *self) {
  return self == NULL ? 0 : self->columns;
}

static const char *cpkt_sqlite_table_column_name(const cpkt_sqlite_table *self,
                                                 int column) {
  if (self == NULL || self->values == NULL || column < 0 ||
      column >= self->columns) {
    return NULL;
  }
  return self->values[column];
}

static const char *cpkt_sqlite_table_value(const cpkt_sqlite_table *self,
                                           int row, int column) {
  int index;
  if (self == NULL || self->values == NULL || row < 0 || row >= self->rows ||
      column < 0 || column >= self->columns)
    return NULL;
  index = (row + 1) * self->columns + column;
  return self->values[index];
}

static void cpkt_sqlite_table_close(cpkt_sqlite_table *self) {
  if (self == NULL)
    return;
  if (self->values != NULL)
    sqlite3_free_table(self->values);
  self->values = NULL;
  free(self);
}

int cpkt_sqlite_exec(cpkt_sqlite *self, const char *sql,
                     cpkt_sqlite_row_callback callback, void *context) {
  cpkt_sqlite_exec_context callback_context;
  if (self == NULL || cpkt_sqlite_native(self) == NULL || sql == NULL) {
    return CPKT_SQLITE_MISUSE;
  }
  callback_context.callback = callback;
  callback_context.context = context;
  return sqlite3_exec(cpkt_sqlite_native(self), sql,
                      callback == NULL ? NULL : cpkt_sqlite_exec_callback,
                      callback == NULL ? NULL : &callback_context, NULL);
}

int cpkt_sqlite_get_table(cpkt_sqlite *self, const char *sql,
                          cpkt_sqlite_table **table_out, char **error_out) {
  cpkt_sqlite_table *table;
  char **values;
  int rows;
  int columns;
  int status;
  if (table_out != NULL)
    *table_out = NULL;
  if (error_out != NULL)
    *error_out = NULL;
  if (self == NULL || cpkt_sqlite_native(self) == NULL || sql == NULL ||
      table_out == NULL)
    return SQLITE_MISUSE;
  values = NULL;
  rows = 0;
  columns = 0;
  status = sqlite3_get_table(cpkt_sqlite_native(self), sql, &values, &rows,
                             &columns, error_out);
  if (status != SQLITE_OK) {
    if (values != NULL)
      sqlite3_free_table(values);
    return status;
  }
  table = (cpkt_sqlite_table *)calloc(1, sizeof(*table));
  if (table == NULL) {
    sqlite3_free_table(values);
    return SQLITE_NOMEM;
  }
  table->row_count = cpkt_sqlite_table_row_count;
  table->column_count = cpkt_sqlite_table_column_count;
  table->column_name = cpkt_sqlite_table_column_name;
  table->value = cpkt_sqlite_table_value;
  table->close = cpkt_sqlite_table_close;
  table->values = values;
  table->rows = rows;
  table->columns = columns;
  *table_out = table;
  return SQLITE_OK;
}

int cpkt_sqlite_statement_transfer_bindings(cpkt_sqlite_statement *source,
                                            cpkt_sqlite_statement *target) {
  if (target == NULL || source == NULL ||
      cpkt_sqlite_native_statement(target) == NULL ||
      cpkt_sqlite_native_statement(source) == NULL)
    return SQLITE_MISUSE;
  return sqlite3_transfer_bindings(cpkt_sqlite_native_statement(source),
                                   cpkt_sqlite_native_statement(target));
}

static int cpkt_sqlite_wrap_statement(cpkt_sqlite *database,
                                      sqlite3_stmt *native_statement,
                                      int borrowed,
                                      cpkt_sqlite_statement **statement_out) {
  cpkt_sqlite_statement *public_statement;
  if (database == NULL || statement_out == NULL)
    return SQLITE_MISUSE;
  *statement_out = NULL;
  if (native_statement == NULL)
    return SQLITE_OK;
  public_statement =
      (cpkt_sqlite_statement *)calloc(1, sizeof(*public_statement));
  if (public_statement == NULL) {
    if (!borrowed)
      sqlite3_finalize(native_statement);
    return CPKT_SQLITE_NOMEM;
  }
  public_statement->database = cpkt_sqlite_statement_database;
  public_statement->bind_null = cpkt_sqlite_statement_bind_null;
  public_statement->bind_int = cpkt_sqlite_statement_bind_int;
  public_statement->bind_i64 = cpkt_sqlite_statement_bind_i64;
  public_statement->bind_double = cpkt_sqlite_statement_bind_double;
  public_statement->bind_text = cpkt_sqlite_statement_bind_text;
  public_statement->bind_text16 = cpkt_sqlite_statement_bind_text16;
  public_statement->bind_text_u64 = cpkt_sqlite_statement_bind_text_u64;
  public_statement->bind_text_owned = cpkt_sqlite_statement_bind_text_owned;
  public_statement->bind_blob = cpkt_sqlite_statement_bind_blob;
  public_statement->bind_blob_u64 = cpkt_sqlite_statement_bind_blob_u64;
  public_statement->bind_blob_owned = cpkt_sqlite_statement_bind_blob_owned;
  public_statement->bind_zero_blob = cpkt_sqlite_statement_bind_zero_blob;
  public_statement->bind_zero_blob_u64 =
      cpkt_sqlite_statement_bind_zero_blob_u64;
  public_statement->bind_value = cpkt_sqlite_statement_bind_value;
  public_statement->bind_pointer = cpkt_sqlite_statement_bind_pointer;
  public_statement->bind_carray = cpkt_sqlite_statement_bind_carray;
  public_statement->bind_carray_with_context =
      cpkt_sqlite_statement_bind_carray_with_context;
  public_statement->step = cpkt_sqlite_statement_step;
  public_statement->reset = cpkt_sqlite_statement_reset;
  public_statement->clear_bindings = cpkt_sqlite_statement_clear_bindings;
  public_statement->parameter_count = cpkt_sqlite_statement_parameter_count;
  public_statement->parameter_name = cpkt_sqlite_statement_parameter_name;
  public_statement->parameter_index = cpkt_sqlite_statement_parameter_index;
  public_statement->column_count = cpkt_sqlite_statement_column_count;
  public_statement->column_name = cpkt_sqlite_statement_column_name;
  public_statement->column_name16 = cpkt_sqlite_statement_column_name16;
  public_statement->column_type = cpkt_sqlite_statement_column_type;
  public_statement->column_int = cpkt_sqlite_statement_column_int;
  public_statement->column_i64 = cpkt_sqlite_statement_column_i64;
  public_statement->column_double = cpkt_sqlite_statement_column_double;
  public_statement->column_text = cpkt_sqlite_statement_column_text;
  public_statement->column_text16 = cpkt_sqlite_statement_column_text16;
  public_statement->column_blob = cpkt_sqlite_statement_column_blob;
  public_statement->column_bytes = cpkt_sqlite_statement_column_bytes;
  public_statement->column_bytes16 = cpkt_sqlite_statement_column_bytes16;
  public_statement->column_value = cpkt_sqlite_statement_column_value;
  public_statement->column_database_name =
      cpkt_sqlite_statement_column_database_name;
  public_statement->column_database_name16 =
      cpkt_sqlite_statement_column_database_name16;
  public_statement->column_table_name = cpkt_sqlite_statement_column_table_name;
  public_statement->column_table_name16 =
      cpkt_sqlite_statement_column_table_name16;
  public_statement->column_origin_name =
      cpkt_sqlite_statement_column_origin_name;
  public_statement->column_origin_name16 =
      cpkt_sqlite_statement_column_origin_name16;
  public_statement->column_declared_type =
      cpkt_sqlite_statement_column_declared_type;
  public_statement->column_declared_type16 =
      cpkt_sqlite_statement_column_declared_type16;
  public_statement->sql = cpkt_sqlite_statement_sql;
  public_statement->expanded_sql = cpkt_sqlite_statement_expanded_sql;
  public_statement->normalized_sql = cpkt_sqlite_statement_normalized_sql;
  public_statement->readonly = cpkt_sqlite_statement_readonly;
  public_statement->busy = cpkt_sqlite_statement_busy;
  public_statement->expired = cpkt_sqlite_statement_expired;
  public_statement->is_explain = cpkt_sqlite_statement_is_explain;
  public_statement->explain = cpkt_sqlite_statement_explain;
  public_statement->data_count = cpkt_sqlite_statement_data_count;
  public_statement->status = cpkt_sqlite_statement_status;
  public_statement->scan_status_i64 = cpkt_sqlite_statement_scan_status_i64;
  public_statement->scan_status_double =
      cpkt_sqlite_statement_scan_status_double;
  public_statement->scan_status_int = cpkt_sqlite_statement_scan_status_int;
  public_statement->scan_status_text = cpkt_sqlite_statement_scan_status_text;
  public_statement->scan_status_reset = cpkt_sqlite_statement_scan_status_reset;
  public_statement->finalize = cpkt_sqlite_statement_finalize;
  public_statement->statement = native_statement;
  public_statement->owner = database;
  public_statement->borrowed = borrowed;
  cpkt_sqlite_retain_child(database);
  *statement_out = public_statement;
  return CPKT_SQLITE_OK;
}

int cpkt_sqlite_prepare(cpkt_sqlite *self, const char *sql, int byte_count,
                        unsigned long flags,
                        cpkt_sqlite_statement **statement_out,
                        const char **tail_out) {
  sqlite3_stmt *native_statement;
  const char *native_tail;
  int status;
  if (statement_out != NULL)
    *statement_out = NULL;
  if (tail_out != NULL)
    *tail_out = NULL;
  if (self == NULL || cpkt_sqlite_native(self) == NULL || sql == NULL ||
      statement_out == NULL)
    return CPKT_SQLITE_MISUSE;
  native_statement = NULL;
  native_tail = NULL;
  status =
      sqlite3_prepare_v3(cpkt_sqlite_native(self), sql, byte_count,
                         (unsigned int)flags, &native_statement, &native_tail);
  if (tail_out != NULL)
    *tail_out = native_tail;
  if (status != SQLITE_OK)
    return status;
  return cpkt_sqlite_wrap_statement(self, native_statement, 0, statement_out);
}

int cpkt_sqlite_prepare16(cpkt_sqlite *self, const void *sql, int byte_count,
                          unsigned long flags,
                          cpkt_sqlite_statement **statement_out,
                          const void **tail_out) {
  sqlite3_stmt *native_statement;
  const void *native_tail;
  int status;
  if (statement_out != NULL)
    *statement_out = NULL;
  if (tail_out != NULL)
    *tail_out = NULL;
  if (self == NULL || cpkt_sqlite_native(self) == NULL || sql == NULL ||
      statement_out == NULL)
    return CPKT_SQLITE_MISUSE;
  native_statement = NULL;
  native_tail = NULL;
  status = sqlite3_prepare16_v3(cpkt_sqlite_native(self), sql, byte_count,
                                (unsigned int)flags, &native_statement,
                                &native_tail);
  if (tail_out != NULL)
    *tail_out = native_tail;
  if (status != SQLITE_OK)
    return status;
  return cpkt_sqlite_wrap_statement(self, native_statement, 0, statement_out);
}

int cpkt_sqlite_prepare_legacy(cpkt_sqlite *self, const char *sql,
                               int byte_count,
                               cpkt_sqlite_statement **statement_out,
                               const char **tail_out) {
  sqlite3_stmt *native_statement;
  const char *native_tail;
  int status;
  if (statement_out != NULL)
    *statement_out = NULL;
  if (tail_out != NULL)
    *tail_out = NULL;
  if (self == NULL || cpkt_sqlite_native(self) == NULL || sql == NULL ||
      statement_out == NULL)
    return CPKT_SQLITE_MISUSE;
  native_statement = NULL;
  native_tail = NULL;
  status = sqlite3_prepare(cpkt_sqlite_native(self), sql, byte_count,
                           &native_statement, &native_tail);
  if (tail_out != NULL)
    *tail_out = native_tail;
  if (status != SQLITE_OK)
    return status;
  return cpkt_sqlite_wrap_statement(self, native_statement, 0, statement_out);
}

int cpkt_sqlite_prepare_legacy16(cpkt_sqlite *self, const void *sql,
                                 int byte_count,
                                 cpkt_sqlite_statement **statement_out,
                                 const void **tail_out) {
  sqlite3_stmt *native_statement;
  const void *native_tail;
  int status;
  if (statement_out != NULL)
    *statement_out = NULL;
  if (tail_out != NULL)
    *tail_out = NULL;
  if (self == NULL || cpkt_sqlite_native(self) == NULL || sql == NULL ||
      statement_out == NULL)
    return CPKT_SQLITE_MISUSE;
  native_statement = NULL;
  native_tail = NULL;
  status = sqlite3_prepare16(cpkt_sqlite_native(self), sql, byte_count,
                             &native_statement, &native_tail);
  if (tail_out != NULL)
    *tail_out = native_tail;
  if (status != SQLITE_OK)
    return status;
  return cpkt_sqlite_wrap_statement(self, native_statement, 0, statement_out);
}

int cpkt_sqlite_next_statement(cpkt_sqlite *database,
                               const cpkt_sqlite_statement *previous,
                               cpkt_sqlite_statement **out) {
  sqlite3_stmt *native_statement;
  if (out != NULL)
    *out = NULL;
  if (database == NULL || cpkt_sqlite_native(database) == NULL || out == NULL) {
    return CPKT_SQLITE_MISUSE;
  }
  native_statement = sqlite3_next_stmt(cpkt_sqlite_native(database),
                                       cpkt_sqlite_native_statement(previous));
  return cpkt_sqlite_wrap_statement(database, native_statement, 1, out);
}

int cpkt_sqlite_busy_timeout(cpkt_sqlite *self, int milliseconds) {
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return CPKT_SQLITE_MISUSE;
  return sqlite3_busy_timeout(cpkt_sqlite_native(self), milliseconds);
}

int cpkt_sqlite_extended_result_codes(cpkt_sqlite *self, int enabled) {
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return CPKT_SQLITE_MISUSE;
  return sqlite3_extended_result_codes(cpkt_sqlite_native(self), enabled);
}

int cpkt_sqlite_wal_auto_checkpoint(cpkt_sqlite *self, int page_count) {
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return CPKT_SQLITE_MISUSE;
  return sqlite3_wal_autocheckpoint(cpkt_sqlite_native(self), page_count);
}

int cpkt_sqlite_wal_checkpoint(cpkt_sqlite *self, const char *schema, int mode,
                               int *log_frames_out,
                               int *checkpointed_frames_out) {
  if (log_frames_out != NULL)
    *log_frames_out = 0;
  if (checkpointed_frames_out != NULL)
    *checkpointed_frames_out = 0;
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return CPKT_SQLITE_MISUSE;
  return sqlite3_wal_checkpoint_v2(cpkt_sqlite_native(self), schema, mode,
                                   log_frames_out, checkpointed_frames_out);
}

int cpkt_sqlite_load_extension(cpkt_sqlite *self, const char *path,
                               const char *entry_point, char **error_out) {
  if (error_out != NULL)
    *error_out = NULL;
  if (self == NULL || cpkt_sqlite_native(self) == NULL || path == NULL) {
    return CPKT_SQLITE_MISUSE;
  }
  return sqlite3_load_extension(cpkt_sqlite_native(self), path, entry_point,
                                error_out);
}

int cpkt_sqlite_enable_extension_loading(cpkt_sqlite *self, int enabled) {
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return CPKT_SQLITE_MISUSE;
  return sqlite3_enable_load_extension(cpkt_sqlite_native(self), enabled);
}

int cpkt_sqlite_unlock_notify(cpkt_sqlite *self,
                              cpkt_sqlite_unlock_notify_callback callback,
                              void *context) {
  cpkt_sqlite_state *state;
  cpkt_sqlite_unlock_notify_binding *binding;
  cpkt_sqlite_unlock_notify_binding *old_binding;
  sqlite3_mutex *mutex;
  pthread_mutex_t *global_mutex;
  int status;
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return SQLITE_MISUSE;
  state = cpkt_sqlite_state_for(self);
  if (state == NULL)
    return SQLITE_NOMEM;
  mutex = cpkt_sqlite_connection_lock(self);
  global_mutex = cpkt_sqlite_global_mutex();
  if (callback == NULL) {
    status = sqlite3_unlock_notify(cpkt_sqlite_native(self), NULL, NULL);
    if (status == SQLITE_OK) {
      cpkt_sqlite_global_lock(global_mutex);
      old_binding = state->unlock_notify_binding;
      state->unlock_notify_binding = NULL;
      cpkt_sqlite_global_unlock(global_mutex);
      free(old_binding);
    }
    cpkt_sqlite_connection_unlock(mutex);
    return status;
  }
  binding = (cpkt_sqlite_unlock_notify_binding *)calloc(1, sizeof(*binding));
  if (binding == NULL) {
    cpkt_sqlite_connection_unlock(mutex);
    return SQLITE_NOMEM;
  }
  binding->state = state;
  binding->callback = callback;
  binding->context = context;
  cpkt_sqlite_global_lock(global_mutex);
  old_binding = state->unlock_notify_binding;
  state->unlock_notify_binding = binding;
  cpkt_sqlite_global_unlock(global_mutex);
  /* SQLite serializes this call with delivery using its STATIC_MAIN mutex.
   * Keep old_binding alive until the call returns. */
  status = sqlite3_unlock_notify(cpkt_sqlite_native(self),
                                 cpkt_sqlite_unlock_notify_trampoline, binding);
  if (status != SQLITE_OK) {
    cpkt_sqlite_global_lock(global_mutex);
    if (state->unlock_notify_binding == binding)
      state->unlock_notify_binding =
          old_binding != NULL && !old_binding->delivered ? old_binding : NULL;
    cpkt_sqlite_global_unlock(global_mutex);
    if (old_binding != NULL && old_binding->delivered)
      free(old_binding);
    free(binding);
    cpkt_sqlite_connection_unlock(mutex);
    return status;
  }
  free(old_binding);
  cpkt_sqlite_connection_unlock(mutex);
  return status;
}

int cpkt_sqlite_overload_function(cpkt_sqlite *self, const char *name,
                                  int argument_count) {
  if (self == NULL || cpkt_sqlite_native(self) == NULL || name == NULL) {
    return SQLITE_MISUSE;
  }
  return sqlite3_overload_function(cpkt_sqlite_native(self), name,
                                   argument_count);
}

int cpkt_sqlite_drop_modules(cpkt_sqlite *self, int kept_name_count,
                             const char *const *kept_names) {
  const char **names;
  int index;
  int status;
  if (self == NULL || cpkt_sqlite_native(self) == NULL || kept_name_count < 0 ||
      (kept_name_count > 0 && kept_names == NULL))
    return SQLITE_MISUSE;
  names = NULL;
  if (kept_name_count > 0) {
    names = (const char **)calloc((size_t)kept_name_count + 1, sizeof(*names));
    if (names == NULL)
      return SQLITE_NOMEM;
    for (index = 0; index < kept_name_count; ++index) {
      if (kept_names[index] == NULL) {
        free(names);
        return SQLITE_MISUSE;
      }
      names[index] = kept_names[index];
    }
  }
  status = sqlite3_drop_modules(cpkt_sqlite_native(self), names);
  free(names);
  return status;
}

static int cpkt_sqlite_virtual_table_set_error(cpkt_sqlite_virtual_table *self,
                                               const char *message) {
  cpkt_sqlite_native_virtual_table *native_table;
  if (self == NULL || self->internal == NULL || message == NULL)
    return SQLITE_MISUSE;
  native_table = (cpkt_sqlite_native_virtual_table *)self->internal;
  sqlite3_free(native_table->base.zErrMsg);
  native_table->base.zErrMsg = sqlite3_mprintf("%s", message);
  return native_table->base.zErrMsg == NULL ? SQLITE_NOMEM : SQLITE_OK;
}

cpkt_sqlite_virtual_table *cpkt_sqlite_virtual_table_new(void *state) {
  cpkt_sqlite_virtual_table *table;
  table = (cpkt_sqlite_virtual_table *)calloc(1, sizeof(*table));
  if (table == NULL)
    return NULL;
  table->set_error = cpkt_sqlite_virtual_table_set_error;
  table->state = state;
  return table;
}

cpkt_sqlite_virtual_cursor *
cpkt_sqlite_virtual_cursor_new(cpkt_sqlite_virtual_table *table, void *state) {
  cpkt_sqlite_virtual_cursor *cursor;
  if (table == NULL)
    return NULL;
  cursor = (cpkt_sqlite_virtual_cursor *)calloc(1, sizeof(*cursor));
  if (cursor == NULL)
    return NULL;
  cursor->table = table;
  cursor->state = state;
  return cursor;
}

static int cpkt_sqlite_module_connect(sqlite3 *native_database, void *auxiliary,
                                      int argument_count,
                                      const char *const *arguments,
                                      sqlite3_vtab **out, char **error_out,
                                      int is_create) {
  cpkt_sqlite_module_binding *module;
  cpkt_sqlite_native_virtual_table *native_table;
  cpkt_sqlite_virtual_table *public_table;
  cpkt_sqlite_module_connect_callback callback;
  int status;
  (void)native_database;
  if (out == NULL || auxiliary == NULL)
    return SQLITE_MISUSE;
  *out = NULL;
  module = (cpkt_sqlite_module_binding *)auxiliary;
  callback = is_create ? module->methods.create : module->methods.connect;
  if (callback == NULL)
    return SQLITE_MISUSE;
  native_table =
      (cpkt_sqlite_native_virtual_table *)calloc(1, sizeof(*native_table));
  if (native_table == NULL)
    return SQLITE_NOMEM;
  public_table = NULL;
  status = callback(module->methods.context, module->database, argument_count,
                    arguments, &public_table, error_out);
  if (status != SQLITE_OK) {
    free(public_table);
    free(native_table);
    return status;
  }
  if (public_table == NULL || public_table->internal != NULL) {
    cpkt_sqlite_module_table_callback cleanup;
    cleanup =
        is_create ? module->methods.destroy_table : module->methods.disconnect;
    if (public_table != NULL && cleanup != NULL)
      (void)cleanup(module->methods.context, public_table);
    free(public_table);
    free(native_table);
    return SQLITE_MISUSE;
  }
  native_table->base.pModule = &module->module;
  native_table->public_table = public_table;
  native_table->module = module;
  public_table->internal = native_table;
  *out = &native_table->base;
  return SQLITE_OK;
}

static int cpkt_sqlite_module_create_trampoline(
    sqlite3 *database, void *auxiliary, int argument_count,
    const char *const *arguments, sqlite3_vtab **out, char **error_out) {
  return cpkt_sqlite_module_connect(database, auxiliary, argument_count,
                                    arguments, out, error_out, 1);
}

static int cpkt_sqlite_module_connect_trampoline(
    sqlite3 *database, void *auxiliary, int argument_count,
    const char *const *arguments, sqlite3_vtab **out, char **error_out) {
  return cpkt_sqlite_module_connect(database, auxiliary, argument_count,
                                    arguments, out, error_out, 0);
}

static int
cpkt_sqlite_module_best_index_trampoline(sqlite3_vtab *table,
                                         sqlite3_index_info *native_info) {
  cpkt_sqlite_native_virtual_table *native_table;
  cpkt_sqlite_module_binding *module;
  cpkt_sqlite_index_constraint *constraints;
  cpkt_sqlite_index_order *orders;
  cpkt_sqlite_index_constraint_usage *usages;
  cpkt_sqlite_index_info info;
  int index;
  int status;
  if (table == NULL || native_info == NULL)
    return SQLITE_MISUSE;
  native_table = (cpkt_sqlite_native_virtual_table *)table;
  module = native_table->module;
  if (module == NULL || module->methods.best_index == NULL)
    return SQLITE_MISUSE;
  constraints = NULL;
  orders = NULL;
  usages = NULL;
  if (native_info->nConstraint > 0) {
    constraints = (cpkt_sqlite_index_constraint *)calloc(
        (size_t)native_info->nConstraint, sizeof(*constraints));
    usages = (cpkt_sqlite_index_constraint_usage *)calloc(
        (size_t)native_info->nConstraint, sizeof(*usages));
    if (constraints == NULL || usages == NULL) {
      free(constraints);
      free(usages);
      return SQLITE_NOMEM;
    }
    for (index = 0; index < native_info->nConstraint; ++index) {
      constraints[index].column = native_info->aConstraint[index].iColumn;
      constraints[index].operation = native_info->aConstraint[index].op;
      constraints[index].usable = native_info->aConstraint[index].usable;
    }
  }
  if (native_info->nOrderBy > 0) {
    orders = (cpkt_sqlite_index_order *)calloc((size_t)native_info->nOrderBy,
                                               sizeof(*orders));
    if (orders == NULL) {
      free(constraints);
      free(usages);
      return SQLITE_NOMEM;
    }
    for (index = 0; index < native_info->nOrderBy; ++index) {
      orders[index].column = native_info->aOrderBy[index].iColumn;
      orders[index].descending = native_info->aOrderBy[index].desc;
    }
  }
  memset(&info, 0, sizeof(info));
  info.constraint_count = native_info->nConstraint;
  info.constraints = constraints;
  info.order_count = native_info->nOrderBy;
  info.orders = orders;
  info.usages = usages;
  info.index_number = native_info->idxNum;
  info.index_string = native_info->idxStr;
  info.order_by_consumed = native_info->orderByConsumed;
  info.estimated_cost = native_info->estimatedCost;
  info.estimated_rows = cpkt_sqlite_public_i64(native_info->estimatedRows);
  info.index_flags = (unsigned long)native_info->idxFlags;
  info.columns_used = cpkt_sqlite_public_u64(native_info->colUsed);
  info.internal = native_info;
  status = module->methods.best_index(module->methods.context,
                                      native_table->public_table, &info);
  if (status == SQLITE_OK) {
    for (index = 0; index < native_info->nConstraint; ++index) {
      native_info->aConstraintUsage[index].argvIndex =
          usages[index].argument_index;
      native_info->aConstraintUsage[index].omit =
          (unsigned char)usages[index].omit;
    }
    native_info->idxNum = info.index_number;
    native_info->orderByConsumed = info.order_by_consumed;
    native_info->estimatedCost = info.estimated_cost;
    native_info->estimatedRows = cpkt_sqlite_native_i64(info.estimated_rows);
    native_info->idxFlags = (int)info.index_flags;
    if (info.index_string != NULL) {
      native_info->idxStr = sqlite3_mprintf("%s", info.index_string);
      if (native_info->idxStr == NULL)
        status = SQLITE_NOMEM;
      else
        native_info->needToFreeIdxStr = 1;
    }
  }
  free(orders);
  free(usages);
  free(constraints);
  return status;
}

static int cpkt_sqlite_module_table_trampoline(sqlite3_vtab *table,
                                               int destroy) {
  cpkt_sqlite_native_virtual_table *native_table;
  cpkt_sqlite_module_binding *module;
  cpkt_sqlite_virtual_function_binding *function;
  cpkt_sqlite_virtual_function_binding *next;
  cpkt_sqlite_module_table_callback callback;
  int status;
  if (table == NULL)
    return SQLITE_MISUSE;
  native_table = (cpkt_sqlite_native_virtual_table *)table;
  module = native_table->module;
  callback =
      destroy ? module->methods.destroy_table : module->methods.disconnect;
  status = callback == NULL
               ? SQLITE_OK
               : callback(module->methods.context, native_table->public_table);
  if (destroy && status != SQLITE_OK)
    return status;
  function = native_table->functions;
  while (function != NULL) {
    next = function->next;
    free(function);
    function = next;
  }
  if (native_table->public_table != NULL)
    native_table->public_table->internal = NULL;
  free(native_table->public_table);
  free(native_table);
  return status;
}

static int cpkt_sqlite_module_disconnect_trampoline(sqlite3_vtab *table) {
  return cpkt_sqlite_module_table_trampoline(table, 0);
}

static int cpkt_sqlite_module_destroy_trampoline(sqlite3_vtab *table) {
  return cpkt_sqlite_module_table_trampoline(table, 1);
}

static int cpkt_sqlite_module_find_function_trampoline(
    sqlite3_vtab *table, int argument_count, const char *name,
    void (**function_out)(sqlite3_context *, int, sqlite3_value **),
    void **user_data_out) {
  cpkt_sqlite_native_virtual_table *native_table;
  cpkt_sqlite_module_binding *module;
  cpkt_sqlite_virtual_function_binding *binding;
  cpkt_sqlite_scalar_callback function;
  void *user_data;
  int found;
  if (table == NULL || function_out == NULL || user_data_out == NULL ||
      name == NULL) {
    return 0;
  }
  native_table = (cpkt_sqlite_native_virtual_table *)table;
  module = native_table->module;
  if (module == NULL || module->methods.find_function == NULL)
    return 0;
  function = NULL;
  user_data = NULL;
  found = module->methods.find_function(
      module->methods.context, native_table->public_table, argument_count, name,
      &function, &user_data);
  if (found == 0 || function == NULL)
    return 0;
  /* SQLite has no per-statement destructor for xFindFunction user data.
   * Keep distinct pairs until table disconnect, preserving prepared queries,
   * but do not allocate another binding for the same callback and context. */
  for (binding = native_table->functions; binding != NULL;
       binding = binding->next)
    if (binding->function.scalar == function &&
        binding->function.user_data == user_data) {
      *function_out = cpkt_sqlite_function_scalar_trampoline;
      *user_data_out = &binding->function;
      return found;
    }
  binding = (cpkt_sqlite_virtual_function_binding *)calloc(1, sizeof(*binding));
  if (binding == NULL)
    return 0;
  binding->function.database = module->database;
  binding->function.user_data = user_data;
  binding->function.scalar = function;
  binding->next = native_table->functions;
  native_table->functions = binding;
  *function_out = cpkt_sqlite_function_scalar_trampoline;
  *user_data_out = &binding->function;
  return found;
}

static int cpkt_sqlite_module_open_trampoline(sqlite3_vtab *table,
                                              sqlite3_vtab_cursor **out) {
  cpkt_sqlite_native_virtual_table *native_table;
  cpkt_sqlite_native_virtual_cursor *native_cursor;
  cpkt_sqlite_virtual_cursor *public_cursor;
  cpkt_sqlite_module_binding *module;
  int status;
  if (table == NULL || out == NULL)
    return SQLITE_MISUSE;
  *out = NULL;
  native_table = (cpkt_sqlite_native_virtual_table *)table;
  module = native_table->module;
  if (module == NULL || module->methods.open == NULL)
    return SQLITE_MISUSE;
  native_cursor =
      (cpkt_sqlite_native_virtual_cursor *)calloc(1, sizeof(*native_cursor));
  if (native_cursor == NULL)
    return SQLITE_NOMEM;
  public_cursor = NULL;
  status = module->methods.open(module->methods.context,
                                native_table->public_table, &public_cursor);
  if (status != SQLITE_OK) {
    free(public_cursor);
    free(native_cursor);
    return status;
  }
  if (public_cursor == NULL ||
      public_cursor->table != native_table->public_table ||
      public_cursor->internal != NULL) {
    if (public_cursor != NULL && module->methods.close != NULL)
      (void)module->methods.close(module->methods.context, public_cursor);
    free(public_cursor);
    free(native_cursor);
    return SQLITE_MISUSE;
  }
  native_cursor->base.pVtab = table;
  native_cursor->public_cursor = public_cursor;
  native_cursor->table = native_table;
  public_cursor->internal = native_cursor;
  *out = &native_cursor->base;
  return SQLITE_OK;
}

static int cpkt_sqlite_module_close_trampoline(sqlite3_vtab_cursor *cursor) {
  cpkt_sqlite_native_virtual_cursor *native_cursor;
  cpkt_sqlite_module_binding *module;
  int status;
  if (cursor == NULL)
    return SQLITE_MISUSE;
  native_cursor = (cpkt_sqlite_native_virtual_cursor *)cursor;
  module = native_cursor->table->module;
  status = module->methods.close == NULL
               ? SQLITE_OK
               : module->methods.close(module->methods.context,
                                       native_cursor->public_cursor);
  if (native_cursor->public_cursor != NULL)
    native_cursor->public_cursor->internal = NULL;
  free(native_cursor->public_cursor);
  free(native_cursor);
  return status;
}

static int cpkt_sqlite_module_filter_trampoline(
    sqlite3_vtab_cursor *cursor, int index_number, const char *index_string,
    int argument_count, sqlite3_value **native_arguments) {
  cpkt_sqlite_native_virtual_cursor *native_cursor;
  cpkt_sqlite_module_binding *module;
  cpkt_sqlite_value *values;
  cpkt_sqlite_value **arguments;
  int index;
  int status;
  if (cursor == NULL)
    return SQLITE_MISUSE;
  native_cursor = (cpkt_sqlite_native_virtual_cursor *)cursor;
  module = native_cursor->table->module;
  if (module->methods.filter == NULL)
    return SQLITE_MISUSE;
  values = NULL;
  arguments = NULL;
  if (argument_count > 0) {
    values =
        (cpkt_sqlite_value *)calloc((size_t)argument_count, sizeof(*values));
    arguments = (cpkt_sqlite_value **)calloc((size_t)argument_count,
                                             sizeof(*arguments));
    if (values == NULL || arguments == NULL) {
      free(arguments);
      free(values);
      return SQLITE_NOMEM;
    }
    for (index = 0; index < argument_count; ++index) {
      values[index].value = native_arguments[index];
      arguments[index] = &values[index];
    }
  }
  status = module->methods.filter(module->methods.context,
                                  native_cursor->public_cursor, index_number,
                                  index_string, argument_count, arguments);
  free(arguments);
  free(values);
  return status;
}

static int cpkt_sqlite_module_next_trampoline(sqlite3_vtab_cursor *cursor) {
  cpkt_sqlite_native_virtual_cursor *native_cursor;
  cpkt_sqlite_module_binding *module;
  if (cursor == NULL)
    return SQLITE_MISUSE;
  native_cursor = (cpkt_sqlite_native_virtual_cursor *)cursor;
  module = native_cursor->table->module;
  return module->methods.next == NULL
             ? SQLITE_MISUSE
             : module->methods.next(module->methods.context,
                                    native_cursor->public_cursor);
}

static int cpkt_sqlite_module_eof_trampoline(sqlite3_vtab_cursor *cursor) {
  cpkt_sqlite_native_virtual_cursor *native_cursor;
  cpkt_sqlite_module_binding *module;
  if (cursor == NULL)
    return 1;
  native_cursor = (cpkt_sqlite_native_virtual_cursor *)cursor;
  module = native_cursor->table->module;
  return module->methods.eof == NULL
             ? 1
             : module->methods.eof(module->methods.context,
                                   native_cursor->public_cursor) != 0;
}

static int cpkt_sqlite_module_column_trampoline(sqlite3_vtab_cursor *cursor,
                                                sqlite3_context *native_context,
                                                int column) {
  cpkt_sqlite_native_virtual_cursor *native_cursor;
  cpkt_sqlite_module_binding *module;
  cpkt_sqlite_context public_context;
  if (cursor == NULL || native_context == NULL)
    return SQLITE_MISUSE;
  native_cursor = (cpkt_sqlite_native_virtual_cursor *)cursor;
  module = native_cursor->table->module;
  if (module->methods.column == NULL)
    return SQLITE_MISUSE;
  public_context.context = native_context;
  public_context.database = module->database;
  return module->methods.column(module->methods.context,
                                native_cursor->public_cursor, &public_context,
                                column);
}

static int cpkt_sqlite_module_rowid_trampoline(sqlite3_vtab_cursor *cursor,
                                               sqlite3_int64 *rowid_out) {
  cpkt_sqlite_native_virtual_cursor *native_cursor;
  cpkt_sqlite_module_binding *module;
  cpkt_sqlite_i64 rowid;
  int status;
  if (cursor == NULL || rowid_out == NULL)
    return SQLITE_MISUSE;
  native_cursor = (cpkt_sqlite_native_virtual_cursor *)cursor;
  module = native_cursor->table->module;
  if (module->methods.rowid == NULL)
    return SQLITE_MISUSE;
  rowid.high = 0;
  rowid.low = 0;
  status = module->methods.rowid(module->methods.context,
                                 native_cursor->public_cursor, &rowid);
  if (status == SQLITE_OK)
    *rowid_out = cpkt_sqlite_native_i64(rowid);
  return status;
}

static int
cpkt_sqlite_module_update_trampoline(sqlite3_vtab *table, int argument_count,
                                     sqlite3_value **native_arguments,
                                     sqlite3_int64 *rowid_out) {
  cpkt_sqlite_native_virtual_table *native_table;
  cpkt_sqlite_module_binding *module;
  cpkt_sqlite_value *values;
  cpkt_sqlite_value **arguments;
  cpkt_sqlite_i64 rowid;
  int index;
  int status;
  if (table == NULL || rowid_out == NULL)
    return SQLITE_MISUSE;
  native_table = (cpkt_sqlite_native_virtual_table *)table;
  module = native_table->module;
  if (module->methods.update == NULL)
    return SQLITE_MISUSE;
  values = NULL;
  arguments = NULL;
  if (argument_count > 0) {
    values =
        (cpkt_sqlite_value *)calloc((size_t)argument_count, sizeof(*values));
    arguments = (cpkt_sqlite_value **)calloc((size_t)argument_count,
                                             sizeof(*arguments));
    if (values == NULL || arguments == NULL) {
      free(arguments);
      free(values);
      return SQLITE_NOMEM;
    }
    for (index = 0; index < argument_count; ++index) {
      values[index].value = native_arguments[index];
      arguments[index] = &values[index];
    }
  }
  rowid = cpkt_sqlite_public_i64(*rowid_out);
  status = module->methods.update(module->methods.context,
                                  native_table->public_table, argument_count,
                                  arguments, &rowid);
  if (status == SQLITE_OK)
    *rowid_out = cpkt_sqlite_native_i64(rowid);
  free(arguments);
  free(values);
  return status;
}

static int cpkt_sqlite_module_table_operation(sqlite3_vtab *table,
                                              int operation, int argument) {
  cpkt_sqlite_native_virtual_table *native_table;
  cpkt_sqlite_module_binding *module;
  cpkt_sqlite_module_table_callback table_callback;
  cpkt_sqlite_module_savepoint_callback savepoint_callback;
  if (table == NULL)
    return SQLITE_MISUSE;
  native_table = (cpkt_sqlite_native_virtual_table *)table;
  module = native_table->module;
  table_callback = NULL;
  savepoint_callback = NULL;
  if (operation == 0)
    table_callback = module->methods.begin;
  else if (operation == 1)
    table_callback = module->methods.sync;
  else if (operation == 2)
    table_callback = module->methods.commit;
  else if (operation == 3)
    table_callback = module->methods.rollback;
  else if (operation == 4)
    savepoint_callback = module->methods.savepoint;
  else if (operation == 5)
    savepoint_callback = module->methods.release;
  else if (operation == 6)
    savepoint_callback = module->methods.rollback_to;
  if (savepoint_callback != NULL)
    return savepoint_callback(module->methods.context,
                              native_table->public_table, argument);
  return table_callback == NULL ? SQLITE_OK
                                : table_callback(module->methods.context,
                                                 native_table->public_table);
}

static int cpkt_sqlite_module_begin_trampoline(sqlite3_vtab *table) {
  return cpkt_sqlite_module_table_operation(table, 0, 0);
}
static int cpkt_sqlite_module_sync_trampoline(sqlite3_vtab *table) {
  return cpkt_sqlite_module_table_operation(table, 1, 0);
}
static int cpkt_sqlite_module_commit_trampoline(sqlite3_vtab *table) {
  return cpkt_sqlite_module_table_operation(table, 2, 0);
}
static int cpkt_sqlite_module_rollback_trampoline(sqlite3_vtab *table) {
  return cpkt_sqlite_module_table_operation(table, 3, 0);
}
static int cpkt_sqlite_module_savepoint_trampoline(sqlite3_vtab *table,
                                                   int value) {
  return cpkt_sqlite_module_table_operation(table, 4, value);
}
static int cpkt_sqlite_module_release_trampoline(sqlite3_vtab *table,
                                                 int value) {
  return cpkt_sqlite_module_table_operation(table, 5, value);
}
static int cpkt_sqlite_module_rollback_to_trampoline(sqlite3_vtab *table,
                                                     int value) {
  return cpkt_sqlite_module_table_operation(table, 6, value);
}

static int cpkt_sqlite_module_rename_trampoline(sqlite3_vtab *table,
                                                const char *name) {
  cpkt_sqlite_native_virtual_table *native_table;
  cpkt_sqlite_module_binding *module;
  if (table == NULL || name == NULL)
    return SQLITE_MISUSE;
  native_table = (cpkt_sqlite_native_virtual_table *)table;
  module = native_table->module;
  return module->methods.rename == NULL
             ? SQLITE_OK
             : module->methods.rename(module->methods.context,
                                      native_table->public_table, name);
}

static int cpkt_sqlite_module_integrity_trampoline(sqlite3_vtab *table,
                                                   const char *schema,
                                                   const char *name, int flags,
                                                   char **error_out) {
  cpkt_sqlite_native_virtual_table *native_table;
  cpkt_sqlite_module_binding *module;
  if (table == NULL)
    return SQLITE_MISUSE;
  native_table = (cpkt_sqlite_native_virtual_table *)table;
  module = native_table->module;
  return module->methods.integrity == NULL
             ? SQLITE_OK
             : module->methods.integrity(module->methods.context,
                                         native_table->public_table, schema,
                                         name, flags, error_out);
}

static void cpkt_sqlite_module_destroy_binding(void *context) {
  cpkt_sqlite_module_binding *module;
  module = (cpkt_sqlite_module_binding *)context;
  if (module == NULL)
    return;
  if (module->methods.destroy != NULL)
    module->methods.destroy(module->methods.context);
  free(module);
}

int cpkt_sqlite_declare_virtual_table(cpkt_sqlite *database,
                                      const char *schema) {
  if (database == NULL || cpkt_sqlite_native(database) == NULL ||
      schema == NULL) {
    return SQLITE_MISUSE;
  }
  return sqlite3_declare_vtab(cpkt_sqlite_native(database), schema);
}

int cpkt_sqlite_create_module(cpkt_sqlite *database, const char *name,
                              const cpkt_sqlite_module_methods *methods) {
  cpkt_sqlite_module_binding *module;
  int status;
  if (database == NULL || cpkt_sqlite_native(database) == NULL ||
      name == NULL || methods == NULL || methods->best_index == NULL ||
      methods->open == NULL || methods->close == NULL ||
      methods->filter == NULL || methods->next == NULL ||
      methods->eof == NULL || methods->column == NULL ||
      methods->rowid == NULL) {
    return SQLITE_MISUSE;
  }
  if (methods->create == NULL && methods->connect == NULL)
    return SQLITE_MISUSE;
  module = (cpkt_sqlite_module_binding *)calloc(1, sizeof(*module));
  if (module == NULL)
    return SQLITE_NOMEM;
  module->database = database;
  module->methods = *methods;
  module->module.iVersion = 4;
  module->module.xCreate =
      methods->create == NULL ? NULL : cpkt_sqlite_module_create_trampoline;
  module->module.xConnect = methods->connect == NULL
                                ? NULL
                                : (methods->connect == methods->create
                                       ? cpkt_sqlite_module_create_trampoline
                                       : cpkt_sqlite_module_connect_trampoline);
  module->module.xBestIndex = cpkt_sqlite_module_best_index_trampoline;
  module->module.xDisconnect = cpkt_sqlite_module_disconnect_trampoline;
  module->module.xDestroy = cpkt_sqlite_module_destroy_trampoline;
  module->module.xOpen = cpkt_sqlite_module_open_trampoline;
  module->module.xClose = cpkt_sqlite_module_close_trampoline;
  module->module.xFilter = cpkt_sqlite_module_filter_trampoline;
  module->module.xNext = cpkt_sqlite_module_next_trampoline;
  module->module.xEof = cpkt_sqlite_module_eof_trampoline;
  module->module.xColumn = cpkt_sqlite_module_column_trampoline;
  module->module.xRowid = cpkt_sqlite_module_rowid_trampoline;
  module->module.xUpdate =
      methods->update == NULL ? NULL : cpkt_sqlite_module_update_trampoline;
  module->module.xBegin = cpkt_sqlite_module_begin_trampoline;
  module->module.xSync = cpkt_sqlite_module_sync_trampoline;
  module->module.xCommit = cpkt_sqlite_module_commit_trampoline;
  module->module.xRollback = cpkt_sqlite_module_rollback_trampoline;
  module->module.xFindFunction =
      methods->find_function == NULL
          ? NULL
          : cpkt_sqlite_module_find_function_trampoline;
  module->module.xRename = cpkt_sqlite_module_rename_trampoline;
  module->module.xSavepoint = cpkt_sqlite_module_savepoint_trampoline;
  module->module.xRelease = cpkt_sqlite_module_release_trampoline;
  module->module.xRollbackTo = cpkt_sqlite_module_rollback_to_trampoline;
  module->module.xShadowName = methods->shadow_name;
  module->module.xIntegrity = cpkt_sqlite_module_integrity_trampoline;
  status = sqlite3_create_module_v2(cpkt_sqlite_native(database), name,
                                    &module->module, module,
                                    cpkt_sqlite_module_destroy_binding);
  if (status != SQLITE_OK)
    return status;
  return SQLITE_OK;
}

int cpkt_sqlite_virtual_table_config_none(cpkt_sqlite *database,
                                          int operation) {
  if (database == NULL || cpkt_sqlite_native(database) == NULL)
    return SQLITE_MISUSE;
  if (operation != SQLITE_VTAB_INNOCUOUS &&
      operation != SQLITE_VTAB_DIRECTONLY &&
      operation != SQLITE_VTAB_USES_ALL_SCHEMAS)
    return SQLITE_MISUSE;
  return sqlite3_vtab_config(cpkt_sqlite_native(database), operation);
}

int cpkt_sqlite_virtual_table_config_int(cpkt_sqlite *database, int operation,
                                         int value) {
  if (database == NULL || cpkt_sqlite_native(database) == NULL)
    return SQLITE_MISUSE;
  if (operation != SQLITE_VTAB_CONSTRAINT_SUPPORT)
    return SQLITE_MISUSE;
  return sqlite3_vtab_config(cpkt_sqlite_native(database), operation, value);
}

int cpkt_sqlite_virtual_table_on_conflict(cpkt_sqlite *database) {
  if (database == NULL || cpkt_sqlite_native(database) == NULL)
    return SQLITE_MISUSE;
  return sqlite3_vtab_on_conflict(cpkt_sqlite_native(database));
}

int cpkt_sqlite_context_no_change(const cpkt_sqlite_context *context) {
  if (context == NULL || cpkt_sqlite_native_context(context) == NULL)
    return 0;
  return sqlite3_vtab_nochange(cpkt_sqlite_native_context(context));
}

int cpkt_sqlite_index_info_rhs_value(const cpkt_sqlite_index_info *self,
                                     int constraint_index,
                                     cpkt_sqlite_value *value_out) {
  sqlite3_value *value;
  int status;
  if (value_out != NULL)
    memset(value_out, 0, sizeof(*value_out));
  if (self == NULL || self->internal == NULL || value_out == NULL)
    return SQLITE_MISUSE;
  value = NULL;
  status = sqlite3_vtab_rhs_value((sqlite3_index_info *)self->internal,
                                  constraint_index, &value);
  if (status == SQLITE_OK)
    value_out->value = value;
  return status;
}

const char *cpkt_sqlite_index_info_collation(const cpkt_sqlite_index_info *self,
                                             int constraint_index) {
  if (self == NULL || self->internal == NULL)
    return NULL;
  return sqlite3_vtab_collation((sqlite3_index_info *)self->internal,
                                constraint_index);
}

int cpkt_sqlite_index_info_distinct(const cpkt_sqlite_index_info *self) {
  if (self == NULL || self->internal == NULL)
    return 0;
  return sqlite3_vtab_distinct((sqlite3_index_info *)self->internal);
}

int cpkt_sqlite_index_info_set_in(cpkt_sqlite_index_info *self,
                                  int constraint_index, int enabled) {
  if (self == NULL || self->internal == NULL)
    return SQLITE_MISUSE;
  return sqlite3_vtab_in((sqlite3_index_info *)self->internal, constraint_index,
                         enabled);
}

int cpkt_sqlite_virtual_table_in_first(const cpkt_sqlite_value *input,
                                       cpkt_sqlite_value *value_out) {
  sqlite3_value *value;
  int status;
  if (value_out != NULL)
    memset(value_out, 0, sizeof(*value_out));
  if (input == NULL || cpkt_sqlite_native_value(input) == NULL ||
      value_out == NULL) {
    return SQLITE_MISUSE;
  }
  value = NULL;
  status = sqlite3_vtab_in_first(cpkt_sqlite_native_value(input), &value);
  if (status == SQLITE_OK)
    value_out->value = value;
  return status;
}

int cpkt_sqlite_virtual_table_in_next(const cpkt_sqlite_value *input,
                                      cpkt_sqlite_value *value_out) {
  sqlite3_value *value;
  int status;
  if (value_out != NULL)
    memset(value_out, 0, sizeof(*value_out));
  if (input == NULL || cpkt_sqlite_native_value(input) == NULL ||
      value_out == NULL) {
    return SQLITE_MISUSE;
  }
  value = NULL;
  status = sqlite3_vtab_in_next(cpkt_sqlite_native_value(input), &value);
  if (status == SQLITE_OK)
    value_out->value = value;
  return status;
}

int cpkt_sqlite_database_config_int(cpkt_sqlite *self, int operation, int value,
                                    int *result_out) {
  if (self == NULL || cpkt_sqlite_native(self) == NULL || result_out == NULL) {
    return CPKT_SQLITE_MISUSE;
  }
  switch (operation) {
  case SQLITE_DBCONFIG_ENABLE_FKEY:
  case SQLITE_DBCONFIG_ENABLE_TRIGGER:
  case SQLITE_DBCONFIG_ENABLE_FTS3_TOKENIZER:
  case SQLITE_DBCONFIG_ENABLE_LOAD_EXTENSION:
  case SQLITE_DBCONFIG_NO_CKPT_ON_CLOSE:
  case SQLITE_DBCONFIG_ENABLE_QPSG:
  case SQLITE_DBCONFIG_TRIGGER_EQP:
  case SQLITE_DBCONFIG_RESET_DATABASE:
  case SQLITE_DBCONFIG_DEFENSIVE:
  case SQLITE_DBCONFIG_WRITABLE_SCHEMA:
  case SQLITE_DBCONFIG_LEGACY_ALTER_TABLE:
  case SQLITE_DBCONFIG_DQS_DML:
  case SQLITE_DBCONFIG_DQS_DDL:
  case SQLITE_DBCONFIG_ENABLE_VIEW:
  case SQLITE_DBCONFIG_LEGACY_FILE_FORMAT:
  case SQLITE_DBCONFIG_TRUSTED_SCHEMA:
  case SQLITE_DBCONFIG_STMT_SCANSTATUS:
  case SQLITE_DBCONFIG_REVERSE_SCANORDER:
  case SQLITE_DBCONFIG_ENABLE_ATTACH_CREATE:
  case SQLITE_DBCONFIG_ENABLE_ATTACH_WRITE:
  case SQLITE_DBCONFIG_ENABLE_COMMENTS:
  case SQLITE_DBCONFIG_FP_DIGITS:
    break;
  default:
    return SQLITE_MISUSE;
  }
  return sqlite3_db_config(cpkt_sqlite_native(self), operation, value,
                           result_out);
}

int cpkt_sqlite_database_config_lookaside(cpkt_sqlite *self, void *buffer,
                                          int slot_byte_count, int slot_count) {
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return CPKT_SQLITE_MISUSE;
  return sqlite3_db_config(cpkt_sqlite_native(self), SQLITE_DBCONFIG_LOOKASIDE,
                           buffer, slot_byte_count, slot_count);
}

int cpkt_sqlite_database_config_main_name(cpkt_sqlite *self, const char *name) {
  if (self == NULL || cpkt_sqlite_native(self) == NULL || name == NULL) {
    return CPKT_SQLITE_MISUSE;
  }
  return sqlite3_db_config(cpkt_sqlite_native(self), SQLITE_DBCONFIG_MAINDBNAME,
                           name);
}

int cpkt_sqlite_changes(const cpkt_sqlite *self) {
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return 0;
  return sqlite3_changes(cpkt_sqlite_native(self));
}

cpkt_sqlite_i64 cpkt_sqlite_last_insert_rowid(const cpkt_sqlite *self) {
  cpkt_sqlite_i64 result;
  result.high = 0;
  result.low = 0;
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return result;
  return cpkt_sqlite_public_i64(
      sqlite3_last_insert_rowid(cpkt_sqlite_native(self)));
}

const char *cpkt_sqlite_error(const cpkt_sqlite *self) {
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return "invalid SQLite receiver";
  return sqlite3_errmsg(cpkt_sqlite_native(self));
}

const void *cpkt_sqlite_error16(const cpkt_sqlite *self) {
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return NULL;
  return sqlite3_errmsg16(cpkt_sqlite_native(self));
}

int cpkt_sqlite_error_code(const cpkt_sqlite *self) {
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return CPKT_SQLITE_MISUSE;
  return sqlite3_extended_errcode(cpkt_sqlite_native(self));
}

int cpkt_sqlite_result_code(const cpkt_sqlite *self) {
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return CPKT_SQLITE_MISUSE;
  return sqlite3_errcode(cpkt_sqlite_native(self));
}

int cpkt_sqlite_set_error(cpkt_sqlite *self, int code, const char *message) {
  if (self == NULL || cpkt_sqlite_native(self) == NULL || message == NULL) {
    return CPKT_SQLITE_MISUSE;
  }
  return sqlite3_set_errmsg(cpkt_sqlite_native(self), code, message);
}

static void cpkt_sqlite_destroy(cpkt_sqlite *self) {
  cpkt_sqlite_state *state;
  cpkt_sqlite_rtree_geometry_binding *binding;
  cpkt_sqlite_rtree_geometry_binding *next;
  cpkt_sqlite_state **state_link;
  pthread_mutex_t *mutex;
  if (self == NULL)
    return;
  state = cpkt_sqlite_state_for(self);
  if (state != NULL) {
    if (state->listed_wrapper) {
      mutex = cpkt_sqlite_global_mutex();
      cpkt_sqlite_global_lock(mutex);
      state_link = &cpkt_sqlite_wrapper_head;
      while (*state_link != NULL && *state_link != state) {
        state_link = &(*state_link)->next_wrapper;
      }
      if (*state_link == state)
        *state_link = state->next_wrapper;
      state->next_wrapper = NULL;
      state->listed_wrapper = 0;
      cpkt_sqlite_global_unlock(mutex);
    }
    free(state->unlock_notify_binding);
    state->unlock_notify_binding = NULL;
  }
  self->database = NULL;
  if (state != NULL) {
    binding = state->rtree_geometry_bindings;
    while (binding != NULL) {
      next = binding->next;
      free(binding);
      binding = next;
    }
    state->rtree_geometry_bindings = NULL;
  }
  free(self->facade_state);
  self->facade_state = NULL;
  free(self);
}

static void cpkt_sqlite_open_wrapper_cleanup(void *context) {
  cpkt_sqlite *self;
  cpkt_sqlite_state *state;
  self = (cpkt_sqlite *)context;
  state = cpkt_sqlite_state_for(self);
  if (state != NULL && state->open_pending)
    cpkt_sqlite_destroy(self);
}

void cpkt_sqlite_close(cpkt_sqlite *self) {
  cpkt_sqlite_state *state;
  pthread_mutex_t *mutex;
  int finish;
  if (self == NULL)
    return;
  finish = 0;
  mutex = cpkt_sqlite_global_mutex();
  cpkt_sqlite_global_lock(mutex);
  state = cpkt_sqlite_state_for(self);
  if (state != NULL && !state->closing) {
    state->closing = 1;
    if (state->child_count == 0) {
      state->finish_started = 1;
      finish = 1;
    }
  }
  cpkt_sqlite_global_unlock(mutex);
  if (finish)
    cpkt_sqlite_finish_close(self);
}

int cpkt_sqlite_close_strict(cpkt_sqlite *self) {
  cpkt_sqlite_state *state;
  pthread_mutex_t *mutex;
  int status;
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return CPKT_SQLITE_MISUSE;
  mutex = cpkt_sqlite_global_mutex();
  cpkt_sqlite_global_lock(mutex);
  state = cpkt_sqlite_state_for(self);
  if (state == NULL || state->closing) {
    cpkt_sqlite_global_unlock(mutex);
    return CPKT_SQLITE_MISUSE;
  }
  if (state->child_count != 0) {
    cpkt_sqlite_global_unlock(mutex);
    return CPKT_SQLITE_BUSY;
  }
  state->closing = 1;
  state->finish_started = 1;
  cpkt_sqlite_global_unlock(mutex);
  status = sqlite3_close(cpkt_sqlite_native(self));
  if (status != SQLITE_OK) {
    cpkt_sqlite_global_lock(mutex);
    state->closing = 0;
    state->finish_started = 0;
    cpkt_sqlite_global_unlock(mutex);
    return status;
  }
  cpkt_sqlite_destroy(self);
  return CPKT_SQLITE_OK;
}

static void cpkt_sqlite_finish_close(cpkt_sqlite *self) {
  if (self == NULL)
    return;
  if (cpkt_sqlite_native(self) != NULL) {
    sqlite3_unlock_notify(cpkt_sqlite_native(self), NULL, NULL);
    (void)sqlite3_close_v2(cpkt_sqlite_native(self));
  }
  cpkt_sqlite_destroy(self);
}

cpkt_sqlite_i64 cpkt_sqlite_i64_make(unsigned long high, unsigned long low) {
  cpkt_sqlite_i64 value;
  value.high = high & CPKT_SQLITE_WORD_MASK;
  value.low = low & CPKT_SQLITE_WORD_MASK;
  return value;
}

static cpkt_sqlite *cpkt_sqlite_wrap_database(sqlite3 *database,
                                              int close_on_failure) {
  cpkt_sqlite *self;
  cpkt_sqlite_state *state;
  pthread_mutex_t *mutex;
  if (database == NULL)
    return NULL;
  mutex = cpkt_sqlite_global_mutex();
  cpkt_sqlite_global_lock(mutex);
  state = cpkt_sqlite_wrapper_head;
  while (state != NULL) {
    if (state->database != NULL && state->database->database == database) {
      self = state->database;
      cpkt_sqlite_global_unlock(mutex);
      return self;
    }
    state = state->next_wrapper;
  }
  cpkt_sqlite_global_unlock(mutex);
  self = (cpkt_sqlite *)calloc(1, sizeof(*self));
  if (self == NULL) {
    if (close_on_failure)
      sqlite3_close_v2(database);
    return NULL;
  }
  self->facade_state = calloc(1, sizeof(cpkt_sqlite_state));
  if (self->facade_state == NULL) {
    if (close_on_failure)
      sqlite3_close_v2(database);
    free(self);
    return NULL;
  }
  state = (cpkt_sqlite_state *)self->facade_state;
  state->database = self;
  self->tx = cpkt_sqlite_exec;
  self->prepare = cpkt_sqlite_prepare;
  self->prepare16 = cpkt_sqlite_prepare16;
  self->busy_timeout = cpkt_sqlite_busy_timeout;
  self->extended_result_codes = cpkt_sqlite_extended_result_codes;
  self->wal_auto_checkpoint = cpkt_sqlite_wal_auto_checkpoint;
  self->wal_checkpoint = cpkt_sqlite_wal_checkpoint;
  self->load_extension = cpkt_sqlite_load_extension;
  self->enable_extension_loading = cpkt_sqlite_enable_extension_loading;
  self->unlock_notify = cpkt_sqlite_unlock_notify;
  self->overload_function = cpkt_sqlite_overload_function;
  self->changes = cpkt_sqlite_changes;
  self->last_insert_rowid = cpkt_sqlite_last_insert_rowid;
  self->preupdate_blob_write = cpkt_sqlite_preupdate_blob_write;
  self->error = cpkt_sqlite_error;
  self->error_code = cpkt_sqlite_error_code;
  self->set_error = cpkt_sqlite_set_error;
  self->close = cpkt_sqlite_close;
  self->database = database;
  cpkt_sqlite_global_lock(mutex);
  state->next_wrapper = cpkt_sqlite_wrapper_head;
  cpkt_sqlite_wrapper_head = state;
  state->listed_wrapper = 1;
  cpkt_sqlite_global_unlock(mutex);
  if (!close_on_failure) {
    /* SQLite destroys the native handle before returning NULL on some open
     * failures. Its client-data destructor then releases this receiver. */
    state->open_pending = 1;
    if (sqlite3_set_clientdata(database, cpkt_sqlite_open_clientdata_key, self,
                               cpkt_sqlite_open_wrapper_cleanup) != SQLITE_OK)
      return NULL;
  }
  return self;
}

static cpkt_sqlite *cpkt_sqlite_complete_open(sqlite3 *database) {
  cpkt_sqlite *self;
  cpkt_sqlite_state *state;
  self = cpkt_sqlite_wrap_database(database, 1);
  if (self == NULL)
    return NULL;
  state = cpkt_sqlite_state_for(self);
  if (state != NULL && state->open_pending) {
    state->open_pending = 0;
    (void)sqlite3_set_clientdata(database, cpkt_sqlite_open_clientdata_key,
                                 NULL, NULL);
  }
  return self;
}

cpkt_sqlite *cpkt_sqlite_open(const char *filename, int flags,
                              const char *vfs) {
  sqlite3 *database;
  if (filename == NULL)
    return NULL;
  database = NULL;
  (void)sqlite3_open_v2(filename, &database, flags, vfs);
  return cpkt_sqlite_complete_open(database);
}

cpkt_sqlite *cpkt_sqlite_open16(const void *filename) {
  sqlite3 *database;
  if (filename == NULL)
    return NULL;
  database = NULL;
  (void)sqlite3_open16(filename, &database);
  return cpkt_sqlite_complete_open(database);
}

cpkt_sqlite_auto_extension *
cpkt_sqlite_auto_extension_new(cpkt_sqlite_auto_extension_callback callback,
                               void *context) {
  cpkt_sqlite_auto_extension *public_extension;
  cpkt_sqlite_auto_extension_binding *binding;
  pthread_mutex_t *mutex;
  if (callback == NULL)
    return NULL;
  public_extension =
      (cpkt_sqlite_auto_extension *)calloc(1, sizeof(*public_extension));
  binding = (cpkt_sqlite_auto_extension_binding *)calloc(1, sizeof(*binding));
  if (public_extension == NULL || binding == NULL) {
    free(public_extension);
    free(binding);
    return NULL;
  }
  public_extension->register_extension = cpkt_sqlite_auto_extension_register;
  public_extension->cancel = cpkt_sqlite_auto_extension_cancel;
  public_extension->close = cpkt_sqlite_auto_extension_close;
  public_extension->state = context;
  public_extension->internal = binding;
  binding->public_extension = public_extension;
  binding->callback = callback;
  binding->context = context;
  mutex = cpkt_sqlite_global_mutex();
  cpkt_sqlite_global_lock(mutex);
  binding->next = cpkt_sqlite_auto_extension_head;
  cpkt_sqlite_auto_extension_head = binding;
  cpkt_sqlite_global_unlock(mutex);
  return public_extension;
}

static void cpkt_sqlite_auto_extension_registration_clear(void) {
  cpkt_sqlite_auto_extension_binding *binding;
  cpkt_sqlite_auto_extension_trampoline_registered = 0;
  binding = cpkt_sqlite_auto_extension_head;
  while (binding != NULL) {
    binding->registered = 0;
    binding = binding->next;
  }
}

void cpkt_sqlite_auto_extension_reset(void) {
  pthread_mutex_t *mutex;
  cpkt_sqlite_global_lock(&cpkt_sqlite_auto_extension_transition_mutex);
  sqlite3_reset_auto_extension();
  mutex = cpkt_sqlite_global_mutex();
  cpkt_sqlite_global_lock(mutex);
  cpkt_sqlite_auto_extension_registration_clear();
  cpkt_sqlite_global_unlock(mutex);
  cpkt_sqlite_global_unlock(&cpkt_sqlite_auto_extension_transition_mutex);
}

cpkt_sqlite_vfs *cpkt_sqlite_vfs_new(const char *name,
                                     int maximum_pathname_bytes, void *state,
                                     const cpkt_sqlite_vfs_methods *methods) {
  cpkt_sqlite_vfs *public_vfs;
  cpkt_sqlite_vfs_binding *binding;
  if (name == NULL || name[0] == '\0' || maximum_pathname_bytes <= 0 ||
      methods == NULL || methods->version < 1 || methods->version > 3 ||
      methods->open == NULL || methods->delete_file == NULL ||
      methods->access == NULL || methods->full_path == NULL ||
      methods->randomness == NULL || methods->sleep == NULL ||
      methods->current_time == NULL)
    return NULL;
  public_vfs = (cpkt_sqlite_vfs *)calloc(1, sizeof(*public_vfs));
  binding = (cpkt_sqlite_vfs_binding *)calloc(1, sizeof(*binding));
  if (public_vfs == NULL || binding == NULL) {
    free(public_vfs);
    free(binding);
    return NULL;
  }
  public_vfs->register_vfs = cpkt_sqlite_vfs_register;
  public_vfs->unregister_vfs = cpkt_sqlite_vfs_unregister;
  public_vfs->close = cpkt_sqlite_vfs_close;
  public_vfs->name = name;
  public_vfs->maximum_pathname_bytes = maximum_pathname_bytes;
  public_vfs->state = state;
  public_vfs->methods = *methods;
  public_vfs->internal = binding;
  binding->public_vfs = public_vfs;
  binding->native.iVersion = methods->version;
  binding->native.szOsFile = (int)sizeof(cpkt_sqlite_vfs_file);
  binding->native.mxPathname = maximum_pathname_bytes;
  binding->native.zName = name;
  binding->native.pAppData = state;
  binding->native.xOpen = cpkt_sqlite_vfs_open_native;
  binding->native.xDelete = cpkt_sqlite_vfs_delete_native;
  binding->native.xAccess = cpkt_sqlite_vfs_access_native;
  binding->native.xFullPathname = cpkt_sqlite_vfs_full_path_native;
  binding->native.xDlOpen = cpkt_sqlite_vfs_dl_open_native;
  binding->native.xDlError = cpkt_sqlite_vfs_dl_error_native;
  binding->native.xDlSym = cpkt_sqlite_vfs_dl_symbol_native;
  binding->native.xDlClose = cpkt_sqlite_vfs_dl_close_native;
  binding->native.xRandomness = cpkt_sqlite_vfs_randomness_native;
  binding->native.xSleep = cpkt_sqlite_vfs_sleep_native;
  binding->native.xCurrentTime = cpkt_sqlite_vfs_current_time_native;
  binding->native.xGetLastError = cpkt_sqlite_vfs_last_error_native;
  binding->native.xCurrentTimeInt64 =
      methods->version >= 2 && methods->current_time_i64 != NULL
          ? cpkt_sqlite_vfs_current_time_i64_native
          : NULL;
  binding->native.xSetSystemCall =
      methods->version >= 3 && methods->set_system_call != NULL
          ? cpkt_sqlite_vfs_set_system_call_native
          : NULL;
  binding->native.xGetSystemCall =
      methods->version >= 3 && methods->get_system_call != NULL
          ? cpkt_sqlite_vfs_get_system_call_native
          : NULL;
  binding->native.xNextSystemCall =
      methods->version >= 3 && methods->next_system_call != NULL
          ? cpkt_sqlite_vfs_next_system_call_native
          : NULL;
  {
    pthread_mutex_t *mutex;
    mutex = cpkt_sqlite_global_mutex();
    cpkt_sqlite_global_lock(mutex);
    binding->next = cpkt_sqlite_vfs_head;
    cpkt_sqlite_vfs_head = binding;
    cpkt_sqlite_global_unlock(mutex);
  }
  return public_vfs;
}

cpkt_sqlite_vfs *cpkt_sqlite_vfs_find(const char *name) {
  cpkt_sqlite_vfs_binding *binding;
  sqlite3_vfs *native_vfs;
  pthread_mutex_t *mutex;
  native_vfs = sqlite3_vfs_find(name);
  if (native_vfs == NULL)
    return NULL;
  mutex = cpkt_sqlite_global_mutex();
  cpkt_sqlite_global_lock(mutex);
  binding = cpkt_sqlite_vfs_head;
  while (binding != NULL) {
    if (&binding->native == native_vfs) {
      cpkt_sqlite_global_unlock(mutex);
      return binding->public_vfs;
    }
    binding = binding->next;
  }
  cpkt_sqlite_global_unlock(mutex);
  return cpkt_sqlite_native_vfs_view(native_vfs);
}

cpkt_sqlite_file *cpkt_sqlite_vfs_database_file_object(const char *name) {
  sqlite3_file *native;
  if (name == NULL)
    return NULL;
  native = sqlite3_database_file_object(name);
  if (native == NULL || native->pMethods == NULL)
    return NULL;
  if (native->pMethods->xClose == cpkt_sqlite_vfs_file_close)
    return &cpkt_sqlite_vfs_file_from_native(native)->public_file;
  return cpkt_sqlite_native_file_metadata_view(native);
}

int cpkt_sqlite_vfs_register(cpkt_sqlite_vfs *self, int make_default) {
  cpkt_sqlite_vfs_binding *binding;
  int status;
  if (self == NULL || self->internal == NULL)
    return SQLITE_MISUSE;
  if (cpkt_sqlite_native_vfs_view_is(self))
    return cpkt_sqlite_native_vfs_view_register(self, make_default);
  binding = (cpkt_sqlite_vfs_binding *)self->internal;
  status = sqlite3_vfs_register(&binding->native, make_default);
  if (status == SQLITE_OK)
    binding->registered = 1;
  return status;
}

int cpkt_sqlite_vfs_unregister(cpkt_sqlite_vfs *self) {
  cpkt_sqlite_vfs_binding *binding;
  int status;
  if (self == NULL || self->internal == NULL)
    return SQLITE_MISUSE;
  if (cpkt_sqlite_native_vfs_view_is(self))
    return cpkt_sqlite_native_vfs_view_unregister(self);
  binding = (cpkt_sqlite_vfs_binding *)self->internal;
  status = sqlite3_vfs_unregister(&binding->native);
  if (status == SQLITE_OK)
    binding->registered = 0;
  return status;
}

void cpkt_sqlite_vfs_close(cpkt_sqlite_vfs *self) {
  cpkt_sqlite_vfs_binding *binding;
  cpkt_sqlite_vfs_binding **link;
  pthread_mutex_t *mutex;
  if (self == NULL)
    return;
  if (cpkt_sqlite_native_vfs_view_is(self)) {
    cpkt_sqlite_native_vfs_view_close(self);
    return;
  }
  binding = (cpkt_sqlite_vfs_binding *)self->internal;
  if (binding != NULL && binding->registered) {
    (void)sqlite3_vfs_unregister(&binding->native);
  }
  if (binding != NULL) {
    mutex = cpkt_sqlite_global_mutex();
    cpkt_sqlite_global_lock(mutex);
    link = &cpkt_sqlite_vfs_head;
    while (*link != NULL && *link != binding)
      link = &(*link)->next;
    if (*link == binding)
      *link = binding->next;
    binding->next = NULL;
    cpkt_sqlite_global_unlock(mutex);
  }
  free(binding);
  self->internal = NULL;
  free(self);
}

cpkt_sqlite *cpkt_sqlite_new(const char *filename) {
  return cpkt_sqlite_open(
      filename, CPKT_SQLITE_OPEN_READWRITE | CPKT_SQLITE_OPEN_CREATE, NULL);
}

cpkt_sqlite_u64 cpkt_sqlite_u64_make(unsigned long high, unsigned long low) {
  cpkt_sqlite_u64 value;
  value.high = high & CPKT_SQLITE_WORD_MASK;
  value.low = low & CPKT_SQLITE_WORD_MASK;
  return value;
}

int cpkt_sqlite_i64_compare(cpkt_sqlite_i64 left, cpkt_sqlite_i64 right) {
  unsigned long left_high;
  unsigned long right_high;
  left_high = left.high ^ 0x80000000UL;
  right_high = right.high ^ 0x80000000UL;
  if (left_high < right_high)
    return -1;
  if (left_high > right_high)
    return 1;
  if (left.low < right.low)
    return -1;
  if (left.low > right.low)
    return 1;
  return 0;
}

int cpkt_sqlite_u64_compare(cpkt_sqlite_u64 left, cpkt_sqlite_u64 right) {
  if (left.high < right.high)
    return -1;
  if (left.high > right.high)
    return 1;
  if (left.low < right.low)
    return -1;
  if (left.low > right.low)
    return 1;
  return 0;
}

const char *cpkt_sqlite_library_version(void) { return sqlite3_libversion(); }
const char *cpkt_sqlite_temp_directory_get(void) {
  return sqlite3_temp_directory;
}
int cpkt_sqlite_temp_directory_set(const char *directory) {
  char *replacement;
  char *previous;
  replacement = NULL;
  if (directory != NULL) {
    replacement = sqlite3_mprintf("%s", directory);
    if (replacement == NULL)
      return SQLITE_NOMEM;
  }
  previous = sqlite3_temp_directory;
  sqlite3_temp_directory = replacement;
  sqlite3_free(previous);
  return SQLITE_OK;
}
const char *cpkt_sqlite_source_id(void) { return sqlite3_sourceid(); }
int cpkt_sqlite_library_version_number(void) {
  return sqlite3_libversion_number();
}
int cpkt_sqlite_compile_option_used(const char *name) {
  return sqlite3_compileoption_used(name);
}
const char *cpkt_sqlite_compile_option(int index) {
  return sqlite3_compileoption_get(index);
}
int cpkt_sqlite_threadsafe(void) { return sqlite3_threadsafe(); }
int cpkt_sqlite_initialize(void) { return sqlite3_initialize(); }
int cpkt_sqlite_shutdown(void) {
  int status;
  status = sqlite3_shutdown();
  if (status == SQLITE_OK) {
    /* SQLite has cleared its native registry and torn down its mutexes.
     * Shutdown itself requires exclusive access, so no lock is available or
     * needed while synchronizing the facade's registration state. */
    cpkt_sqlite_auto_extension_registration_clear();
  }
  return status;
}
int cpkt_sqlite_os_initialize(void) { return sqlite3_os_init(); }
int cpkt_sqlite_os_shutdown(void) { return sqlite3_os_end(); }
int cpkt_sqlite_global_recover(void) { return sqlite3_global_recover(); }
void cpkt_sqlite_thread_cleanup(void) { sqlite3_thread_cleanup(); }
int cpkt_sqlite_sleep(int milliseconds) { return sqlite3_sleep(milliseconds); }
int cpkt_sqlite_global_config_none(int operation) {
  if (operation != SQLITE_CONFIG_SINGLETHREAD &&
      operation != SQLITE_CONFIG_MULTITHREAD &&
      operation != SQLITE_CONFIG_SERIALIZED)
    return SQLITE_MISUSE;
  return sqlite3_config(operation);
}
int cpkt_sqlite_global_config_int(int operation, int value) {
  if (operation != SQLITE_CONFIG_MEMSTATUS &&
      operation != SQLITE_CONFIG_SMALL_MALLOC &&
      operation != SQLITE_CONFIG_URI &&
      operation != SQLITE_CONFIG_COVERING_INDEX_SCAN &&
      operation != SQLITE_CONFIG_STMTJRNL_SPILL)
    return SQLITE_MISUSE;
  return sqlite3_config(operation, value);
}
int cpkt_sqlite_global_config_two_int(int operation, int first, int second) {
  if (operation != SQLITE_CONFIG_LOOKASIDE)
    return SQLITE_MISUSE;
  return sqlite3_config(operation, first, second);
}
int cpkt_sqlite_global_config_pointer(int operation, void *value) {
  (void)operation;
  (void)value;
  return SQLITE_MISUSE;
}
int cpkt_sqlite_global_config_pointer_int_int(int operation, void *buffer,
                                              int first, int second) {
  if (operation != SQLITE_CONFIG_PAGECACHE)
    return SQLITE_MISUSE;
  return sqlite3_config(operation, buffer, first, second);
}
int cpkt_sqlite_global_config_i64(int operation, cpkt_sqlite_i64 value) {
  if (operation != SQLITE_CONFIG_MEMDB_MAXSIZE)
    return SQLITE_MISUSE;
  return sqlite3_config(operation, cpkt_sqlite_native_i64(value));
}
int cpkt_sqlite_global_config_two_i64(int operation, cpkt_sqlite_i64 first,
                                      cpkt_sqlite_i64 second) {
  if (operation != SQLITE_CONFIG_MMAP_SIZE)
    return SQLITE_MISUSE;
  return sqlite3_config(operation, cpkt_sqlite_native_i64(first),
                        cpkt_sqlite_native_i64(second));
}
int cpkt_sqlite_global_config_unsigned_int(int operation, unsigned long value) {
  if (operation != SQLITE_CONFIG_PMASZ)
    return SQLITE_MISUSE;
  if (sizeof(unsigned long) > 4 && value > 0xffffffffUL)
    return SQLITE_RANGE;
  return sqlite3_config(operation, (unsigned int)value);
}
int cpkt_sqlite_global_config_int_out(int operation, int *value_out) {
  if (operation != SQLITE_CONFIG_PCACHE_HDRSZ &&
      operation != SQLITE_CONFIG_ROWID_IN_VIEW)
    return SQLITE_MISUSE;
  if (value_out == NULL)
    return SQLITE_MISUSE;
  return sqlite3_config(operation, value_out);
}
int cpkt_sqlite_global_config_log(cpkt_sqlite_log_callback callback,
                                  void *context) {
  return sqlite3_config(SQLITE_CONFIG_LOG, callback, context);
}

/* The private entry is compiled in the pinned SQLite amalgamation. */
extern void sqlite3__cpkt_log_v(int, const char *, va_list);
extern void sqlite3__cpkt_log(int, const char *, ...);

void cpkt_sqlite_log(int error_code, const char *format, ...) {
  va_list arguments;
  if (format == NULL)
    return;
  va_start(arguments, format);
  sqlite3__cpkt_log_v(error_code, format, arguments);
  va_end(arguments);
}

void cpkt_sqlite_log_i64(int error_code, const char *format,
                         cpkt_sqlite_i64 value) {
  if (format == NULL)
    return;
  sqlite3__cpkt_log(error_code, format, cpkt_sqlite_native_i64(value));
}

void cpkt_sqlite_log_u64(int error_code, const char *format,
                         cpkt_sqlite_u64 value) {
  if (format == NULL)
    return;
  sqlite3__cpkt_log(error_code, format, cpkt_sqlite_native_u64(value));
}
int cpkt_sqlite_global_config_memory_methods_set(
    const cpkt_sqlite_memory_methods *methods) {
  sqlite3_mem_methods native_methods;
  if (methods == NULL || methods->allocate == NULL || methods->free == NULL ||
      methods->reallocate == NULL || methods->size == NULL ||
      methods->roundup == NULL || methods->initialize == NULL)
    return SQLITE_MISUSE;
  native_methods.xMalloc = methods->allocate;
  native_methods.xFree = methods->free;
  native_methods.xRealloc = methods->reallocate;
  native_methods.xSize = methods->size;
  native_methods.xRoundup = methods->roundup;
  native_methods.xInit = methods->initialize;
  native_methods.xShutdown = methods->shutdown;
  native_methods.pAppData = methods->context;
  return sqlite3_config(SQLITE_CONFIG_MALLOC, &native_methods);
}
int cpkt_sqlite_global_config_memory_methods_get(
    cpkt_sqlite_memory_methods *methods_out) {
  sqlite3_mem_methods native_methods;
  int status;
  if (methods_out == NULL)
    return SQLITE_MISUSE;
  native_methods.xMalloc = NULL;
  native_methods.xFree = NULL;
  native_methods.xRealloc = NULL;
  native_methods.xSize = NULL;
  native_methods.xRoundup = NULL;
  native_methods.xInit = NULL;
  native_methods.xShutdown = NULL;
  native_methods.pAppData = NULL;
  status = sqlite3_config(SQLITE_CONFIG_GETMALLOC, &native_methods);
  if (status != SQLITE_OK)
    return status;
  methods_out->allocate = native_methods.xMalloc;
  methods_out->free = native_methods.xFree;
  methods_out->reallocate = native_methods.xRealloc;
  methods_out->size = native_methods.xSize;
  methods_out->roundup = native_methods.xRoundup;
  methods_out->initialize = native_methods.xInit;
  methods_out->shutdown = native_methods.xShutdown;
  methods_out->context = native_methods.pAppData;
  return SQLITE_OK;
}

typedef struct cpkt_sqlite_mutex_adapter {
  void *state;
  int type;
  struct cpkt_sqlite_mutex_adapter *next;
} cpkt_sqlite_mutex_adapter;

static cpkt_sqlite_mutex_methods cpkt_sqlite_mutex_methods_current;
static int cpkt_sqlite_mutex_methods_are_set;
static cpkt_sqlite_mutex_adapter *cpkt_sqlite_static_mutexes;
static pthread_mutex_t cpkt_sqlite_static_mutex_lock =
    PTHREAD_MUTEX_INITIALIZER;

static int cpkt_sqlite_custom_mutex_init(void) {
  if (cpkt_sqlite_mutex_methods_current.initialize == NULL)
    return SQLITE_OK;
  return cpkt_sqlite_mutex_methods_current.initialize(
      cpkt_sqlite_mutex_methods_current.context);
}

static int cpkt_sqlite_custom_mutex_end(void) {
  cpkt_sqlite_mutex_adapter *item;
  cpkt_sqlite_mutex_adapter *next;
  int status;
  status = cpkt_sqlite_mutex_methods_current.shutdown == NULL
               ? SQLITE_OK
               : cpkt_sqlite_mutex_methods_current.shutdown(
                     cpkt_sqlite_mutex_methods_current.context);
  pthread_mutex_lock(&cpkt_sqlite_static_mutex_lock);
  item = cpkt_sqlite_static_mutexes;
  cpkt_sqlite_static_mutexes = NULL;
  pthread_mutex_unlock(&cpkt_sqlite_static_mutex_lock);
  while (item != NULL) {
    next = item->next;
    free(item);
    item = next;
  }
  return status;
}

static sqlite3_mutex *cpkt_sqlite_custom_mutex_alloc(int type) {
  cpkt_sqlite_mutex_adapter *item;
  void *state;
  if (type >= SQLITE_MUTEX_STATIC_MAIN) {
    pthread_mutex_lock(&cpkt_sqlite_static_mutex_lock);
    for (item = cpkt_sqlite_static_mutexes; item != NULL; item = item->next) {
      if (item->type == type) {
        pthread_mutex_unlock(&cpkt_sqlite_static_mutex_lock);
        return (sqlite3_mutex *)item;
      }
    }
  }
  item = (cpkt_sqlite_mutex_adapter *)malloc(sizeof(*item));
  if (item == NULL) {
    if (type >= SQLITE_MUTEX_STATIC_MAIN)
      pthread_mutex_unlock(&cpkt_sqlite_static_mutex_lock);
    return NULL;
  }
  state = cpkt_sqlite_mutex_methods_current.allocate(
      cpkt_sqlite_mutex_methods_current.context, type);
  if (state == NULL) {
    free(item);
    if (type >= SQLITE_MUTEX_STATIC_MAIN)
      pthread_mutex_unlock(&cpkt_sqlite_static_mutex_lock);
    return NULL;
  }
  item->state = state;
  item->type = type;
  item->next = NULL;
  if (type >= SQLITE_MUTEX_STATIC_MAIN) {
    item->next = cpkt_sqlite_static_mutexes;
    cpkt_sqlite_static_mutexes = item;
    pthread_mutex_unlock(&cpkt_sqlite_static_mutex_lock);
  }
  return (sqlite3_mutex *)item;
}

static void cpkt_sqlite_custom_mutex_free(sqlite3_mutex *mutex) {
  cpkt_sqlite_mutex_adapter *item;
  item = (cpkt_sqlite_mutex_adapter *)mutex;
  if (item == NULL)
    return;
  if (item->type >= SQLITE_MUTEX_STATIC_MAIN)
    return;
  cpkt_sqlite_mutex_methods_current.free(
      cpkt_sqlite_mutex_methods_current.context, item->state);
  free(item);
}

static void cpkt_sqlite_custom_mutex_enter(sqlite3_mutex *mutex) {
  cpkt_sqlite_mutex_adapter *item;
  item = (cpkt_sqlite_mutex_adapter *)mutex;
  cpkt_sqlite_mutex_methods_current.enter(
      cpkt_sqlite_mutex_methods_current.context, item->state);
}

static int cpkt_sqlite_custom_mutex_try(sqlite3_mutex *mutex) {
  cpkt_sqlite_mutex_adapter *item;
  item = (cpkt_sqlite_mutex_adapter *)mutex;
  return cpkt_sqlite_mutex_methods_current.try_enter(
      cpkt_sqlite_mutex_methods_current.context, item->state);
}

static void cpkt_sqlite_custom_mutex_leave(sqlite3_mutex *mutex) {
  cpkt_sqlite_mutex_adapter *item;
  item = (cpkt_sqlite_mutex_adapter *)mutex;
  cpkt_sqlite_mutex_methods_current.leave(
      cpkt_sqlite_mutex_methods_current.context, item->state);
}

static int cpkt_sqlite_custom_mutex_held(sqlite3_mutex *mutex) {
  cpkt_sqlite_mutex_adapter *item;
  item = (cpkt_sqlite_mutex_adapter *)mutex;
  return cpkt_sqlite_mutex_methods_current.held(
      cpkt_sqlite_mutex_methods_current.context, item->state);
}

static int cpkt_sqlite_custom_mutex_not_held(sqlite3_mutex *mutex) {
  cpkt_sqlite_mutex_adapter *item;
  item = (cpkt_sqlite_mutex_adapter *)mutex;
  return cpkt_sqlite_mutex_methods_current.not_held(
      cpkt_sqlite_mutex_methods_current.context, item->state);
}

int cpkt_sqlite_global_config_mutex_methods_set(
    const cpkt_sqlite_mutex_methods *methods) {
  sqlite3_mutex_methods native;
  int status;
  if (cpkt_sqlite_native_mutex_methods_unwrap(methods, &native)) {
    status = sqlite3_config(SQLITE_CONFIG_MUTEX, &native);
    if (status == SQLITE_OK)
      cpkt_sqlite_mutex_methods_are_set = 0;
    return status;
  }
  if (methods == NULL || methods->allocate == NULL || methods->free == NULL ||
      methods->enter == NULL || methods->try_enter == NULL ||
      methods->leave == NULL)
    return SQLITE_MISUSE;
  native.xMutexInit = cpkt_sqlite_custom_mutex_init;
  native.xMutexEnd = cpkt_sqlite_custom_mutex_end;
  native.xMutexAlloc = cpkt_sqlite_custom_mutex_alloc;
  native.xMutexFree = cpkt_sqlite_custom_mutex_free;
  native.xMutexEnter = cpkt_sqlite_custom_mutex_enter;
  native.xMutexTry = cpkt_sqlite_custom_mutex_try;
  native.xMutexLeave = cpkt_sqlite_custom_mutex_leave;
  native.xMutexHeld =
      methods->held == NULL ? NULL : cpkt_sqlite_custom_mutex_held;
  native.xMutexNotheld =
      methods->not_held == NULL ? NULL : cpkt_sqlite_custom_mutex_not_held;
  status = sqlite3_config(SQLITE_CONFIG_MUTEX, &native);
  if (status == SQLITE_OK) {
    cpkt_sqlite_mutex_methods_current = *methods;
    cpkt_sqlite_mutex_methods_are_set = 1;
  }
  return status;
}

int cpkt_sqlite_global_config_mutex_methods_get(
    cpkt_sqlite_mutex_methods *methods_out) {
  sqlite3_mutex_methods native;
  int status;
  if (methods_out == NULL)
    return SQLITE_MISUSE;
  status = sqlite3_config(SQLITE_CONFIG_GETMUTEX, &native);
  if (status != SQLITE_OK)
    return status;
  if (cpkt_sqlite_mutex_methods_are_set &&
      native.xMutexAlloc == cpkt_sqlite_custom_mutex_alloc) {
    *methods_out = cpkt_sqlite_mutex_methods_current;
    return SQLITE_OK;
  }
  return cpkt_sqlite_native_mutex_methods_get(&native, methods_out);
}

void cpkt_sqlite_mutex_methods_release(cpkt_sqlite_mutex_methods *methods) {
  cpkt_sqlite_native_mutex_methods_release(methods);
}

static int cpkt_sqlite_page_cache_initialize(void *context) {
  cpkt_sqlite_page_cache_methods *methods;
  methods = (cpkt_sqlite_page_cache_methods *)context;
  if (methods == NULL || methods->initialize == NULL)
    return SQLITE_OK;
  return methods->initialize(methods->context);
}

static void cpkt_sqlite_page_cache_shutdown(void *context) {
  cpkt_sqlite_page_cache_methods *methods;
  methods = (cpkt_sqlite_page_cache_methods *)context;
  if (methods != NULL && methods->shutdown != NULL)
    methods->shutdown(methods->context);
}

static sqlite3_pcache *cpkt_sqlite_page_cache_create(int page_byte_count,
                                                     int extra_byte_count,
                                                     int purgeable) {
  cpkt_sqlite_page_cache_binding *binding;
  cpkt_sqlite_page_cache *cache;
  size_t adapter_bytes;
  if (!cpkt_sqlite_page_cache_methods_are_set ||
      cpkt_sqlite_page_cache_methods_current.create == NULL)
    return NULL;
  /* The backend only promises pointer alignment, which can be four bytes.
   * Reserve enough space to align SQLite's pExtra to eight bytes. */
  adapter_bytes = sizeof(cpkt_sqlite_page_binding) + 7U;
  if (extra_byte_count < 0 || adapter_bytes > (size_t)INT_MAX ||
      (size_t)extra_byte_count > (size_t)INT_MAX - adapter_bytes)
    return NULL;
  cache = cpkt_sqlite_page_cache_methods_current.create(
      cpkt_sqlite_page_cache_methods_current.context, page_byte_count,
      extra_byte_count + (int)adapter_bytes, purgeable);
  if (cache == NULL)
    return NULL;
  binding = (cpkt_sqlite_page_cache_binding *)calloc(1, sizeof(*binding));
  if (binding == NULL) {
    if (cpkt_sqlite_page_cache_methods_current.destroy != NULL) {
      cpkt_sqlite_page_cache_methods_current.destroy(cache);
    }
    return NULL;
  }
  binding->cache = cache;
  binding->methods = &cpkt_sqlite_page_cache_methods_current;
  return (sqlite3_pcache *)binding;
}

static cpkt_sqlite_page_cache_binding *
cpkt_sqlite_page_cache_native_binding(sqlite3_pcache *cache) {
  return (cpkt_sqlite_page_cache_binding *)cache;
}

static void cpkt_sqlite_page_cache_cache_size(sqlite3_pcache *cache,
                                              int suggested_page_count) {
  cpkt_sqlite_page_cache_binding *binding;
  binding = cpkt_sqlite_page_cache_native_binding(cache);
  if (binding != NULL && binding->methods->cache_size != NULL) {
    binding->methods->cache_size(binding->cache, suggested_page_count);
  }
}

static int cpkt_sqlite_page_cache_page_count(sqlite3_pcache *cache) {
  cpkt_sqlite_page_cache_binding *binding;
  binding = cpkt_sqlite_page_cache_native_binding(cache);
  if (binding == NULL || binding->methods->page_count == NULL)
    return 0;
  return binding->methods->page_count(binding->cache);
}

static sqlite3_pcache_page *cpkt_sqlite_page_cache_fetch(sqlite3_pcache *cache,
                                                         unsigned int key,
                                                         int create_flag) {
  cpkt_sqlite_page_cache_binding *binding;
  cpkt_sqlite_page_binding *page_binding;
  cpkt_sqlite_page *page;
  char *native_extra;
  size_t padding;
  binding = cpkt_sqlite_page_cache_native_binding(cache);
  if (binding == NULL || binding->methods->fetch == NULL)
    return NULL;
  page =
      binding->methods->fetch(binding->cache, (unsigned long)key, create_flag);
  if (page == NULL)
    return NULL;
  /* The native wrapper must have the same lifetime as the backend page.
   * A backend may silently evict an unpinned page, so a separate allocation
   * cannot be released reliably. The prefix is private to this adapter. */
  page_binding = (cpkt_sqlite_page_binding *)page->extra;
  page_binding->native_page.pBuf = page->buffer;
  native_extra = (char *)page->extra + sizeof(*page_binding);
  padding = (8U - ((size_t)native_extra & 7U)) & 7U;
  page_binding->native_page.pExtra = native_extra + padding;
  page_binding->page = page;
  return &page_binding->native_page;
}

static void cpkt_sqlite_page_cache_unpin(sqlite3_pcache *cache,
                                         sqlite3_pcache_page *native_page,
                                         int discard) {
  cpkt_sqlite_page_cache_binding *binding;
  cpkt_sqlite_page_binding *page_binding;
  binding = cpkt_sqlite_page_cache_native_binding(cache);
  page_binding = (cpkt_sqlite_page_binding *)native_page;
  if (binding == NULL || page_binding == NULL)
    return;
  binding->methods->unpin(binding->cache, page_binding->page, discard);
}

static void cpkt_sqlite_page_cache_rekey(sqlite3_pcache *cache,
                                         sqlite3_pcache_page *native_page,
                                         unsigned int old_key,
                                         unsigned int new_key) {
  cpkt_sqlite_page_cache_binding *binding;
  cpkt_sqlite_page_binding *page_binding;
  binding = cpkt_sqlite_page_cache_native_binding(cache);
  page_binding = (cpkt_sqlite_page_binding *)native_page;
  if (binding == NULL || page_binding == NULL)
    return;
  if (binding->methods->rekey != NULL) {
    binding->methods->rekey(binding->cache, page_binding->page,
                            (unsigned long)old_key, (unsigned long)new_key);
  }
}

static void cpkt_sqlite_page_cache_truncate(sqlite3_pcache *cache,
                                            unsigned int limit) {
  cpkt_sqlite_page_cache_binding *binding;
  binding = cpkt_sqlite_page_cache_native_binding(cache);
  if (binding == NULL)
    return;
  if (binding->methods->truncate != NULL) {
    binding->methods->truncate(binding->cache, (unsigned long)limit);
  }
}

static void cpkt_sqlite_page_cache_destroy(sqlite3_pcache *cache) {
  cpkt_sqlite_page_cache_binding *binding;
  binding = cpkt_sqlite_page_cache_native_binding(cache);
  if (binding == NULL)
    return;
  if (binding->methods->destroy != NULL)
    binding->methods->destroy(binding->cache);
  free(binding);
}

static void cpkt_sqlite_page_cache_shrink(sqlite3_pcache *cache) {
  cpkt_sqlite_page_cache_binding *binding;
  binding = cpkt_sqlite_page_cache_native_binding(cache);
  if (binding != NULL && binding->methods->shrink != NULL) {
    binding->methods->shrink(binding->cache);
  }
}

int cpkt_sqlite_global_config_page_cache_methods_set(
    const cpkt_sqlite_page_cache_methods *methods) {
  sqlite3_pcache_methods2 native_methods;
  int status;
  if (cpkt_sqlite_native_page_cache_methods_unwrap(methods, &native_methods)) {
    status = sqlite3_config(SQLITE_CONFIG_PCACHE2, &native_methods);
    if (status == SQLITE_OK)
      cpkt_sqlite_page_cache_methods_are_set = 0;
    return status;
  }
  if (methods == NULL || methods->create == NULL ||
      methods->cache_size == NULL || methods->page_count == NULL ||
      methods->fetch == NULL || methods->unpin == NULL ||
      methods->rekey == NULL || methods->truncate == NULL ||
      methods->destroy == NULL)
    return SQLITE_MISUSE;
  native_methods.iVersion = 1;
  native_methods.pArg = &cpkt_sqlite_page_cache_methods_current;
  native_methods.xInit = cpkt_sqlite_page_cache_initialize;
  native_methods.xShutdown = cpkt_sqlite_page_cache_shutdown;
  native_methods.xCreate = cpkt_sqlite_page_cache_create;
  native_methods.xCachesize = cpkt_sqlite_page_cache_cache_size;
  native_methods.xPagecount = cpkt_sqlite_page_cache_page_count;
  native_methods.xFetch = cpkt_sqlite_page_cache_fetch;
  native_methods.xUnpin = cpkt_sqlite_page_cache_unpin;
  native_methods.xRekey = cpkt_sqlite_page_cache_rekey;
  native_methods.xTruncate = cpkt_sqlite_page_cache_truncate;
  native_methods.xDestroy = cpkt_sqlite_page_cache_destroy;
  native_methods.xShrink = cpkt_sqlite_page_cache_shrink;
  status = sqlite3_config(SQLITE_CONFIG_PCACHE2, &native_methods);
  if (status == SQLITE_OK) {
    cpkt_sqlite_page_cache_methods_current = *methods;
    cpkt_sqlite_page_cache_methods_are_set = 1;
  }
  return status;
}

int cpkt_sqlite_global_config_page_cache_methods_get(
    cpkt_sqlite_page_cache_methods *methods_out) {
  sqlite3_pcache_methods2 native_methods;
  int status;
  if (methods_out == NULL)
    return SQLITE_MISUSE;
  status = sqlite3_config(SQLITE_CONFIG_GETPCACHE2, &native_methods);
  if (status != SQLITE_OK)
    return status;
  if (cpkt_sqlite_page_cache_methods_are_set &&
      native_methods.pArg == &cpkt_sqlite_page_cache_methods_current &&
      native_methods.xCreate == cpkt_sqlite_page_cache_create) {
    *methods_out = cpkt_sqlite_page_cache_methods_current;
    return SQLITE_OK;
  }
  return cpkt_sqlite_native_page_cache_methods_get(&native_methods,
                                                   methods_out);
}

void cpkt_sqlite_page_cache_methods_release(
    cpkt_sqlite_page_cache_methods *methods) {
  cpkt_sqlite_native_page_cache_methods_release(methods);
}

static void cpkt_sqlite_mutex_enter(cpkt_sqlite_mutex *self) {
  sqlite3_mutex *native_mutex;
  native_mutex = cpkt_sqlite_native_mutex(self);
  if (native_mutex != NULL)
    sqlite3_mutex_enter(native_mutex);
}

static int cpkt_sqlite_mutex_try_enter(cpkt_sqlite_mutex *self) {
  sqlite3_mutex *native_mutex;
  native_mutex = cpkt_sqlite_native_mutex(self);
  if (native_mutex == NULL)
    return SQLITE_MISUSE;
  return sqlite3_mutex_try(native_mutex);
}

static void cpkt_sqlite_mutex_leave(cpkt_sqlite_mutex *self) {
  sqlite3_mutex *native_mutex;
  native_mutex = cpkt_sqlite_native_mutex(self);
  if (native_mutex != NULL)
    sqlite3_mutex_leave(native_mutex);
}

static int cpkt_sqlite_mutex_held(const cpkt_sqlite_mutex *self) {
  /*
   * SQLite only exports sqlite3_mutex_held() in an SQLITE_DEBUG build.
   * Its documented non-debug contract is a successful verification stub.
   * Keep the facade linkable against the production SQLite library rather
   * than coupling it to the facade compilation unit's unrelated NDEBUG flag.
   */
  (void)self;
  return 1;
}

static int cpkt_sqlite_mutex_not_held(const cpkt_sqlite_mutex *self) {
  /* See cpkt_sqlite_mutex_held(). */
  (void)self;
  return 1;
}

static void cpkt_sqlite_mutex_close(cpkt_sqlite_mutex *self) {
  if (self == NULL)
    return;
  if (self->dynamic && cpkt_sqlite_native_mutex(self) != NULL) {
    sqlite3_mutex_free(cpkt_sqlite_native_mutex(self));
  }
  self->mutex = NULL;
  free(self);
}

cpkt_sqlite_mutex *cpkt_sqlite_mutex_new(int mutex_type) {
  sqlite3_mutex *native_mutex;
  cpkt_sqlite_mutex *public_mutex;
  native_mutex = sqlite3_mutex_alloc(mutex_type);
  if (native_mutex == NULL)
    return NULL;
  public_mutex = (cpkt_sqlite_mutex *)calloc(1, sizeof(*public_mutex));
  if (public_mutex == NULL) {
    if (mutex_type == CPKT_SQLITE_MUTEX_FAST ||
        mutex_type == CPKT_SQLITE_MUTEX_RECURSIVE)
      sqlite3_mutex_free(native_mutex);
    return NULL;
  }
  public_mutex->enter = cpkt_sqlite_mutex_enter;
  public_mutex->try_enter = cpkt_sqlite_mutex_try_enter;
  public_mutex->leave = cpkt_sqlite_mutex_leave;
  public_mutex->held = cpkt_sqlite_mutex_held;
  public_mutex->not_held = cpkt_sqlite_mutex_not_held;
  public_mutex->close = cpkt_sqlite_mutex_close;
  public_mutex->mutex = native_mutex;
  public_mutex->dynamic = mutex_type == CPKT_SQLITE_MUTEX_FAST ||
                          mutex_type == CPKT_SQLITE_MUTEX_RECURSIVE;
  return public_mutex;
}

cpkt_sqlite_mutex *cpkt_sqlite_database_mutex(const cpkt_sqlite *database) {
  sqlite3_mutex *native_mutex;
  cpkt_sqlite_mutex *public_mutex;
  if (database == NULL || cpkt_sqlite_native(database) == NULL)
    return NULL;
  native_mutex = sqlite3_db_mutex(cpkt_sqlite_native(database));
  if (native_mutex == NULL)
    return NULL;
  public_mutex = (cpkt_sqlite_mutex *)calloc(1, sizeof(*public_mutex));
  if (public_mutex == NULL)
    return NULL;
  public_mutex->enter = cpkt_sqlite_mutex_enter;
  public_mutex->try_enter = cpkt_sqlite_mutex_try_enter;
  public_mutex->leave = cpkt_sqlite_mutex_leave;
  public_mutex->held = cpkt_sqlite_mutex_held;
  public_mutex->not_held = cpkt_sqlite_mutex_not_held;
  public_mutex->close = cpkt_sqlite_mutex_close;
  public_mutex->mutex = native_mutex;
  public_mutex->dynamic = 0;
  return public_mutex;
}

int cpkt_sqlite_complete(const char *sql) { return sqlite3_complete(sql); }
int cpkt_sqlite_complete_utf16(const void *sql) {
  return sqlite3_complete16(sql);
}
char *cpkt_sqlite_format_v(const char *format, va_list arguments) {
  if (format == NULL)
    return NULL;
  return sqlite3_vmprintf(format, arguments);
}

char *cpkt_sqlite_format(const char *format, ...) {
  char *result;
  va_list arguments;
  va_start(arguments, format);
  result = cpkt_sqlite_format_v(format, arguments);
  va_end(arguments);
  return result;
}

char *cpkt_sqlite_format_i64(const char *format, cpkt_sqlite_i64 value) {
  if (format == NULL)
    return NULL;
  return sqlite3_mprintf(format, cpkt_sqlite_native_i64(value));
}

char *cpkt_sqlite_format_u64(const char *format, cpkt_sqlite_u64 value) {
  if (format == NULL)
    return NULL;
  return sqlite3_mprintf(format, cpkt_sqlite_native_u64(value));
}

char *cpkt_sqlite_format_into_v(int byte_count, char *buffer,
                                const char *format, va_list arguments) {
  if (byte_count <= 0 || buffer == NULL || format == NULL)
    return buffer;
  return sqlite3_vsnprintf(byte_count, buffer, format, arguments);
}

char *cpkt_sqlite_format_into(int byte_count, char *buffer, const char *format,
                              ...) {
  char *result;
  va_list arguments;
  va_start(arguments, format);
  result = cpkt_sqlite_format_into_v(byte_count, buffer, format, arguments);
  va_end(arguments);
  return result;
}

char *cpkt_sqlite_format_into_i64(int byte_count, char *buffer,
                                  const char *format, cpkt_sqlite_i64 value) {
  if (byte_count <= 0 || buffer == NULL || format == NULL)
    return buffer;
  return sqlite3_snprintf(byte_count, buffer, format,
                          cpkt_sqlite_native_i64(value));
}

char *cpkt_sqlite_format_into_u64(int byte_count, char *buffer,
                                  const char *format, cpkt_sqlite_u64 value) {
  if (byte_count <= 0 || buffer == NULL || format == NULL)
    return buffer;
  return sqlite3_snprintf(byte_count, buffer, format,
                          cpkt_sqlite_native_u64(value));
}

void *cpkt_sqlite_allocate(int byte_count) {
  return sqlite3_malloc(byte_count);
}
void *cpkt_sqlite_allocate_u64(cpkt_sqlite_u64 byte_count) {
  return sqlite3_malloc64(cpkt_sqlite_native_u64(byte_count));
}
void *cpkt_sqlite_reallocate(void *memory, int byte_count) {
  return sqlite3_realloc(memory, byte_count);
}
void *cpkt_sqlite_reallocate_u64(void *memory, cpkt_sqlite_u64 byte_count) {
  return sqlite3_realloc64(memory, cpkt_sqlite_native_u64(byte_count));
}
void cpkt_sqlite_free(void *memory) { sqlite3_free(memory); }
cpkt_sqlite_u64 cpkt_sqlite_allocation_size(void *memory) {
  return cpkt_sqlite_public_u64(sqlite3_msize(memory));
}
cpkt_sqlite_i64 cpkt_sqlite_memory_used(void) {
  return cpkt_sqlite_public_i64(sqlite3_memory_used());
}
cpkt_sqlite_i64 cpkt_sqlite_memory_highwater(int reset) {
  return cpkt_sqlite_public_i64(sqlite3_memory_highwater(reset));
}
int cpkt_sqlite_memory_alarm(cpkt_sqlite_memory_alarm_callback callback,
                             void *context, cpkt_sqlite_i64 threshold_bytes) {
  pthread_mutex_t *mutex;
  int status;
  mutex = cpkt_sqlite_global_mutex();
  cpkt_sqlite_global_lock(mutex);
  cpkt_sqlite_memory_alarm_state.callback = callback;
  cpkt_sqlite_memory_alarm_state.context = context;
  status = sqlite3_memory_alarm(
      callback == NULL ? NULL : cpkt_sqlite_memory_alarm_trampoline,
      callback == NULL ? NULL : &cpkt_sqlite_memory_alarm_state,
      cpkt_sqlite_native_i64(threshold_bytes));
  cpkt_sqlite_global_unlock(mutex);
  return status;
}
void cpkt_sqlite_randomness(int byte_count, void *buffer) {
  sqlite3_randomness(byte_count, buffer);
}
int cpkt_sqlite_enable_shared_cache(int enabled) {
  return sqlite3_enable_shared_cache(enabled);
}
int cpkt_sqlite_release_memory(int byte_count) {
  return sqlite3_release_memory(byte_count);
}
cpkt_sqlite_i64 cpkt_sqlite_soft_heap_limit(cpkt_sqlite_i64 byte_count) {
  return cpkt_sqlite_public_i64(
      sqlite3_soft_heap_limit64(cpkt_sqlite_native_i64(byte_count)));
}
cpkt_sqlite_i64 cpkt_sqlite_hard_heap_limit(cpkt_sqlite_i64 byte_count) {
  return cpkt_sqlite_public_i64(
      sqlite3_hard_heap_limit64(cpkt_sqlite_native_i64(byte_count)));
}
const char *cpkt_sqlite_error_string(int code) { return sqlite3_errstr(code); }
int cpkt_sqlite_keyword_count(void) { return sqlite3_keyword_count(); }
int cpkt_sqlite_keyword_name(int index, const char **name, int *byte_count) {
  return sqlite3_keyword_name(index, name, byte_count);
}
int cpkt_sqlite_keyword_check(const char *name, int byte_count) {
  return sqlite3_keyword_check(name, byte_count);
}

static const char *
cpkt_sqlite_filename_database(const cpkt_sqlite_filename *self) {
  return sqlite3_filename_database(cpkt_sqlite_native_filename(self));
}

static const char *
cpkt_sqlite_filename_journal(const cpkt_sqlite_filename *self) {
  return sqlite3_filename_journal(cpkt_sqlite_native_filename(self));
}

static const char *cpkt_sqlite_filename_wal(const cpkt_sqlite_filename *self) {
  return sqlite3_filename_wal(cpkt_sqlite_native_filename(self));
}

static const char *
cpkt_sqlite_filename_uri_parameter(const cpkt_sqlite_filename *self,
                                   const char *name) {
  if (name == NULL)
    return NULL;
  return sqlite3_uri_parameter(cpkt_sqlite_native_filename(self), name);
}

static int cpkt_sqlite_filename_uri_boolean(const cpkt_sqlite_filename *self,
                                            const char *name,
                                            int default_value) {
  if (name == NULL)
    return default_value;
  return sqlite3_uri_boolean(cpkt_sqlite_native_filename(self), name,
                             default_value);
}

static cpkt_sqlite_i64
cpkt_sqlite_filename_uri_i64(const cpkt_sqlite_filename *self, const char *name,
                             cpkt_sqlite_i64 default_value) {
  if (name == NULL)
    return default_value;
  return cpkt_sqlite_public_i64(
      sqlite3_uri_int64(cpkt_sqlite_native_filename(self), name,
                        cpkt_sqlite_native_i64(default_value)));
}

static const char *
cpkt_sqlite_filename_uri_key(const cpkt_sqlite_filename *self, int index) {
  return sqlite3_uri_key(cpkt_sqlite_native_filename(self), index);
}

static void cpkt_sqlite_filename_close(cpkt_sqlite_filename *self) {
  if (self == NULL)
    return;
  if (cpkt_sqlite_native_filename(self) != NULL) {
    sqlite3_free_filename(cpkt_sqlite_native_filename(self));
  }
  self->filename = NULL;
  free(self);
}

cpkt_sqlite_filename *cpkt_sqlite_filename_new(const char *database,
                                               const char *journal,
                                               const char *wal,
                                               int parameter_count,
                                               const char *const *parameters) {
  sqlite3_filename native_filename;
  cpkt_sqlite_filename *public_filename;
  if (database == NULL || parameter_count < 0 ||
      (parameter_count > 0 && parameters == NULL))
    return NULL;
  native_filename = sqlite3_create_filename(
      database, journal, wal, parameter_count, (const char **)parameters);
  if (native_filename == NULL)
    return NULL;
  public_filename = (cpkt_sqlite_filename *)calloc(1, sizeof(*public_filename));
  if (public_filename == NULL) {
    sqlite3_free_filename(native_filename);
    return NULL;
  }
  public_filename->database = cpkt_sqlite_filename_database;
  public_filename->journal = cpkt_sqlite_filename_journal;
  public_filename->wal = cpkt_sqlite_filename_wal;
  public_filename->uri_parameter = cpkt_sqlite_filename_uri_parameter;
  public_filename->uri_boolean = cpkt_sqlite_filename_uri_boolean;
  public_filename->uri_i64 = cpkt_sqlite_filename_uri_i64;
  public_filename->uri_key = cpkt_sqlite_filename_uri_key;
  public_filename->close = cpkt_sqlite_filename_close;
  public_filename->filename = (void *)native_filename;
  return public_filename;
}

static int cpkt_sqlite_fts5_token_trampoline(void *context, int flags,
                                             const char *token,
                                             int token_byte_count,
                                             int start_offset, int end_offset) {
  cpkt_sqlite_fts5_token_context *token_context;
  token_context = (cpkt_sqlite_fts5_token_context *)context;
  if (token_context == NULL || token_context->token == NULL)
    return SQLITE_MISUSE;
  return token_context->token(token_context->context, flags, token,
                              token_byte_count, start_offset, end_offset);
}

static int cpkt_sqlite_fts5_tokenizer_create_trampoline(void *context,
                                                        const char **arguments,
                                                        int argument_count,
                                                        Fts5Tokenizer **out) {
  cpkt_sqlite_fts5_tokenizer_binding *binding;
  cpkt_sqlite_fts5_tokenizer *tokenizer;
  int status;
  binding = (cpkt_sqlite_fts5_tokenizer_binding *)context;
  if (out != NULL)
    *out = NULL;
  if (binding == NULL || binding->create == NULL || out == NULL)
    return SQLITE_MISUSE;
  tokenizer = NULL;
  status = binding->create(binding->context, (const char *const *)arguments,
                           argument_count, &tokenizer);
  if (status != SQLITE_OK)
    return status;
  if (tokenizer == NULL || tokenizer->facade != NULL)
    return SQLITE_MISUSE;
  tokenizer->facade = binding;
  *out = (Fts5Tokenizer *)tokenizer;
  return SQLITE_OK;
}

static void
cpkt_sqlite_fts5_tokenizer_delete_trampoline(Fts5Tokenizer *native_tokenizer) {
  cpkt_sqlite_fts5_tokenizer *tokenizer;
  cpkt_sqlite_fts5_tokenizer_binding *binding;
  tokenizer = (cpkt_sqlite_fts5_tokenizer *)native_tokenizer;
  if (tokenizer == NULL)
    return;
  binding = (cpkt_sqlite_fts5_tokenizer_binding *)tokenizer->facade;
  tokenizer->facade = NULL;
  if (binding != NULL && binding->destroy != NULL) {
    binding->destroy(tokenizer, binding->context);
  }
  free(tokenizer);
}

static int cpkt_sqlite_fts5_tokenizer_tokenize_trampoline(
    Fts5Tokenizer *native_tokenizer, void *context, int flags, const char *text,
    int text_byte_count, const char *locale, int locale_byte_count,
    int (*token)(void *, int, const char *, int, int, int)) {
  cpkt_sqlite_fts5_tokenizer *tokenizer;
  cpkt_sqlite_fts5_tokenizer_binding *binding;
  cpkt_sqlite_fts5_token_context token_context;
  tokenizer = (cpkt_sqlite_fts5_tokenizer *)native_tokenizer;
  if (tokenizer == NULL || token == NULL)
    return SQLITE_MISUSE;
  binding = (cpkt_sqlite_fts5_tokenizer_binding *)tokenizer->facade;
  if (binding == NULL || binding->tokenize == NULL)
    return SQLITE_MISUSE;
  token_context.context = context;
  token_context.token = token;
  return binding->tokenize(tokenizer, &token_context, flags, text,
                           text_byte_count, locale, locale_byte_count,
                           cpkt_sqlite_fts5_token_trampoline);
}

static void cpkt_sqlite_fts5_tokenizer_binding_destroy(void *context) {
  cpkt_sqlite_fts5_tokenizer_binding *binding;
  binding = (cpkt_sqlite_fts5_tokenizer_binding *)context;
  if (binding == NULL)
    return;
  if (binding->binding_destroy != NULL)
    binding->binding_destroy(binding->context);
  free(binding);
}

static void cpkt_sqlite_fts5_context_initialize(cpkt_sqlite_fts5_context *self,
                                                const Fts5ExtensionApi *api,
                                                Fts5Context *context,
                                                cpkt_sqlite *database);

static int cpkt_sqlite_fts5_public_token_trampoline(void *context, int flags,
                                                    const char *token,
                                                    int token_byte_count,
                                                    int start_offset,
                                                    int end_offset) {
  cpkt_sqlite_fts5_public_token_context *token_context;
  token_context = (cpkt_sqlite_fts5_public_token_context *)context;
  if (token_context == NULL || token_context->callback == NULL)
    return SQLITE_MISUSE;
  return token_context->callback(token_context->user_context, flags, token,
                                 token_byte_count, start_offset, end_offset);
}

static void *
cpkt_sqlite_fts5_context_user_data(const cpkt_sqlite_fts5_context *self) {
  const Fts5ExtensionApi *api;
  cpkt_sqlite_fts5_auxiliary_binding *binding;
  api = cpkt_sqlite_native_fts5_extension_api(self);
  if (api == NULL || api->xUserData == NULL)
    return NULL;
  binding = (cpkt_sqlite_fts5_auxiliary_binding *)api->xUserData(
      cpkt_sqlite_native_fts5_context(self));
  return binding == NULL ? NULL : binding->user_data;
}

static int
cpkt_sqlite_fts5_context_column_count(const cpkt_sqlite_fts5_context *self) {
  const Fts5ExtensionApi *api;
  api = cpkt_sqlite_native_fts5_extension_api(self);
  if (api == NULL || api->xColumnCount == NULL)
    return 0;
  return api->xColumnCount(cpkt_sqlite_native_fts5_context(self));
}

static int
cpkt_sqlite_fts5_context_row_count(const cpkt_sqlite_fts5_context *self,
                                   cpkt_sqlite_i64 *out) {
  const Fts5ExtensionApi *api;
  sqlite3_int64 value;
  int status;
  if (out != NULL)
    *out = cpkt_sqlite_i64_make(0, 0);
  api = cpkt_sqlite_native_fts5_extension_api(self);
  if (api == NULL || api->xRowCount == NULL || out == NULL)
    return SQLITE_MISUSE;
  value = 0;
  status = api->xRowCount(cpkt_sqlite_native_fts5_context(self), &value);
  if (status == SQLITE_OK)
    *out = cpkt_sqlite_public_i64(value);
  return status;
}

static int
cpkt_sqlite_fts5_context_column_total_size(const cpkt_sqlite_fts5_context *self,
                                           int column, cpkt_sqlite_i64 *out) {
  const Fts5ExtensionApi *api;
  sqlite3_int64 value;
  int status;
  if (out != NULL)
    *out = cpkt_sqlite_i64_make(0, 0);
  api = cpkt_sqlite_native_fts5_extension_api(self);
  if (api == NULL || api->xColumnTotalSize == NULL || out == NULL)
    return SQLITE_MISUSE;
  value = 0;
  status = api->xColumnTotalSize(cpkt_sqlite_native_fts5_context(self), column,
                                 &value);
  if (status == SQLITE_OK)
    *out = cpkt_sqlite_public_i64(value);
  return status;
}

static int cpkt_sqlite_fts5_context_tokenize(
    cpkt_sqlite_fts5_context *self, const char *text, int text_byte_count,
    void *context, cpkt_sqlite_fts5_token_callback token) {
  const Fts5ExtensionApi *api;
  cpkt_sqlite_fts5_public_token_context token_context;
  api = cpkt_sqlite_native_fts5_extension_api(self);
  if (api == NULL || api->xTokenize == NULL || text == NULL || token == NULL) {
    return SQLITE_MISUSE;
  }
  token_context.user_context = context;
  token_context.callback = token;
  return api->xTokenize(cpkt_sqlite_native_fts5_context(self), text,
                        text_byte_count, &token_context,
                        cpkt_sqlite_fts5_public_token_trampoline);
}

static int cpkt_sqlite_fts5_context_tokenize_locale(
    cpkt_sqlite_fts5_context *self, const char *text, int text_byte_count,
    const char *locale, int locale_byte_count, void *context,
    cpkt_sqlite_fts5_token_callback token) {
  const Fts5ExtensionApi *api;
  cpkt_sqlite_fts5_public_token_context token_context;
  api = cpkt_sqlite_native_fts5_extension_api(self);
  if (api == NULL || api->iVersion < 4 || api->xTokenize_v2 == NULL ||
      text == NULL || (locale == NULL && locale_byte_count != 0) ||
      token == NULL)
    return SQLITE_MISUSE;
  token_context.user_context = context;
  token_context.callback = token;
  return api->xTokenize_v2(cpkt_sqlite_native_fts5_context(self), text,
                           text_byte_count, locale, locale_byte_count,
                           &token_context,
                           cpkt_sqlite_fts5_public_token_trampoline);
}

static int
cpkt_sqlite_fts5_context_phrase_count(const cpkt_sqlite_fts5_context *self) {
  const Fts5ExtensionApi *api;
  api = cpkt_sqlite_native_fts5_extension_api(self);
  if (api == NULL || api->xPhraseCount == NULL)
    return 0;
  return api->xPhraseCount(cpkt_sqlite_native_fts5_context(self));
}

static int
cpkt_sqlite_fts5_context_phrase_size(const cpkt_sqlite_fts5_context *self,
                                     int phrase) {
  const Fts5ExtensionApi *api;
  api = cpkt_sqlite_native_fts5_extension_api(self);
  if (api == NULL || api->xPhraseSize == NULL)
    return 0;
  return api->xPhraseSize(cpkt_sqlite_native_fts5_context(self), phrase);
}

static int
cpkt_sqlite_fts5_context_instance_count(const cpkt_sqlite_fts5_context *self,
                                        int *out) {
  const Fts5ExtensionApi *api;
  if (out != NULL)
    *out = 0;
  api = cpkt_sqlite_native_fts5_extension_api(self);
  if (api == NULL || api->xInstCount == NULL || out == NULL)
    return SQLITE_MISUSE;
  return api->xInstCount(cpkt_sqlite_native_fts5_context(self), out);
}

static int
cpkt_sqlite_fts5_context_instance(const cpkt_sqlite_fts5_context *self,
                                  int index, int *phrase_out, int *column_out,
                                  int *offset_out) {
  const Fts5ExtensionApi *api;
  api = cpkt_sqlite_native_fts5_extension_api(self);
  if (api == NULL || api->xInst == NULL || phrase_out == NULL ||
      column_out == NULL || offset_out == NULL)
    return SQLITE_MISUSE;
  return api->xInst(cpkt_sqlite_native_fts5_context(self), index, phrase_out,
                    column_out, offset_out);
}

static cpkt_sqlite_i64
cpkt_sqlite_fts5_context_rowid(const cpkt_sqlite_fts5_context *self) {
  const Fts5ExtensionApi *api;
  api = cpkt_sqlite_native_fts5_extension_api(self);
  if (api == NULL || api->xRowid == NULL)
    return cpkt_sqlite_i64_make(0, 0);
  return cpkt_sqlite_public_i64(
      api->xRowid(cpkt_sqlite_native_fts5_context(self)));
}

static int
cpkt_sqlite_fts5_context_column_text(const cpkt_sqlite_fts5_context *self,
                                     int column, const char **text_out,
                                     int *byte_count_out) {
  const Fts5ExtensionApi *api;
  if (text_out != NULL)
    *text_out = NULL;
  if (byte_count_out != NULL)
    *byte_count_out = 0;
  api = cpkt_sqlite_native_fts5_extension_api(self);
  if (api == NULL || api->xColumnText == NULL || text_out == NULL ||
      byte_count_out == NULL)
    return SQLITE_MISUSE;
  return api->xColumnText(cpkt_sqlite_native_fts5_context(self), column,
                          text_out, byte_count_out);
}

static int
cpkt_sqlite_fts5_context_column_size(const cpkt_sqlite_fts5_context *self,
                                     int column, int *token_count_out) {
  const Fts5ExtensionApi *api;
  if (token_count_out != NULL)
    *token_count_out = 0;
  api = cpkt_sqlite_native_fts5_extension_api(self);
  if (api == NULL || api->xColumnSize == NULL || token_count_out == NULL) {
    return SQLITE_MISUSE;
  }
  return api->xColumnSize(cpkt_sqlite_native_fts5_context(self), column,
                          token_count_out);
}

static void *
cpkt_sqlite_fts5_phrase_user_data(const cpkt_sqlite_fts5_context *context) {
  const cpkt_sqlite_fts5_phrase_public_context *scope;
  scope = (const cpkt_sqlite_fts5_phrase_public_context *)context;
  return scope->user_data;
}

static int cpkt_sqlite_fts5_query_phrase_trampoline(const Fts5ExtensionApi *api,
                                                    Fts5Context *context,
                                                    void *user_data) {
  cpkt_sqlite_fts5_query_phrase_context *query_context;
  cpkt_sqlite_fts5_phrase_public_context scope;
  query_context = (cpkt_sqlite_fts5_query_phrase_context *)user_data;
  if (query_context == NULL || query_context->callback == NULL)
    return SQLITE_MISUSE;
  cpkt_sqlite_fts5_context_initialize(&scope.public_context, api, context,
                                      query_context->database);
  scope.public_context.user_data = cpkt_sqlite_fts5_phrase_user_data;
  scope.user_data = query_context->user_data;
  return query_context->callback(&scope.public_context,
                                 query_context->user_data);
}

static int cpkt_sqlite_fts5_context_query_phrase(
    cpkt_sqlite_fts5_context *self, int phrase, void *user_data,
    cpkt_sqlite_fts5_query_phrase_callback callback) {
  const Fts5ExtensionApi *api;
  cpkt_sqlite_fts5_query_phrase_context query_context;
  api = cpkt_sqlite_native_fts5_extension_api(self);
  if (api == NULL || api->xQueryPhrase == NULL || callback == NULL)
    return SQLITE_MISUSE;
  query_context.database = self->database;
  query_context.user_data = user_data;
  query_context.callback = callback;
  return api->xQueryPhrase(cpkt_sqlite_native_fts5_context(self), phrase,
                           &query_context,
                           cpkt_sqlite_fts5_query_phrase_trampoline);
}

static int
cpkt_sqlite_fts5_context_set_auxdata(cpkt_sqlite_fts5_context *self, void *data,
                                     cpkt_sqlite_destroy_callback destroy) {
  const Fts5ExtensionApi *api;
  api = cpkt_sqlite_native_fts5_extension_api(self);
  if (api == NULL || api->xSetAuxdata == NULL)
    return SQLITE_MISUSE;
  return api->xSetAuxdata(cpkt_sqlite_native_fts5_context(self), data, destroy);
}

static void *
cpkt_sqlite_fts5_context_get_auxdata(cpkt_sqlite_fts5_context *self,
                                     int clear) {
  const Fts5ExtensionApi *api;
  api = cpkt_sqlite_native_fts5_extension_api(self);
  if (api == NULL || api->xGetAuxdata == NULL)
    return NULL;
  return api->xGetAuxdata(cpkt_sqlite_native_fts5_context(self), clear);
}

static int cpkt_sqlite_fts5_context_phrase_first(
    cpkt_sqlite_fts5_context *self, int phrase,
    cpkt_sqlite_fts5_phrase_iterator *iterator, int *column_out,
    int *offset_out) {
  const Fts5ExtensionApi *api;
  Fts5PhraseIter native_iterator;
  int status;
  if (iterator == NULL || column_out == NULL || offset_out == NULL)
    return SQLITE_MISUSE;
  api = cpkt_sqlite_native_fts5_extension_api(self);
  if (api == NULL || api->xPhraseFirst == NULL)
    return SQLITE_MISUSE;
  native_iterator.a = iterator->first;
  native_iterator.b = iterator->second;
  status = api->xPhraseFirst(cpkt_sqlite_native_fts5_context(self), phrase,
                             &native_iterator, column_out, offset_out);
  iterator->first = native_iterator.a;
  iterator->second = native_iterator.b;
  return status;
}

static void
cpkt_sqlite_fts5_context_phrase_next(cpkt_sqlite_fts5_context *self,
                                     cpkt_sqlite_fts5_phrase_iterator *iterator,
                                     int *column_out, int *offset_out) {
  const Fts5ExtensionApi *api;
  Fts5PhraseIter native_iterator;
  if (iterator == NULL || column_out == NULL || offset_out == NULL)
    return;
  api = cpkt_sqlite_native_fts5_extension_api(self);
  if (api == NULL || api->xPhraseNext == NULL)
    return;
  native_iterator.a = iterator->first;
  native_iterator.b = iterator->second;
  api->xPhraseNext(cpkt_sqlite_native_fts5_context(self), &native_iterator,
                   column_out, offset_out);
  iterator->first = native_iterator.a;
  iterator->second = native_iterator.b;
}

static int cpkt_sqlite_fts5_context_phrase_first_column(
    cpkt_sqlite_fts5_context *self, int phrase,
    cpkt_sqlite_fts5_phrase_iterator *iterator, int *column_out) {
  const Fts5ExtensionApi *api;
  Fts5PhraseIter native_iterator;
  int status;
  if (iterator == NULL || column_out == NULL)
    return SQLITE_MISUSE;
  api = cpkt_sqlite_native_fts5_extension_api(self);
  if (api == NULL || api->xPhraseFirstColumn == NULL)
    return SQLITE_MISUSE;
  native_iterator.a = iterator->first;
  native_iterator.b = iterator->second;
  status = api->xPhraseFirstColumn(cpkt_sqlite_native_fts5_context(self),
                                   phrase, &native_iterator, column_out);
  iterator->first = native_iterator.a;
  iterator->second = native_iterator.b;
  return status;
}

static void cpkt_sqlite_fts5_context_phrase_next_column(
    cpkt_sqlite_fts5_context *self, cpkt_sqlite_fts5_phrase_iterator *iterator,
    int *column_out) {
  const Fts5ExtensionApi *api;
  Fts5PhraseIter native_iterator;
  if (iterator == NULL || column_out == NULL)
    return;
  api = cpkt_sqlite_native_fts5_extension_api(self);
  if (api == NULL || api->xPhraseNextColumn == NULL)
    return;
  native_iterator.a = iterator->first;
  native_iterator.b = iterator->second;
  api->xPhraseNextColumn(cpkt_sqlite_native_fts5_context(self),
                         &native_iterator, column_out);
  iterator->first = native_iterator.a;
  iterator->second = native_iterator.b;
}

static int cpkt_sqlite_fts5_context_query_token(cpkt_sqlite_fts5_context *self,
                                                int phrase, int token,
                                                const char **text_out,
                                                int *byte_count_out) {
  const Fts5ExtensionApi *api;
  if (text_out != NULL)
    *text_out = NULL;
  if (byte_count_out != NULL)
    *byte_count_out = 0;
  api = cpkt_sqlite_native_fts5_extension_api(self);
  if (api == NULL || api->iVersion < 3 || api->xQueryToken == NULL ||
      text_out == NULL || byte_count_out == NULL)
    return SQLITE_MISUSE;
  return api->xQueryToken(cpkt_sqlite_native_fts5_context(self), phrase, token,
                          text_out, byte_count_out);
}

static int cpkt_sqlite_fts5_context_instance_token(
    cpkt_sqlite_fts5_context *self, int instance, int token,
    const char **text_out, int *byte_count_out) {
  const Fts5ExtensionApi *api;
  if (text_out != NULL)
    *text_out = NULL;
  if (byte_count_out != NULL)
    *byte_count_out = 0;
  api = cpkt_sqlite_native_fts5_extension_api(self);
  if (api == NULL || api->iVersion < 3 || api->xInstToken == NULL ||
      text_out == NULL || byte_count_out == NULL)
    return SQLITE_MISUSE;
  return api->xInstToken(cpkt_sqlite_native_fts5_context(self), instance, token,
                         text_out, byte_count_out);
}

static int
cpkt_sqlite_fts5_context_column_locale(cpkt_sqlite_fts5_context *self,
                                       int column, const char **locale_out,
                                       int *byte_count_out) {
  const Fts5ExtensionApi *api;
  if (locale_out != NULL)
    *locale_out = NULL;
  if (byte_count_out != NULL)
    *byte_count_out = 0;
  api = cpkt_sqlite_native_fts5_extension_api(self);
  if (api == NULL || api->iVersion < 4 || api->xColumnLocale == NULL ||
      locale_out == NULL || byte_count_out == NULL)
    return SQLITE_MISUSE;
  return api->xColumnLocale(cpkt_sqlite_native_fts5_context(self), column,
                            locale_out, byte_count_out);
}

static void cpkt_sqlite_fts5_context_initialize(cpkt_sqlite_fts5_context *self,
                                                const Fts5ExtensionApi *api,
                                                Fts5Context *context,
                                                cpkt_sqlite *database) {
  self->version = api == NULL ? 0 : api->iVersion;
  self->user_data = cpkt_sqlite_fts5_context_user_data;
  self->column_count = cpkt_sqlite_fts5_context_column_count;
  self->row_count = cpkt_sqlite_fts5_context_row_count;
  self->column_total_size = cpkt_sqlite_fts5_context_column_total_size;
  self->tokenize = cpkt_sqlite_fts5_context_tokenize;
  self->tokenize_locale = cpkt_sqlite_fts5_context_tokenize_locale;
  self->phrase_count = cpkt_sqlite_fts5_context_phrase_count;
  self->phrase_size = cpkt_sqlite_fts5_context_phrase_size;
  self->instance_count = cpkt_sqlite_fts5_context_instance_count;
  self->instance = cpkt_sqlite_fts5_context_instance;
  self->rowid = cpkt_sqlite_fts5_context_rowid;
  self->column_text = cpkt_sqlite_fts5_context_column_text;
  self->column_size = cpkt_sqlite_fts5_context_column_size;
  self->query_phrase = cpkt_sqlite_fts5_context_query_phrase;
  self->set_auxdata = cpkt_sqlite_fts5_context_set_auxdata;
  self->get_auxdata = cpkt_sqlite_fts5_context_get_auxdata;
  self->phrase_first = cpkt_sqlite_fts5_context_phrase_first;
  self->phrase_next = cpkt_sqlite_fts5_context_phrase_next;
  self->phrase_first_column = cpkt_sqlite_fts5_context_phrase_first_column;
  self->phrase_next_column = cpkt_sqlite_fts5_context_phrase_next_column;
  self->query_token = cpkt_sqlite_fts5_context_query_token;
  self->instance_token = cpkt_sqlite_fts5_context_instance_token;
  self->column_locale = cpkt_sqlite_fts5_context_column_locale;
  self->api = (void *)api;
  self->context = (void *)context;
  self->database = database;
}

static void cpkt_sqlite_fts5_auxiliary_destroy(void *context) {
  cpkt_sqlite_fts5_auxiliary_binding *binding;
  binding = (cpkt_sqlite_fts5_auxiliary_binding *)context;
  if (binding == NULL)
    return;
  if (binding->destroy != NULL)
    binding->destroy(binding->user_data);
  free(binding);
}

static void cpkt_sqlite_fts5_auxiliary_trampoline(const Fts5ExtensionApi *api,
                                                  Fts5Context *fts_context,
                                                  sqlite3_context *sql_context,
                                                  int argument_count,
                                                  sqlite3_value **arguments) {
  cpkt_sqlite_fts5_auxiliary_binding *binding;
  cpkt_sqlite_fts5_sql_context_scope scope;
  cpkt_sqlite_fts5_sql_context_scope **link;
  cpkt_sqlite_fts5_context public_fts_context;
  cpkt_sqlite_context public_sql_context;
  cpkt_sqlite_value *public_values;
  cpkt_sqlite_value **public_value_pointers;
  pthread_mutex_t *mutex;
  int index;
  binding =
      api == NULL || api->xUserData == NULL
          ? NULL
          : (cpkt_sqlite_fts5_auxiliary_binding *)api->xUserData(fts_context);
  if (binding == NULL || binding->callback == NULL) {
    sqlite3_result_error_code(sql_context, SQLITE_MISUSE);
    return;
  }
  public_values = NULL;
  public_value_pointers = NULL;
  if (argument_count > 0) {
    public_values = (cpkt_sqlite_value *)calloc((size_t)argument_count,
                                                sizeof(*public_values));
    public_value_pointers = (cpkt_sqlite_value **)calloc(
        (size_t)argument_count, sizeof(*public_value_pointers));
    if (public_values == NULL || public_value_pointers == NULL) {
      free(public_values);
      free(public_value_pointers);
      sqlite3_result_error_nomem(sql_context);
      return;
    }
    for (index = 0; index < argument_count; ++index) {
      public_values[index].value = arguments[index];
      public_values[index].owned = 0;
      public_values[index].shell_owned = 0;
      public_value_pointers[index] = &public_values[index];
    }
  }
  cpkt_sqlite_fts5_context_initialize(&public_fts_context, api, fts_context,
                                      binding->database);
  public_sql_context.context = sql_context;
  public_sql_context.database = binding->database;
  scope.context = &public_sql_context;
  scope.user_data = binding->user_data;
  mutex = cpkt_sqlite_global_mutex();
  cpkt_sqlite_global_lock(mutex);
  scope.next = cpkt_sqlite_fts5_sql_context_head;
  cpkt_sqlite_fts5_sql_context_head = &scope;
  cpkt_sqlite_global_unlock(mutex);
  binding->callback(&public_fts_context, &public_sql_context, argument_count,
                    public_value_pointers, binding->user_data);
  cpkt_sqlite_global_lock(mutex);
  link = &cpkt_sqlite_fts5_sql_context_head;
  while (*link != NULL && *link != &scope)
    link = &(*link)->next;
  if (*link == &scope)
    *link = scope.next;
  cpkt_sqlite_global_unlock(mutex);
  free(public_value_pointers);
  free(public_values);
}

static int cpkt_sqlite_fts5_api_version(const cpkt_sqlite_fts5_api *self) {
  fts5_api *api;
  api = cpkt_sqlite_native_fts5_api(self);
  return api == NULL ? 0 : api->iVersion;
}

typedef struct cpkt_sqlite_fts5_found_binding {
  fts5_tokenizer_v2 methods;
  void *context;
} cpkt_sqlite_fts5_found_binding;

typedef struct cpkt_sqlite_fts5_instance_binding {
  fts5_tokenizer_v2 methods;
  Fts5Tokenizer *native;
} cpkt_sqlite_fts5_instance_binding;

static void
cpkt_sqlite_fts5_found_close(cpkt_sqlite_fts5_found_tokenizer *self) {
  if (self == NULL)
    return;
  free(self->internal);
  free(self);
}

static void
cpkt_sqlite_fts5_instance_close(cpkt_sqlite_fts5_tokenizer_instance *self) {
  cpkt_sqlite_fts5_instance_binding *binding;
  if (self == NULL)
    return;
  binding = (cpkt_sqlite_fts5_instance_binding *)self->internal;
  if (binding != NULL) {
    binding->methods.xDelete(binding->native);
    free(binding);
  }
  free(self);
}

static int cpkt_sqlite_fts5_instance_tokenize(
    cpkt_sqlite_fts5_tokenizer_instance *self, void *context, int flags,
    const char *text, int text_byte_count, const char *locale,
    int locale_byte_count, cpkt_sqlite_fts5_token_callback token) {
  cpkt_sqlite_fts5_instance_binding *binding;
  cpkt_sqlite_fts5_public_token_context token_context;
  if (self == NULL || self->internal == NULL || text == NULL || token == NULL)
    return SQLITE_MISUSE;
  binding = (cpkt_sqlite_fts5_instance_binding *)self->internal;
  token_context.user_context = context;
  token_context.callback = token;
  return binding->methods.xTokenize(
      binding->native, &token_context, flags, text, text_byte_count, locale,
      locale_byte_count, cpkt_sqlite_fts5_public_token_trampoline);
}

static int
cpkt_sqlite_fts5_found_create(cpkt_sqlite_fts5_found_tokenizer *self,
                              const char *const *arguments, int argument_count,
                              cpkt_sqlite_fts5_tokenizer_instance **out) {
  cpkt_sqlite_fts5_found_binding *found;
  cpkt_sqlite_fts5_instance_binding *binding;
  cpkt_sqlite_fts5_tokenizer_instance *instance;
  int status;
  if (out != NULL)
    *out = NULL;
  if (self == NULL || self->internal == NULL || out == NULL ||
      argument_count < 0 || (argument_count != 0 && arguments == NULL))
    return SQLITE_MISUSE;
  found = (cpkt_sqlite_fts5_found_binding *)self->internal;
  binding = (cpkt_sqlite_fts5_instance_binding *)calloc(1, sizeof(*binding));
  instance =
      (cpkt_sqlite_fts5_tokenizer_instance *)calloc(1, sizeof(*instance));
  if (binding == NULL || instance == NULL) {
    free(binding);
    free(instance);
    return SQLITE_NOMEM;
  }
  binding->methods = found->methods;
  status = binding->methods.xCreate(found->context, (const char **)arguments,
                                    argument_count, &binding->native);
  if (status != SQLITE_OK) {
    free(binding);
    free(instance);
    return status;
  }
  instance->tokenize = cpkt_sqlite_fts5_instance_tokenize;
  instance->close = cpkt_sqlite_fts5_instance_close;
  instance->internal = binding;
  *out = instance;
  return SQLITE_OK;
}

static int
cpkt_sqlite_fts5_api_find_tokenizer(cpkt_sqlite_fts5_api *self,
                                    const char *name,
                                    cpkt_sqlite_fts5_found_tokenizer **out) {
  fts5_api *api;
  fts5_tokenizer_v2 *native;
  cpkt_sqlite_fts5_found_tokenizer *found;
  cpkt_sqlite_fts5_found_binding *binding;
  sqlite3_mutex *mutex;
  void *context;
  int status;
  if (out != NULL)
    *out = NULL;
  api = cpkt_sqlite_native_fts5_api(self);
  if (api == NULL || api->iVersion < 3 || api->xFindTokenizer_v2 == NULL ||
      name == NULL || out == NULL)
    return SQLITE_MISUSE;
  native = NULL;
  context = NULL;
  mutex = cpkt_sqlite_connection_lock(self->database);
  status = api->xFindTokenizer_v2(api, name, &context, &native);
  cpkt_sqlite_connection_unlock(mutex);
  if (status != SQLITE_OK)
    return status;
  if (native == NULL || native->xCreate == NULL || native->xDelete == NULL ||
      native->xTokenize == NULL)
    return SQLITE_NOTFOUND;
  found = (cpkt_sqlite_fts5_found_tokenizer *)calloc(1, sizeof(*found));
  binding = (cpkt_sqlite_fts5_found_binding *)calloc(1, sizeof(*binding));
  if (found == NULL || binding == NULL) {
    free(found);
    free(binding);
    return SQLITE_NOMEM;
  }
  binding->methods = *native;
  binding->context = context;
  found->create = cpkt_sqlite_fts5_found_create;
  found->close = cpkt_sqlite_fts5_found_close;
  found->internal = binding;
  *out = found;
  return SQLITE_OK;
}

static int cpkt_sqlite_fts5_api_create_tokenizer(
    cpkt_sqlite_fts5_api *self, const char *name, void *context,
    cpkt_sqlite_fts5_tokenizer_create_callback create,
    cpkt_sqlite_fts5_tokenizer_destroy_callback destroy,
    cpkt_sqlite_fts5_tokenizer_tokenize_callback tokenize,
    cpkt_sqlite_destroy_callback binding_destroy) {
  fts5_api *api;
  cpkt_sqlite_fts5_tokenizer_binding *binding;
  sqlite3_mutex *mutex;
  int status;
  api = cpkt_sqlite_native_fts5_api(self);
  if (api == NULL || name == NULL || create == NULL || destroy == NULL ||
      tokenize == NULL || api->iVersion < 3 ||
      api->xCreateTokenizer_v2 == NULL) {
    return SQLITE_MISUSE;
  }
  binding = (cpkt_sqlite_fts5_tokenizer_binding *)calloc(1, sizeof(*binding));
  if (binding == NULL)
    return SQLITE_NOMEM;
  binding->context = context;
  binding->create = create;
  binding->destroy = destroy;
  binding->tokenize = tokenize;
  binding->binding_destroy = binding_destroy;
  binding->native_tokenizer.iVersion = 2;
  binding->native_tokenizer.xCreate =
      cpkt_sqlite_fts5_tokenizer_create_trampoline;
  binding->native_tokenizer.xDelete =
      cpkt_sqlite_fts5_tokenizer_delete_trampoline;
  binding->native_tokenizer.xTokenize =
      cpkt_sqlite_fts5_tokenizer_tokenize_trampoline;
  mutex = cpkt_sqlite_connection_lock(self->database);
  status =
      api->xCreateTokenizer_v2(api, name, binding, &binding->native_tokenizer,
                               cpkt_sqlite_fts5_tokenizer_binding_destroy);
  cpkt_sqlite_connection_unlock(mutex);
  if (status != SQLITE_OK) {
    cpkt_sqlite_fts5_tokenizer_binding_destroy(binding);
    return status;
  }
  return SQLITE_OK;
}

static int cpkt_sqlite_fts5_api_create_auxiliary(
    cpkt_sqlite_fts5_api *self, const char *name, void *user_data,
    cpkt_sqlite_fts5_auxiliary_callback callback,
    cpkt_sqlite_destroy_callback destroy) {
  fts5_api *api;
  cpkt_sqlite_fts5_auxiliary_binding *binding;
  sqlite3_mutex *mutex;
  int status;
  api = cpkt_sqlite_native_fts5_api(self);
  if (api == NULL || name == NULL || callback == NULL ||
      api->xCreateFunction == NULL) {
    return SQLITE_MISUSE;
  }
  binding = (cpkt_sqlite_fts5_auxiliary_binding *)calloc(1, sizeof(*binding));
  if (binding == NULL)
    return SQLITE_NOMEM;
  binding->database = self->database;
  binding->user_data = user_data;
  binding->callback = callback;
  binding->destroy = destroy;
  mutex = cpkt_sqlite_connection_lock(self->database);
  status = api->xCreateFunction(api, name, binding,
                                cpkt_sqlite_fts5_auxiliary_trampoline,
                                cpkt_sqlite_fts5_auxiliary_destroy);
  cpkt_sqlite_connection_unlock(mutex);
  if (status != SQLITE_OK) {
    cpkt_sqlite_fts5_auxiliary_destroy(binding);
    return status;
  }
  return SQLITE_OK;
}

static void cpkt_sqlite_fts5_api_close(cpkt_sqlite_fts5_api *self) {
  if (self == NULL)
    return;
  self->api = NULL;
  self->database = NULL;
  free(self);
}

int cpkt_sqlite_fts5_api_open(cpkt_sqlite *database,
                              cpkt_sqlite_fts5_api **out) {
  sqlite3_stmt *statement;
  fts5_api *api;
  cpkt_sqlite_fts5_api *public_api;
  int status;
  if (out != NULL)
    *out = NULL;
  if (database == NULL || cpkt_sqlite_native(database) == NULL || out == NULL) {
    return SQLITE_MISUSE;
  }
  statement = NULL;
  api = NULL;
  status = sqlite3_prepare_v2(cpkt_sqlite_native(database), "SELECT fts5(?1)",
                              -1, &statement, NULL);
  if (status != SQLITE_OK)
    return status;
  status = sqlite3_bind_pointer(statement, 1, &api, "fts5_api_ptr", NULL);
  if (status == SQLITE_OK)
    status = sqlite3_step(statement);
  if (status == SQLITE_ROW)
    status = SQLITE_OK;
  if (sqlite3_finalize(statement) != SQLITE_OK && status == SQLITE_OK) {
    status = SQLITE_ERROR;
  }
  if (status != SQLITE_OK)
    return status;
  if (api == NULL)
    return SQLITE_NOTFOUND;
  public_api = (cpkt_sqlite_fts5_api *)calloc(1, sizeof(*public_api));
  if (public_api == NULL)
    return SQLITE_NOMEM;
  public_api->version = cpkt_sqlite_fts5_api_version;
  public_api->find_tokenizer = cpkt_sqlite_fts5_api_find_tokenizer;
  public_api->create_tokenizer = cpkt_sqlite_fts5_api_create_tokenizer;
  public_api->create_auxiliary = cpkt_sqlite_fts5_api_create_auxiliary;
  public_api->close = cpkt_sqlite_fts5_api_close;
  public_api->api = api;
  public_api->database = database;
  *out = public_api;
  return SQLITE_OK;
}

cpkt_sqlite_fts5_tokenizer *cpkt_sqlite_fts5_tokenizer_new(void *state) {
  cpkt_sqlite_fts5_tokenizer *tokenizer;
  tokenizer = (cpkt_sqlite_fts5_tokenizer *)calloc(1, sizeof(*tokenizer));
  if (tokenizer != NULL)
    tokenizer->state = state;
  return tokenizer;
}

void *cpkt_sqlite_fts5_tokenizer_state(const cpkt_sqlite_fts5_tokenizer *self) {
  return self == NULL ? NULL : self->state;
}

int cpkt_sqlite_case_compare(const char *left, const char *right) {
  return sqlite3_stricmp(left, right);
}
int cpkt_sqlite_case_compare_n(const char *left, const char *right,
                               int byte_count) {
  return sqlite3_strnicmp(left, right, byte_count);
}
int cpkt_sqlite_glob(const char *pattern, const char *text) {
  return sqlite3_strglob(pattern, text);
}
int cpkt_sqlite_like(const char *pattern, const char *text,
                     unsigned long escape_character) {
  return sqlite3_strlike(pattern, text, (unsigned int)escape_character);
}
int cpkt_sqlite_status(int category, int *current, int *highwater, int reset) {
  return sqlite3_status(category, current, highwater, reset);
}
int cpkt_sqlite_status_i64(int category, cpkt_sqlite_i64 *current,
                           cpkt_sqlite_i64 *highwater, int reset) {
  sqlite3_int64 native_current;
  sqlite3_int64 native_highwater;
  int status;
  native_current = 0;
  native_highwater = 0;
  status =
      sqlite3_status64(category, &native_current, &native_highwater, reset);
  if (current != NULL)
    *current = cpkt_sqlite_public_i64(native_current);
  if (highwater != NULL)
    *highwater = cpkt_sqlite_public_i64(native_highwater);
  return status;
}

cpkt_sqlite_i64 cpkt_sqlite_changes64(const cpkt_sqlite *self) {
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return cpkt_sqlite_i64_make(0, 0);
  return cpkt_sqlite_public_i64(sqlite3_changes64(cpkt_sqlite_native(self)));
}

int cpkt_sqlite_total_changes(const cpkt_sqlite *self) {
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return 0;
  return sqlite3_total_changes(cpkt_sqlite_native(self));
}

cpkt_sqlite_i64 cpkt_sqlite_total_changes64(const cpkt_sqlite *self) {
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return cpkt_sqlite_i64_make(0, 0);
  return cpkt_sqlite_public_i64(
      sqlite3_total_changes64(cpkt_sqlite_native(self)));
}

void cpkt_sqlite_set_last_insert_rowid(cpkt_sqlite *self,
                                       cpkt_sqlite_i64 value) {
  if (self != NULL && cpkt_sqlite_native(self) != NULL) {
    sqlite3_set_last_insert_rowid(cpkt_sqlite_native(self),
                                  cpkt_sqlite_native_i64(value));
  }
}

void cpkt_sqlite_interrupt(cpkt_sqlite *self) {
  if (self != NULL && cpkt_sqlite_native(self) != NULL)
    sqlite3_interrupt(cpkt_sqlite_native(self));
}

int cpkt_sqlite_interrupted(const cpkt_sqlite *self) {
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return 0;
  return sqlite3_is_interrupted(cpkt_sqlite_native(self));
}

int cpkt_sqlite_limit(cpkt_sqlite *self, int category, int new_value) {
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return CPKT_SQLITE_MISUSE;
  return sqlite3_limit(cpkt_sqlite_native(self), category, new_value);
}

int cpkt_sqlite_autocommit(const cpkt_sqlite *self) {
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return 0;
  return sqlite3_get_autocommit(cpkt_sqlite_native(self));
}

const char *cpkt_sqlite_database_name(const cpkt_sqlite *self, int index) {
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return NULL;
  return sqlite3_db_name(cpkt_sqlite_native(self), index);
}

const char *cpkt_sqlite_database_filename(const cpkt_sqlite *self,
                                          const char *name) {
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return NULL;
  return sqlite3_db_filename(cpkt_sqlite_native(self), name);
}

int cpkt_sqlite_database_readonly(const cpkt_sqlite *self, const char *name) {
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return -1;
  return sqlite3_db_readonly(cpkt_sqlite_native(self), name);
}

int cpkt_sqlite_transaction_state(const cpkt_sqlite *self, const char *schema) {
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return -1;
  return sqlite3_txn_state(cpkt_sqlite_native(self), schema);
}

int cpkt_sqlite_table_column_metadata(
    cpkt_sqlite *self, const char *database_name, const char *table_name,
    const char *column_name, cpkt_sqlite_column_metadata *metadata_out) {
  const char *declared_type;
  const char *collation;
  int not_null;
  int primary_key;
  int auto_increment;
  int status;
  if (self == NULL || cpkt_sqlite_native(self) == NULL || table_name == NULL) {
    return SQLITE_MISUSE;
  }
  declared_type = NULL;
  collation = NULL;
  not_null = 0;
  primary_key = 0;
  auto_increment = 0;
  status = sqlite3_table_column_metadata(
      cpkt_sqlite_native(self), database_name, table_name, column_name,
      metadata_out == NULL ? NULL : &declared_type,
      metadata_out == NULL ? NULL : &collation,
      metadata_out == NULL ? NULL : &not_null,
      metadata_out == NULL ? NULL : &primary_key,
      metadata_out == NULL ? NULL : &auto_increment);
  if (status != SQLITE_OK || metadata_out == NULL)
    return status;
  metadata_out->declared_type = declared_type;
  metadata_out->collation = collation;
  metadata_out->not_null = not_null;
  metadata_out->primary_key = primary_key;
  metadata_out->auto_increment = auto_increment;
  return SQLITE_OK;
}

int cpkt_sqlite_database_release_memory(cpkt_sqlite *self) {
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return CPKT_SQLITE_MISUSE;
  return sqlite3_db_release_memory(cpkt_sqlite_native(self));
}

int cpkt_sqlite_cache_flush(cpkt_sqlite *self) {
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return CPKT_SQLITE_MISUSE;
  return sqlite3_db_cacheflush(cpkt_sqlite_native(self));
}

int cpkt_sqlite_file_control(cpkt_sqlite *self, const char *database_name,
                             int operation, void *argument) {
  sqlite3_int64 native_value;
  int status;
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return SQLITE_MISUSE;
  if (argument == NULL || !cpkt_sqlite_file_control_has_i64(operation))
    return sqlite3_file_control(cpkt_sqlite_native(self), database_name,
                                operation, argument);
  native_value = cpkt_sqlite_native_i64(*(cpkt_sqlite_i64 *)argument);
  status = sqlite3_file_control(cpkt_sqlite_native(self), database_name,
                                operation, &native_value);
  if (status == SQLITE_OK)
    *(cpkt_sqlite_i64 *)argument = cpkt_sqlite_public_i64(native_value);
  return status;
}

int cpkt_sqlite_file_control_int(cpkt_sqlite *self, const char *database_name,
                                 int operation, int *value) {
  if (value == NULL)
    return SQLITE_MISUSE;
  switch (operation) {
  case SQLITE_FCNTL_LOCKSTATE:
  case SQLITE_FCNTL_CHUNK_SIZE:
  case SQLITE_FCNTL_PERSIST_WAL:
  case SQLITE_FCNTL_POWERSAFE_OVERWRITE:
  case SQLITE_FCNTL_HAS_MOVED:
  case SQLITE_FCNTL_LOCK_TIMEOUT:
  case SQLITE_FCNTL_LAST_ERRNO:
  case SQLITE_FCNTL_RESERVE_BYTES:
  case SQLITE_FCNTL_EXTERNAL_READER:
    break;
  default:
    return SQLITE_MISUSE;
  }
  return cpkt_sqlite_file_control(self, database_name, operation, value);
}

int cpkt_sqlite_file_control_unsigned(cpkt_sqlite *self,
                                      const char *database_name, int operation,
                                      unsigned long *value) {
  unsigned int native_value;
  int status;
  if (value == NULL || operation != SQLITE_FCNTL_DATA_VERSION)
    return SQLITE_MISUSE;
  native_value = 0;
  status =
      cpkt_sqlite_file_control(self, database_name, operation, &native_value);
  if (status == SQLITE_OK)
    *value = (unsigned long)native_value;
  return status;
}

int cpkt_sqlite_file_control_text(cpkt_sqlite *self, const char *database_name,
                                  int operation, char **text_out) {
  const char *borrowed;
  int status;
  if (text_out == NULL || (operation != SQLITE_FCNTL_VFSNAME &&
                           operation != SQLITE_FCNTL_TEMPFILENAME &&
                           operation != SQLITE_FCNTL_GET_LOCKPROXYFILE))
    return SQLITE_MISUSE;
  *text_out = NULL;
  if (operation == SQLITE_FCNTL_GET_LOCKPROXYFILE) {
    borrowed = NULL;
    status =
        cpkt_sqlite_file_control(self, database_name, operation, &borrowed);
    if (status != SQLITE_OK || borrowed == NULL)
      return status;
    /* Native proxy locking returns provider-owned or static text. */
    *text_out = sqlite3_mprintf("%s", borrowed);
    return *text_out == NULL ? SQLITE_NOMEM : SQLITE_OK;
  }
  return cpkt_sqlite_file_control(self, database_name, operation, text_out);
}

int cpkt_sqlite_file_control_text_input(cpkt_sqlite *self,
                                        const char *database_name,
                                        int operation, const char *text) {
  if (text == NULL || (operation != SQLITE_FCNTL_TRACE &&
                       operation != SQLITE_FCNTL_SET_LOCKPROXYFILE))
    return SQLITE_MISUSE;
  return cpkt_sqlite_file_control(self, database_name, operation, (void *)text);
}

int cpkt_sqlite_file_control_none(cpkt_sqlite *self, const char *database_name,
                                  int operation) {
  if (operation != SQLITE_FCNTL_RESET_CACHE &&
      operation != SQLITE_FCNTL_BEGIN_ATOMIC_WRITE &&
      operation != SQLITE_FCNTL_COMMIT_ATOMIC_WRITE &&
      operation != SQLITE_FCNTL_ROLLBACK_ATOMIC_WRITE)
    return SQLITE_MISUSE;
  return cpkt_sqlite_file_control(self, database_name, operation, NULL);
}

int cpkt_sqlite_file_control_file(cpkt_sqlite *self, const char *database_name,
                                  int operation,
                                  cpkt_sqlite_file_handle **out) {
  sqlite3_file *native;
  cpkt_sqlite_file_handle *handle;
  int status;
  if (out != NULL)
    *out = NULL;
  if (out == NULL || (operation != SQLITE_FCNTL_FILE_POINTER &&
                      operation != SQLITE_FCNTL_JOURNAL_POINTER))
    return SQLITE_MISUSE;
  native = NULL;
  status = cpkt_sqlite_file_control(self, database_name, operation, &native);
  if (status != SQLITE_OK)
    return status;
  if (native == NULL || native->pMethods == NULL)
    return SQLITE_NOTFOUND;
  handle = (cpkt_sqlite_file_handle *)calloc(1, sizeof(*handle));
  if (handle == NULL)
    return SQLITE_NOMEM;
  handle->native = native;
  handle->view = cpkt_sqlite_native_file_view(native);
  if (handle->view == NULL) {
    free(handle);
    return SQLITE_NOMEM;
  }
  handle->owns_view = 1;
  *out = handle;
  return SQLITE_OK;
}

int cpkt_sqlite_file_control_vfs(cpkt_sqlite *self, const char *database_name,
                                 cpkt_sqlite_vfs_handle **out) {
  sqlite3_vfs *native;
  cpkt_sqlite_vfs_handle *handle;
  int status;
  if (out != NULL)
    *out = NULL;
  if (out == NULL)
    return SQLITE_MISUSE;
  native = NULL;
  status = cpkt_sqlite_file_control(self, database_name,
                                    SQLITE_FCNTL_VFS_POINTER, &native);
  if (status != SQLITE_OK)
    return status;
  if (native == NULL)
    return SQLITE_NOTFOUND;
  handle = (cpkt_sqlite_vfs_handle *)calloc(1, sizeof(*handle));
  if (handle == NULL)
    return SQLITE_NOMEM;
  handle->native = native;
  handle->view = cpkt_sqlite_native_vfs_view(native);
  if (handle->view == NULL) {
    free(handle);
    return SQLITE_NOMEM;
  }
  handle->owns_view = 1;
  *out = handle;
  return SQLITE_OK;
}

int cpkt_sqlite_file_control_filestat(cpkt_sqlite *self,
                                      const char *database_name,
                                      cpkt_sqlite_string *output) {
  if (output == NULL || output->string == NULL)
    return SQLITE_MISUSE;
  return cpkt_sqlite_file_control(self, database_name, SQLITE_FCNTL_FILESTAT,
                                  output->string);
}

int cpkt_sqlite_file_handle_read(cpkt_sqlite_file_handle *self, void *buffer,
                                 int byte_count, cpkt_sqlite_i64 offset) {
  if (self == NULL || self->native == NULL || buffer == NULL ||
      byte_count < 0 || self->native->pMethods->xRead == NULL)
    return SQLITE_MISUSE;
  return self->native->pMethods->xRead(self->native, buffer, byte_count,
                                       cpkt_sqlite_native_i64(offset));
}

int cpkt_sqlite_file_handle_write(cpkt_sqlite_file_handle *self,
                                  const void *buffer, int byte_count,
                                  cpkt_sqlite_i64 offset) {
  if (self == NULL || self->native == NULL || buffer == NULL ||
      byte_count < 0 || self->native->pMethods->xWrite == NULL)
    return SQLITE_MISUSE;
  return self->native->pMethods->xWrite(self->native, buffer, byte_count,
                                        cpkt_sqlite_native_i64(offset));
}

int cpkt_sqlite_file_handle_size(cpkt_sqlite_file_handle *self,
                                 cpkt_sqlite_i64 *out) {
  sqlite3_int64 native_value;
  int status;
  if (self == NULL || self->native == NULL || out == NULL ||
      self->native->pMethods->xFileSize == NULL)
    return SQLITE_MISUSE;
  status = self->native->pMethods->xFileSize(self->native, &native_value);
  if (status == SQLITE_OK)
    *out = cpkt_sqlite_public_i64(native_value);
  return status;
}

int cpkt_sqlite_file_handle_sync(cpkt_sqlite_file_handle *self, int flags) {
  if (self == NULL || self->native == NULL ||
      self->native->pMethods->xSync == NULL)
    return SQLITE_MISUSE;
  return self->native->pMethods->xSync(self->native, flags);
}

cpkt_sqlite_file *cpkt_sqlite_file_handle_view(cpkt_sqlite_file_handle *self) {
  return self == NULL ? NULL : self->view;
}

void cpkt_sqlite_file_handle_close(cpkt_sqlite_file_handle *self) {
  if (self == NULL)
    return;
  if (self->owns_view && self->view != NULL)
    cpkt_sqlite_native_file_view_release(self->view);
  free(self);
}

const char *cpkt_sqlite_vfs_handle_name(const cpkt_sqlite_vfs_handle *self) {
  return self == NULL || self->native == NULL ? NULL : self->native->zName;
}

int cpkt_sqlite_vfs_handle_version(const cpkt_sqlite_vfs_handle *self) {
  return self == NULL || self->native == NULL ? 0 : self->native->iVersion;
}

int cpkt_sqlite_vfs_handle_access(cpkt_sqlite_vfs_handle *self,
                                  const char *path, int flags, int *out) {
  if (self == NULL || self->native == NULL || path == NULL || out == NULL ||
      self->native->xAccess == NULL)
    return SQLITE_MISUSE;
  return self->native->xAccess(self->native, path, flags, out);
}

cpkt_sqlite_vfs *cpkt_sqlite_vfs_handle_view(cpkt_sqlite_vfs_handle *self) {
  return self == NULL ? NULL : self->view;
}

void cpkt_sqlite_vfs_handle_close(cpkt_sqlite_vfs_handle *self) {
  if (self == NULL)
    return;
  if (self->owns_view && self->view != NULL)
    self->view->close(self->view);
  free(self);
}

int cpkt_sqlite_set_lock_timeout(cpkt_sqlite *self, int milliseconds,
                                 unsigned long flags) {
  if (self == NULL || cpkt_sqlite_native(self) == NULL ||
      flags > 0xffffffffUL) {
    return SQLITE_MISUSE;
  }
  return sqlite3_setlk_timeout(cpkt_sqlite_native(self), milliseconds,
                               (int)flags);
}

int cpkt_sqlite_status_database(const cpkt_sqlite *self, int category,
                                int *current, int *highwater, int reset) {
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return CPKT_SQLITE_MISUSE;
  return sqlite3_db_status(cpkt_sqlite_native(self), category, current,
                           highwater, reset);
}

int cpkt_sqlite_status_database_i64(const cpkt_sqlite *self, int category,
                                    cpkt_sqlite_i64 *current,
                                    cpkt_sqlite_i64 *highwater, int reset) {
  sqlite3_int64 native_current;
  sqlite3_int64 native_highwater;
  int status;
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return CPKT_SQLITE_MISUSE;
  native_current = 0;
  native_highwater = 0;
  status = sqlite3_db_status64(cpkt_sqlite_native(self), category,
                               &native_current, &native_highwater, reset);
  if (current != NULL)
    *current = cpkt_sqlite_public_i64(native_current);
  if (highwater != NULL)
    *highwater = cpkt_sqlite_public_i64(native_highwater);
  return status;
}

int cpkt_sqlite_system_error(const cpkt_sqlite *self) {
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return 0;
  return sqlite3_system_errno(cpkt_sqlite_native(self));
}

int cpkt_sqlite_error_offset(const cpkt_sqlite *self) {
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return -1;
  return sqlite3_error_offset(cpkt_sqlite_native(self));
}

int cpkt_sqlite_set_busy_handler(cpkt_sqlite *self,
                                 cpkt_sqlite_busy_callback callback,
                                 void *context) {
  cpkt_sqlite_state *state;
  sqlite3_mutex *mutex;
  int status;
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return CPKT_SQLITE_MISUSE;
  state = cpkt_sqlite_state_for(self);
  if (state == NULL)
    return CPKT_SQLITE_NOMEM;
  mutex = cpkt_sqlite_connection_lock(self);
  state->busy_callback = callback;
  state->busy_context = context;
  status = sqlite3_busy_handler(cpkt_sqlite_native(self),
                                callback == NULL ? NULL
                                                 : cpkt_sqlite_busy_trampoline,
                                callback == NULL ? NULL : state);
  cpkt_sqlite_connection_unlock(mutex);
  return status;
}

int cpkt_sqlite_set_authorizer(cpkt_sqlite *self,
                               cpkt_sqlite_authorizer_callback callback,
                               void *context) {
  cpkt_sqlite_state *state;
  sqlite3_mutex *mutex;
  int status;
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return CPKT_SQLITE_MISUSE;
  state = cpkt_sqlite_state_for(self);
  if (state == NULL)
    return CPKT_SQLITE_NOMEM;
  mutex = cpkt_sqlite_connection_lock(self);
  state->authorizer_callback = callback;
  state->authorizer_context = context;
  status = sqlite3_set_authorizer(
      cpkt_sqlite_native(self),
      callback == NULL ? NULL : cpkt_sqlite_authorizer_trampoline,
      callback == NULL ? NULL : state);
  cpkt_sqlite_connection_unlock(mutex);
  return status;
}

int cpkt_sqlite_set_collation_needed(
    cpkt_sqlite *self, cpkt_sqlite_collation_needed_callback callback,
    void *context) {
  cpkt_sqlite_state *state;
  sqlite3_mutex *mutex;
  int status;
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return SQLITE_MISUSE;
  state = cpkt_sqlite_state_for(self);
  if (state == NULL)
    return SQLITE_NOMEM;
  mutex = cpkt_sqlite_connection_lock(self);
  state->collation_needed_callback = callback;
  state->collation_needed16_callback = NULL;
  state->collation_needed_context = context;
  status = sqlite3_collation_needed(
      cpkt_sqlite_native(self), callback == NULL ? NULL : state,
      callback == NULL ? NULL : cpkt_sqlite_collation_needed_trampoline);
  cpkt_sqlite_connection_unlock(mutex);
  return status;
}

int cpkt_sqlite_set_collation_needed16(
    cpkt_sqlite *self, cpkt_sqlite_collation_needed16_callback callback,
    void *context) {
  cpkt_sqlite_state *state;
  sqlite3_mutex *mutex;
  int status;
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return SQLITE_MISUSE;
  state = cpkt_sqlite_state_for(self);
  if (state == NULL)
    return SQLITE_NOMEM;
  mutex = cpkt_sqlite_connection_lock(self);
  state->collation_needed_callback = NULL;
  state->collation_needed16_callback = callback;
  state->collation_needed_context = context;
  status = sqlite3_collation_needed16(
      cpkt_sqlite_native(self), callback == NULL ? NULL : state,
      callback == NULL ? NULL : cpkt_sqlite_collation_needed16_trampoline);
  cpkt_sqlite_connection_unlock(mutex);
  return status;
}

int cpkt_sqlite_set_autovacuum_callback(
    cpkt_sqlite *self, cpkt_sqlite_autovacuum_callback callback, void *context,
    cpkt_sqlite_destroy_callback destroy) {
  cpkt_sqlite_autovacuum_binding *binding;
  int status;
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return SQLITE_MISUSE;
  if (callback == NULL) {
    if (context != NULL || destroy != NULL)
      return SQLITE_MISUSE;
    return sqlite3_autovacuum_pages(cpkt_sqlite_native(self), NULL, NULL, NULL);
  }
  binding = (cpkt_sqlite_autovacuum_binding *)calloc(1, sizeof(*binding));
  if (binding == NULL)
    return SQLITE_NOMEM;
  binding->context = context;
  binding->callback = callback;
  binding->destroy = destroy;
  status = sqlite3_autovacuum_pages(cpkt_sqlite_native(self),
                                    cpkt_sqlite_autovacuum_trampoline, binding,
                                    cpkt_sqlite_autovacuum_destroy);
  if (status != SQLITE_OK)
    cpkt_sqlite_autovacuum_destroy(binding);
  return status;
}

void *cpkt_sqlite_client_data(const cpkt_sqlite *self, const char *name) {
  if (self == NULL || cpkt_sqlite_native(self) == NULL || name == NULL)
    return NULL;
  return sqlite3_get_clientdata(cpkt_sqlite_native(self), name);
}

int cpkt_sqlite_set_client_data(cpkt_sqlite *self, const char *name, void *data,
                                cpkt_sqlite_destroy_callback destroy) {
  if (self == NULL || cpkt_sqlite_native(self) == NULL || name == NULL) {
    return SQLITE_MISUSE;
  }
  return sqlite3_set_clientdata(cpkt_sqlite_native(self), name, data, destroy);
}

int cpkt_sqlite_set_trace(cpkt_sqlite *self, unsigned long mask,
                          cpkt_sqlite_trace_callback callback, void *context) {
  cpkt_sqlite_state *state;
  sqlite3_mutex *mutex;
  int status;
  if (self == NULL || cpkt_sqlite_native(self) == NULL || mask > 0xffffffffUL) {
    return SQLITE_MISUSE;
  }
  state = cpkt_sqlite_state_for(self);
  if (state == NULL)
    return SQLITE_NOMEM;
  mutex = cpkt_sqlite_connection_lock(self);
  state->trace_callback = callback;
  state->trace_context = context;
  state->legacy_trace_callback = NULL;
  state->legacy_trace_context = NULL;
  state->legacy_profile_callback = NULL;
  state->legacy_profile_context = NULL;
  status =
      sqlite3_trace_v2(cpkt_sqlite_native(self), (unsigned int)mask,
                       callback == NULL ? NULL : cpkt_sqlite_trace_trampoline,
                       callback == NULL ? NULL : state);
  cpkt_sqlite_connection_unlock(mutex);
  return status;
}

void *cpkt_sqlite_set_legacy_trace(cpkt_sqlite *self,
                                   cpkt_sqlite_legacy_trace_callback callback,
                                   void *context) {
  cpkt_sqlite_state *state;
  sqlite3_mutex *mutex;
  void *previous;
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return NULL;
  state = cpkt_sqlite_state_for(self);
  if (state == NULL)
    return NULL;
  mutex = cpkt_sqlite_connection_lock(self);
  previous = state->trace_callback != NULL
                 ? state->trace_context
                 : (state->legacy_trace_callback != NULL
                        ? state->legacy_trace_context
                        : state->legacy_profile_context);
  state->trace_callback = NULL;
  state->trace_context = NULL;
  state->legacy_profile_callback = NULL;
  state->legacy_profile_context = NULL;
  state->legacy_trace_callback = callback;
  state->legacy_trace_context = context;
  (void)sqlite3_trace(cpkt_sqlite_native(self),
                      callback == NULL ? NULL
                                       : cpkt_sqlite_legacy_trace_trampoline,
                      callback == NULL ? NULL : state);
  cpkt_sqlite_connection_unlock(mutex);
  return previous;
}

void *
cpkt_sqlite_set_legacy_profile(cpkt_sqlite *self,
                               cpkt_sqlite_legacy_profile_callback callback,
                               void *context) {
  cpkt_sqlite_state *state;
  sqlite3_mutex *mutex;
  void *previous;
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return NULL;
  state = cpkt_sqlite_state_for(self);
  if (state == NULL)
    return NULL;
  mutex = cpkt_sqlite_connection_lock(self);
  previous = state->legacy_profile_context;
  state->legacy_trace_callback = NULL;
  state->legacy_trace_context = NULL;
  state->legacy_profile_callback = callback;
  state->legacy_profile_context = context;
  (void)sqlite3_profile(
      cpkt_sqlite_native(self),
      callback == NULL ? NULL : cpkt_sqlite_legacy_profile_trampoline,
      callback == NULL ? NULL : state);
  cpkt_sqlite_connection_unlock(mutex);
  return previous;
}

int cpkt_sqlite_register_rtree_geometry(
    cpkt_sqlite *self, const char *name,
    cpkt_sqlite_rtree_geometry_callback callback, void *context) {
  cpkt_sqlite_state *state;
  cpkt_sqlite_rtree_geometry_binding *binding;
  sqlite3_mutex *mutex;
  int status;
  if (self == NULL || cpkt_sqlite_native(self) == NULL || name == NULL ||
      callback == NULL)
    return SQLITE_MISUSE;
  state = cpkt_sqlite_state_for(self);
  if (state == NULL)
    return SQLITE_NOMEM;
  binding = (cpkt_sqlite_rtree_geometry_binding *)calloc(1, sizeof(*binding));
  if (binding == NULL)
    return SQLITE_NOMEM;
  binding->context = context;
  binding->callback = callback;
  mutex = cpkt_sqlite_connection_lock(self);
  status = sqlite3_rtree_geometry_callback(
      cpkt_sqlite_native(self), name, cpkt_sqlite_rtree_geometry_trampoline,
      binding);
  if (status != SQLITE_OK) {
    cpkt_sqlite_connection_unlock(mutex);
    free(binding);
    return status;
  }
  binding->next = state->rtree_geometry_bindings;
  state->rtree_geometry_bindings = binding;
  cpkt_sqlite_connection_unlock(mutex);
  return SQLITE_OK;
}

int cpkt_sqlite_register_rtree_query(cpkt_sqlite *self, const char *name,
                                     cpkt_sqlite_rtree_query_callback callback,
                                     void *context,
                                     cpkt_sqlite_destroy_callback destroy) {
  cpkt_sqlite_rtree_query_binding *binding;
  int status;
  if (self == NULL || cpkt_sqlite_native(self) == NULL || name == NULL ||
      callback == NULL)
    return SQLITE_MISUSE;
  binding = (cpkt_sqlite_rtree_query_binding *)calloc(1, sizeof(*binding));
  if (binding == NULL)
    return SQLITE_NOMEM;
  binding->context = context;
  binding->callback = callback;
  binding->destroy = destroy;
  status = sqlite3_rtree_query_callback(
      cpkt_sqlite_native(self), name, cpkt_sqlite_rtree_query_trampoline,
      binding, cpkt_sqlite_rtree_query_destroy);
  /* SQLite invokes the supplied destructor even when registration fails. */
  return status;
}

void cpkt_sqlite_set_progress_handler(cpkt_sqlite *self, int instruction_count,
                                      cpkt_sqlite_progress_callback callback,
                                      void *context) {
  cpkt_sqlite_state *state;
  sqlite3_mutex *mutex;
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return;
  state = cpkt_sqlite_state_for(self);
  if (state == NULL)
    return;
  mutex = cpkt_sqlite_connection_lock(self);
  state->progress_callback = callback;
  state->progress_context = context;
  sqlite3_progress_handler(cpkt_sqlite_native(self), instruction_count,
                           callback == NULL ? NULL
                                            : cpkt_sqlite_progress_trampoline,
                           callback == NULL ? NULL : state);
  cpkt_sqlite_connection_unlock(mutex);
}

void cpkt_sqlite_set_commit_hook(cpkt_sqlite *self,
                                 cpkt_sqlite_commit_callback callback,
                                 void *context) {
  cpkt_sqlite_state *state;
  sqlite3_mutex *mutex;
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return;
  state = cpkt_sqlite_state_for(self);
  if (state == NULL)
    return;
  mutex = cpkt_sqlite_connection_lock(self);
  state->commit_callback = callback;
  state->commit_context = context;
  sqlite3_commit_hook(cpkt_sqlite_native(self),
                      callback == NULL ? NULL : cpkt_sqlite_commit_trampoline,
                      callback == NULL ? NULL : state);
  cpkt_sqlite_connection_unlock(mutex);
}

void cpkt_sqlite_set_rollback_hook(cpkt_sqlite *self,
                                   cpkt_sqlite_rollback_callback callback,
                                   void *context) {
  cpkt_sqlite_state *state;
  sqlite3_mutex *mutex;
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return;
  state = cpkt_sqlite_state_for(self);
  if (state == NULL)
    return;
  mutex = cpkt_sqlite_connection_lock(self);
  state->rollback_callback = callback;
  state->rollback_context = context;
  sqlite3_rollback_hook(cpkt_sqlite_native(self),
                        callback == NULL ? NULL
                                         : cpkt_sqlite_rollback_trampoline,
                        callback == NULL ? NULL : state);
  cpkt_sqlite_connection_unlock(mutex);
}

void cpkt_sqlite_set_update_hook(cpkt_sqlite *self,
                                 cpkt_sqlite_update_callback callback,
                                 void *context) {
  cpkt_sqlite_state *state;
  sqlite3_mutex *mutex;
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return;
  state = cpkt_sqlite_state_for(self);
  if (state == NULL)
    return;
  mutex = cpkt_sqlite_connection_lock(self);
  state->update_callback = callback;
  state->update_context = context;
  sqlite3_update_hook(cpkt_sqlite_native(self),
                      callback == NULL ? NULL : cpkt_sqlite_update_trampoline,
                      callback == NULL ? NULL : state);
  cpkt_sqlite_connection_unlock(mutex);
}

void cpkt_sqlite_set_wal_hook(cpkt_sqlite *self,
                              cpkt_sqlite_wal_callback callback,
                              void *context) {
  cpkt_sqlite_state *state;
  sqlite3_mutex *mutex;
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return;
  state = cpkt_sqlite_state_for(self);
  if (state == NULL)
    return;
  mutex = cpkt_sqlite_connection_lock(self);
  state->wal_callback = callback;
  state->wal_context = context;
  sqlite3_wal_hook(cpkt_sqlite_native(self),
                   callback == NULL ? NULL : cpkt_sqlite_wal_trampoline,
                   callback == NULL ? NULL : state);
  cpkt_sqlite_connection_unlock(mutex);
}

int cpkt_sqlite_set_preupdate_hook(cpkt_sqlite *self,
                                   cpkt_sqlite_preupdate_callback callback,
                                   void *context) {
  cpkt_sqlite_state *state;
  sqlite3_mutex *mutex;
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return CPKT_SQLITE_MISUSE;
  state = cpkt_sqlite_state_for(self);
  if (state == NULL)
    return CPKT_SQLITE_NOMEM;
  mutex = cpkt_sqlite_connection_lock(self);
  if (state->session_count != 0) {
    cpkt_sqlite_connection_unlock(mutex);
    return CPKT_SQLITE_MISUSE;
  }
  state->preupdate_callback = callback;
  state->preupdate_context = context;
  sqlite3_preupdate_hook(cpkt_sqlite_native(self),
                         callback == NULL ? NULL
                                          : cpkt_sqlite_preupdate_trampoline,
                         callback == NULL ? NULL : state);
  cpkt_sqlite_connection_unlock(mutex);
  return CPKT_SQLITE_OK;
}

int cpkt_sqlite_preupdate_count(const cpkt_sqlite *self) {
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return CPKT_SQLITE_MISUSE;
  return sqlite3_preupdate_count(cpkt_sqlite_native(self));
}

int cpkt_sqlite_preupdate_depth(const cpkt_sqlite *self) {
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return CPKT_SQLITE_MISUSE;
  return sqlite3_preupdate_depth(cpkt_sqlite_native(self));
}

int cpkt_sqlite_preupdate_blob_write(const cpkt_sqlite *self) {
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return CPKT_SQLITE_MISUSE;
  return sqlite3_preupdate_blobwrite(cpkt_sqlite_native(self));
}

static int cpkt_sqlite_preupdate_value(cpkt_sqlite *self, int column,
                                       cpkt_sqlite_value **out, int old_value) {
  sqlite3_value *native_value;
  cpkt_sqlite_value *public_value;
  int status;
  if (out != NULL)
    *out = NULL;
  if (self == NULL || cpkt_sqlite_native(self) == NULL || out == NULL)
    return CPKT_SQLITE_MISUSE;
  native_value = NULL;
  status = old_value ? sqlite3_preupdate_old(cpkt_sqlite_native(self), column,
                                             &native_value)
                     : sqlite3_preupdate_new(cpkt_sqlite_native(self), column,
                                             &native_value);
  if (status != SQLITE_OK || native_value == NULL)
    return status;
  public_value = (cpkt_sqlite_value *)calloc(1, sizeof(*public_value));
  if (public_value == NULL)
    return CPKT_SQLITE_NOMEM;
  public_value->value = native_value;
  public_value->owned = 0;
  public_value->shell_owned = 1;
  *out = public_value;
  return CPKT_SQLITE_OK;
}

int cpkt_sqlite_preupdate_old(cpkt_sqlite *self, int column,
                              cpkt_sqlite_value **out) {
  return cpkt_sqlite_preupdate_value(self, column, out, 1);
}

int cpkt_sqlite_preupdate_new(cpkt_sqlite *self, int column,
                              cpkt_sqlite_value **out) {
  return cpkt_sqlite_preupdate_value(self, column, out, 0);
}

static int cpkt_sqlite_blob_read(cpkt_sqlite_blob *self, void *buffer,
                                 int byte_count, int offset) {
  if (cpkt_sqlite_native_blob(self) == NULL)
    return CPKT_SQLITE_MISUSE;
  return sqlite3_blob_read(cpkt_sqlite_native_blob(self), buffer, byte_count,
                           offset);
}

static int cpkt_sqlite_blob_write(cpkt_sqlite_blob *self, const void *buffer,
                                  int byte_count, int offset) {
  if (cpkt_sqlite_native_blob(self) == NULL)
    return CPKT_SQLITE_MISUSE;
  return sqlite3_blob_write(cpkt_sqlite_native_blob(self), buffer, byte_count,
                            offset);
}

static int cpkt_sqlite_blob_reopen(cpkt_sqlite_blob *self,
                                   cpkt_sqlite_i64 row_id) {
  if (cpkt_sqlite_native_blob(self) == NULL)
    return CPKT_SQLITE_MISUSE;
  return sqlite3_blob_reopen(cpkt_sqlite_native_blob(self),
                             cpkt_sqlite_native_i64(row_id));
}

static int cpkt_sqlite_blob_bytes(const cpkt_sqlite_blob *self) {
  if (cpkt_sqlite_native_blob(self) == NULL)
    return 0;
  return sqlite3_blob_bytes(cpkt_sqlite_native_blob(self));
}

static int cpkt_sqlite_blob_close(cpkt_sqlite_blob *self) {
  cpkt_sqlite *database;
  int status;
  if (self == NULL)
    return CPKT_SQLITE_MISUSE;
  database = self->database;
  status = cpkt_sqlite_native_blob(self) == NULL
               ? CPKT_SQLITE_OK
               : sqlite3_blob_close(cpkt_sqlite_native_blob(self));
  self->blob = NULL;
  self->database = NULL;
  free(self);
  cpkt_sqlite_release_child(database);
  return status;
}

int cpkt_sqlite_open_blob(cpkt_sqlite *self, const char *database_name,
                          const char *table_name, const char *column_name,
                          cpkt_sqlite_i64 row_id, int writable,
                          cpkt_sqlite_blob **out) {
  sqlite3_blob *native_blob;
  cpkt_sqlite_blob *public_blob;
  int status;
  if (out != NULL)
    *out = NULL;
  if (self == NULL || cpkt_sqlite_native(self) == NULL ||
      database_name == NULL || table_name == NULL || column_name == NULL ||
      out == NULL)
    return CPKT_SQLITE_MISUSE;
  native_blob = NULL;
  status = sqlite3_blob_open(
      cpkt_sqlite_native(self), database_name, table_name, column_name,
      cpkt_sqlite_native_i64(row_id), writable, &native_blob);
  if (status != SQLITE_OK)
    return status;
  public_blob = (cpkt_sqlite_blob *)calloc(1, sizeof(*public_blob));
  if (public_blob == NULL) {
    sqlite3_blob_close(native_blob);
    return CPKT_SQLITE_NOMEM;
  }
  public_blob->read = cpkt_sqlite_blob_read;
  public_blob->write = cpkt_sqlite_blob_write;
  public_blob->reopen = cpkt_sqlite_blob_reopen;
  public_blob->bytes = cpkt_sqlite_blob_bytes;
  public_blob->close = cpkt_sqlite_blob_close;
  public_blob->blob = native_blob;
  public_blob->database = self;
  cpkt_sqlite_retain_child(self);
  *out = public_blob;
  return CPKT_SQLITE_OK;
}

static int cpkt_sqlite_backup_step(cpkt_sqlite_backup *self, int page_count) {
  if (cpkt_sqlite_native_backup(self) == NULL)
    return CPKT_SQLITE_MISUSE;
  return sqlite3_backup_step(cpkt_sqlite_native_backup(self), page_count);
}

static int cpkt_sqlite_backup_remaining(const cpkt_sqlite_backup *self) {
  if (cpkt_sqlite_native_backup(self) == NULL)
    return 0;
  return sqlite3_backup_remaining(cpkt_sqlite_native_backup(self));
}

static int cpkt_sqlite_backup_page_count(const cpkt_sqlite_backup *self) {
  if (cpkt_sqlite_native_backup(self) == NULL)
    return 0;
  return sqlite3_backup_pagecount(cpkt_sqlite_native_backup(self));
}

static int cpkt_sqlite_backup_close(cpkt_sqlite_backup *self) {
  cpkt_sqlite *destination;
  cpkt_sqlite *source;
  int status;
  if (self == NULL)
    return CPKT_SQLITE_MISUSE;
  destination = self->destination;
  source = self->source;
  status = cpkt_sqlite_native_backup(self) == NULL
               ? CPKT_SQLITE_OK
               : sqlite3_backup_finish(cpkt_sqlite_native_backup(self));
  self->backup = NULL;
  self->destination = NULL;
  self->source = NULL;
  free(self);
  cpkt_sqlite_release_child(destination);
  cpkt_sqlite_release_child(source);
  return status;
}

int cpkt_sqlite_backup_start(cpkt_sqlite *destination,
                             const char *destination_name, cpkt_sqlite *source,
                             const char *source_name,
                             cpkt_sqlite_backup **out) {
  sqlite3_backup *native_backup;
  cpkt_sqlite_backup *public_backup;
  if (out != NULL)
    *out = NULL;
  if (destination == NULL || source == NULL || destination_name == NULL ||
      source_name == NULL || out == NULL)
    return CPKT_SQLITE_MISUSE;
  native_backup =
      sqlite3_backup_init(cpkt_sqlite_native(destination), destination_name,
                          cpkt_sqlite_native(source), source_name);
  if (native_backup == NULL)
    return sqlite3_errcode(cpkt_sqlite_native(destination));
  public_backup = (cpkt_sqlite_backup *)calloc(1, sizeof(*public_backup));
  if (public_backup == NULL) {
    sqlite3_backup_finish(native_backup);
    return CPKT_SQLITE_NOMEM;
  }
  public_backup->step = cpkt_sqlite_backup_step;
  public_backup->remaining = cpkt_sqlite_backup_remaining;
  public_backup->page_count = cpkt_sqlite_backup_page_count;
  public_backup->close = cpkt_sqlite_backup_close;
  public_backup->backup = native_backup;
  public_backup->destination = destination;
  public_backup->source = source;
  cpkt_sqlite_retain_child(destination);
  cpkt_sqlite_retain_child(source);
  *out = public_backup;
  return CPKT_SQLITE_OK;
}

int cpkt_sqlite_snapshot_get(cpkt_sqlite *self, const char *schema,
                             cpkt_sqlite_snapshot **out) {
  sqlite3_snapshot *native_snapshot;
  cpkt_sqlite_snapshot *public_snapshot;
  int status;
  if (out != NULL)
    *out = NULL;
  if (self == NULL || cpkt_sqlite_native(self) == NULL || schema == NULL ||
      out == NULL) {
    return CPKT_SQLITE_MISUSE;
  }
  native_snapshot = NULL;
  status =
      sqlite3_snapshot_get(cpkt_sqlite_native(self), schema, &native_snapshot);
  if (status != SQLITE_OK)
    return status;
  public_snapshot = (cpkt_sqlite_snapshot *)calloc(1, sizeof(*public_snapshot));
  if (public_snapshot == NULL) {
    sqlite3_snapshot_free(native_snapshot);
    return CPKT_SQLITE_NOMEM;
  }
  public_snapshot->snapshot = native_snapshot;
  *out = public_snapshot;
  return CPKT_SQLITE_OK;
}

int cpkt_sqlite_snapshot_open(cpkt_sqlite *self, const char *schema,
                              cpkt_sqlite_snapshot *snapshot) {
  if (self == NULL || schema == NULL || snapshot == NULL ||
      snapshot->snapshot == NULL) {
    return CPKT_SQLITE_MISUSE;
  }
  return sqlite3_snapshot_open(cpkt_sqlite_native(self), schema,
                               (sqlite3_snapshot *)snapshot->snapshot);
}

int cpkt_sqlite_snapshot_compare(const cpkt_sqlite_snapshot *left,
                                 const cpkt_sqlite_snapshot *right) {
  if (left == NULL || right == NULL || left->snapshot == NULL ||
      right->snapshot == NULL) {
    return 0;
  }
  return sqlite3_snapshot_cmp((sqlite3_snapshot *)left->snapshot,
                              (sqlite3_snapshot *)right->snapshot);
}

void cpkt_sqlite_snapshot_free(cpkt_sqlite_snapshot *snapshot) {
  if (snapshot == NULL)
    return;
  if (snapshot->snapshot != NULL)
    sqlite3_snapshot_free((sqlite3_snapshot *)snapshot->snapshot);
  snapshot->snapshot = NULL;
  free(snapshot);
}

int cpkt_sqlite_snapshot_recover(cpkt_sqlite *self, const char *schema) {
  if (self == NULL || cpkt_sqlite_native(self) == NULL || schema == NULL) {
    return CPKT_SQLITE_MISUSE;
  }
  return sqlite3_snapshot_recover(cpkt_sqlite_native(self), schema);
}

unsigned char *cpkt_sqlite_serialize(cpkt_sqlite *self, const char *schema,
                                     cpkt_sqlite_i64 *byte_count,
                                     unsigned long flags) {
  sqlite3_int64 native_count;
  unsigned char *result;
  if (byte_count != NULL)
    *byte_count = cpkt_sqlite_i64_make(0, 0);
  if (self == NULL || cpkt_sqlite_native(self) == NULL)
    return NULL;
  native_count = 0;
  result = sqlite3_serialize(cpkt_sqlite_native(self), schema, &native_count,
                             (unsigned int)flags);
  if (byte_count != NULL)
    *byte_count = cpkt_sqlite_public_i64(native_count);
  return result;
}

int cpkt_sqlite_deserialize(cpkt_sqlite *self, const char *schema,
                            unsigned char *data, cpkt_sqlite_i64 database_size,
                            cpkt_sqlite_i64 buffer_size, unsigned long flags) {
  if (self == NULL || cpkt_sqlite_native(self) == NULL || data == NULL)
    return CPKT_SQLITE_MISUSE;
  return sqlite3_deserialize(cpkt_sqlite_native(self), schema, data,
                             cpkt_sqlite_native_i64(database_size),
                             cpkt_sqlite_native_i64(buffer_size),
                             (unsigned int)flags);
}

int cpkt_sqlite_create_function(
    cpkt_sqlite *self, const char *name, int argument_count,
    unsigned long text_representation, void *user_data,
    cpkt_sqlite_scalar_callback scalar, cpkt_sqlite_scalar_callback step,
    cpkt_sqlite_scalar_callback final, cpkt_sqlite_destroy_callback destroy) {
  cpkt_sqlite_function_binding *binding;
  int status;
  if (self == NULL || cpkt_sqlite_native(self) == NULL || name == NULL) {
    if (destroy != NULL)
      destroy(user_data);
    return CPKT_SQLITE_MISUSE;
  }
  if (scalar == NULL && step == NULL && final == NULL)
    return sqlite3_create_function_v2(cpkt_sqlite_native(self), name,
                                      argument_count, (int)text_representation,
                                      user_data, NULL, NULL, NULL, destroy);
  binding = (cpkt_sqlite_function_binding *)calloc(1, sizeof(*binding));
  if (binding == NULL) {
    if (destroy != NULL)
      destroy(user_data);
    return CPKT_SQLITE_NOMEM;
  }
  binding->database = self;
  binding->user_data = user_data;
  binding->scalar = scalar;
  binding->step = step;
  binding->final = final;
  binding->destroy = destroy;
  status = sqlite3_create_function_v2(
      cpkt_sqlite_native(self), name, argument_count, (int)text_representation,
      binding, scalar == NULL ? NULL : cpkt_sqlite_function_scalar_trampoline,
      step == NULL ? NULL : cpkt_sqlite_function_step_trampoline,
      final == NULL ? NULL : cpkt_sqlite_function_final_trampoline,
      cpkt_sqlite_function_destroy);
  if (status != SQLITE_OK) {
    /* The native routine calls the destructor on failure as documented. */
    return status;
  }
  return CPKT_SQLITE_OK;
}

extern int sqlite3__cpkt_create_function16_v2(
    sqlite3 *, const void *, int, int, void *,
    void (*)(sqlite3_context *, int, sqlite3_value **),
    void (*)(sqlite3_context *, int, sqlite3_value **),
    void (*)(sqlite3_context *), void (*)(void *));

int cpkt_sqlite_create_function16(cpkt_sqlite *self, const void *name,
                                  int argument_count,
                                  unsigned long text_representation,
                                  void *user_data,
                                  cpkt_sqlite_scalar_callback scalar,
                                  cpkt_sqlite_scalar_callback step,
                                  cpkt_sqlite_scalar_callback final) {
  cpkt_sqlite_function_binding *binding;
  int status;
  if (self == NULL || cpkt_sqlite_native(self) == NULL || name == NULL) {
    return CPKT_SQLITE_MISUSE;
  }
  if (scalar == NULL && step == NULL && final == NULL)
    return sqlite3__cpkt_create_function16_v2(
        cpkt_sqlite_native(self), name, argument_count,
        (int)text_representation, user_data, NULL, NULL, NULL, NULL);
  binding = (cpkt_sqlite_function_binding *)calloc(1, sizeof(*binding));
  if (binding == NULL)
    return CPKT_SQLITE_NOMEM;
  binding->database = self;
  binding->user_data = user_data;
  binding->scalar = scalar;
  binding->step = step;
  binding->final = final;
  status = sqlite3__cpkt_create_function16_v2(
      cpkt_sqlite_native(self), name, argument_count, (int)text_representation,
      binding, scalar == NULL ? NULL : cpkt_sqlite_function_scalar_trampoline,
      step == NULL ? NULL : cpkt_sqlite_function_step_trampoline,
      final == NULL ? NULL : cpkt_sqlite_function_final_trampoline,
      cpkt_sqlite_function_destroy);
  /* The native registration owns the binding, including failure cleanup. */
  return status;
}

int cpkt_sqlite_create_window_function(
    cpkt_sqlite *self, const char *name, int argument_count,
    unsigned long text_representation, void *user_data,
    cpkt_sqlite_scalar_callback step, cpkt_sqlite_scalar_callback final,
    cpkt_sqlite_scalar_callback value, cpkt_sqlite_scalar_callback inverse,
    cpkt_sqlite_destroy_callback destroy) {
  cpkt_sqlite_function_binding *binding;
  int status;
  if (self == NULL || cpkt_sqlite_native(self) == NULL || name == NULL ||
      (step == NULL) != (final == NULL) ||
      (value == NULL) != (inverse == NULL) || (step == NULL && value != NULL)) {
    if (destroy != NULL)
      destroy(user_data);
    return CPKT_SQLITE_MISUSE;
  }
  if (step == NULL)
    return sqlite3_create_window_function(
        cpkt_sqlite_native(self), name, argument_count,
        (int)text_representation, user_data, NULL, NULL, NULL, NULL, destroy);
  binding = (cpkt_sqlite_function_binding *)calloc(1, sizeof(*binding));
  if (binding == NULL) {
    if (destroy != NULL)
      destroy(user_data);
    return CPKT_SQLITE_NOMEM;
  }
  binding->database = self;
  binding->user_data = user_data;
  binding->step = step;
  binding->final = final;
  binding->value = value;
  binding->inverse = inverse;
  binding->destroy = destroy;
  status = sqlite3_create_window_function(
      cpkt_sqlite_native(self), name, argument_count, (int)text_representation,
      binding, cpkt_sqlite_function_step_trampoline,
      cpkt_sqlite_function_final_trampoline,
      value == NULL ? NULL : cpkt_sqlite_function_value_trampoline,
      inverse == NULL ? NULL : cpkt_sqlite_function_inverse_trampoline,
      cpkt_sqlite_function_destroy);
  if (status != SQLITE_OK)
    return status;
  return CPKT_SQLITE_OK;
}

int cpkt_sqlite_create_collation(cpkt_sqlite *self, const char *name,
                                 unsigned long text_representation,
                                 void *context,
                                 cpkt_sqlite_collation_callback compare,
                                 cpkt_sqlite_destroy_callback destroy) {
  cpkt_sqlite_collation_binding *binding;
  int status;
  if (self == NULL || cpkt_sqlite_native(self) == NULL || name == NULL) {
    return CPKT_SQLITE_MISUSE;
  }
  if (compare == NULL) {
    return sqlite3_create_collation_v2(cpkt_sqlite_native(self), name,
                                       (int)text_representation, NULL, NULL,
                                       NULL);
  }
  binding = (cpkt_sqlite_collation_binding *)calloc(1, sizeof(*binding));
  if (binding == NULL)
    return CPKT_SQLITE_NOMEM;
  binding->context = context;
  binding->compare = compare;
  binding->destroy = destroy;
  status = sqlite3_create_collation_v2(
      cpkt_sqlite_native(self), name, (int)text_representation, binding,
      cpkt_sqlite_collation_trampoline, cpkt_sqlite_collation_destroy);
  if (status != SQLITE_OK) {
    /* SQLite deliberately does not call xDestroy on this failure path. The
     * binding is facade-owned, while context remains caller-owned. */
    free(binding);
    return status;
  }
  return CPKT_SQLITE_OK;
}

int cpkt_sqlite_create_collation16(cpkt_sqlite *self, const void *name,
                                   unsigned long text_representation,
                                   void *context,
                                   cpkt_sqlite_collation_callback compare) {
  if (self == NULL || cpkt_sqlite_native(self) == NULL || name == NULL) {
    return CPKT_SQLITE_MISUSE;
  }
  return sqlite3_create_collation16(cpkt_sqlite_native(self), name,
                                    (int)text_representation, context, compare);
}

void *cpkt_sqlite_context_user_data(cpkt_sqlite_context *context) {
  cpkt_sqlite_function_binding *binding;
  cpkt_sqlite_fts5_sql_context_scope *scope;
  pthread_mutex_t *mutex;
  void *user_data;
  if (cpkt_sqlite_native_context(context) == NULL)
    return NULL;
  mutex = cpkt_sqlite_global_mutex();
  cpkt_sqlite_global_lock(mutex);
  scope = cpkt_sqlite_fts5_sql_context_head;
  while (scope != NULL) {
    if (scope->context == context) {
      user_data = scope->user_data;
      cpkt_sqlite_global_unlock(mutex);
      return user_data;
    }
    scope = scope->next;
  }
  cpkt_sqlite_global_unlock(mutex);
  binding = (cpkt_sqlite_function_binding *)sqlite3_user_data(
      cpkt_sqlite_native_context(context));
  return binding == NULL ? NULL : binding->user_data;
}

void *cpkt_sqlite_context_aggregate(cpkt_sqlite_context *context,
                                    int byte_count) {
  if (cpkt_sqlite_native_context(context) == NULL)
    return NULL;
  return sqlite3_aggregate_context(cpkt_sqlite_native_context(context),
                                   byte_count);
}

int cpkt_sqlite_context_aggregate_count(cpkt_sqlite_context *context) {
  if (cpkt_sqlite_native_context(context) == NULL)
    return 0;
  return sqlite3_aggregate_count(cpkt_sqlite_native_context(context));
}

cpkt_sqlite *cpkt_sqlite_context_database(cpkt_sqlite_context *context) {
  return context == NULL ? NULL : context->database;
}

void *cpkt_sqlite_context_auxdata(cpkt_sqlite_context *context,
                                  int argument_index) {
  cpkt_sqlite_auxdata_binding *binding;
  if (cpkt_sqlite_native_context(context) == NULL)
    return NULL;
  binding = (cpkt_sqlite_auxdata_binding *)sqlite3_get_auxdata(
      cpkt_sqlite_native_context(context), argument_index);
  return binding == NULL ? NULL : binding->data;
}

int cpkt_sqlite_context_set_auxdata(cpkt_sqlite_context *context,
                                    int argument_index, void *data,
                                    cpkt_sqlite_destroy_callback destroy) {
  cpkt_sqlite_auxdata_binding *binding;
  if (cpkt_sqlite_native_context(context) == NULL || argument_index < 0) {
    return CPKT_SQLITE_MISUSE;
  }
  binding = (cpkt_sqlite_auxdata_binding *)calloc(1, sizeof(*binding));
  if (binding == NULL)
    return CPKT_SQLITE_NOMEM;
  binding->data = data;
  binding->destroy = destroy;
  sqlite3_set_auxdata(cpkt_sqlite_native_context(context), argument_index,
                      binding, cpkt_sqlite_auxdata_destroy);
  return CPKT_SQLITE_OK;
}

void cpkt_sqlite_context_result_null(cpkt_sqlite_context *context) {
  if (cpkt_sqlite_native_context(context) != NULL)
    sqlite3_result_null(cpkt_sqlite_native_context(context));
}

void cpkt_sqlite_context_result_int(cpkt_sqlite_context *context, int value) {
  if (cpkt_sqlite_native_context(context) != NULL)
    sqlite3_result_int(cpkt_sqlite_native_context(context), value);
}

void cpkt_sqlite_context_result_i64(cpkt_sqlite_context *context,
                                    cpkt_sqlite_i64 value) {
  if (cpkt_sqlite_native_context(context) != NULL) {
    sqlite3_result_int64(cpkt_sqlite_native_context(context),
                         cpkt_sqlite_native_i64(value));
  }
}

void cpkt_sqlite_context_result_double(cpkt_sqlite_context *context,
                                       double value) {
  if (cpkt_sqlite_native_context(context) != NULL)
    sqlite3_result_double(cpkt_sqlite_native_context(context), value);
}

void cpkt_sqlite_context_result_text(cpkt_sqlite_context *context,
                                     const char *value, int byte_count) {
  if (cpkt_sqlite_native_context(context) != NULL) {
    sqlite3_result_text(cpkt_sqlite_native_context(context), value, byte_count,
                        SQLITE_TRANSIENT);
  }
}

void cpkt_sqlite_context_result_text16(cpkt_sqlite_context *context,
                                       const void *value, int byte_count) {
  if (cpkt_sqlite_native_context(context) != NULL) {
    sqlite3_result_text16(cpkt_sqlite_native_context(context), value,
                          byte_count, SQLITE_TRANSIENT);
  }
}

void cpkt_sqlite_context_result_text16le(cpkt_sqlite_context *context,
                                         const void *value, int byte_count) {
  if (cpkt_sqlite_native_context(context) != NULL) {
    sqlite3_result_text16le(cpkt_sqlite_native_context(context), value,
                            byte_count, SQLITE_TRANSIENT);
  }
}

void cpkt_sqlite_context_result_text16be(cpkt_sqlite_context *context,
                                         const void *value, int byte_count) {
  if (cpkt_sqlite_native_context(context) != NULL) {
    sqlite3_result_text16be(cpkt_sqlite_native_context(context), value,
                            byte_count, SQLITE_TRANSIENT);
  }
}

void cpkt_sqlite_context_result_text_u64(cpkt_sqlite_context *context,
                                         const char *value,
                                         cpkt_sqlite_u64 byte_count,
                                         unsigned long encoding) {
  if (cpkt_sqlite_native_context(context) != NULL) {
    sqlite3_result_text64(cpkt_sqlite_native_context(context), value,
                          cpkt_sqlite_native_u64(byte_count), SQLITE_TRANSIENT,
                          (unsigned char)encoding);
  }
}

void cpkt_sqlite_context_result_blob(cpkt_sqlite_context *context,
                                     const void *value, int byte_count) {
  if (cpkt_sqlite_native_context(context) != NULL) {
    sqlite3_result_blob(cpkt_sqlite_native_context(context), value, byte_count,
                        SQLITE_TRANSIENT);
  }
}

void cpkt_sqlite_context_result_blob_u64(cpkt_sqlite_context *context,
                                         const void *value,
                                         cpkt_sqlite_u64 byte_count) {
  if (cpkt_sqlite_native_context(context) != NULL) {
    sqlite3_result_blob64(cpkt_sqlite_native_context(context), value,
                          cpkt_sqlite_native_u64(byte_count), SQLITE_TRANSIENT);
  }
}

int cpkt_sqlite_context_result_zero_blob(cpkt_sqlite_context *context,
                                         int byte_count) {
  if (cpkt_sqlite_native_context(context) == NULL)
    return CPKT_SQLITE_MISUSE;
  sqlite3_result_zeroblob(cpkt_sqlite_native_context(context), byte_count);
  return CPKT_SQLITE_OK;
}

int cpkt_sqlite_context_result_zero_blob_u64(cpkt_sqlite_context *context,
                                             cpkt_sqlite_u64 byte_count) {
  if (cpkt_sqlite_native_context(context) == NULL)
    return CPKT_SQLITE_MISUSE;
  return sqlite3_result_zeroblob64(cpkt_sqlite_native_context(context),
                                   cpkt_sqlite_native_u64(byte_count));
}

void cpkt_sqlite_context_result_pointer(cpkt_sqlite_context *context,
                                        void *value, const char *type_name,
                                        cpkt_sqlite_destroy_callback destroy) {
  if (cpkt_sqlite_native_context(context) != NULL && type_name != NULL) {
    sqlite3_result_pointer(cpkt_sqlite_native_context(context), value,
                           type_name, destroy);
  }
}

void cpkt_sqlite_context_result_no_memory(cpkt_sqlite_context *context) {
  if (cpkt_sqlite_native_context(context) != NULL) {
    sqlite3_result_error_nomem(cpkt_sqlite_native_context(context));
  }
}

void cpkt_sqlite_context_result_too_big(cpkt_sqlite_context *context) {
  if (cpkt_sqlite_native_context(context) != NULL) {
    sqlite3_result_error_toobig(cpkt_sqlite_native_context(context));
  }
}

void cpkt_sqlite_context_result_error(cpkt_sqlite_context *context,
                                      const char *message, int byte_count) {
  if (cpkt_sqlite_native_context(context) != NULL) {
    sqlite3_result_error(cpkt_sqlite_native_context(context), message,
                         byte_count);
  }
}

void cpkt_sqlite_context_result_error16(cpkt_sqlite_context *context,
                                        const void *message, int byte_count) {
  if (cpkt_sqlite_native_context(context) != NULL) {
    sqlite3_result_error16(cpkt_sqlite_native_context(context), message,
                           byte_count);
  }
}

void cpkt_sqlite_context_result_error_code(cpkt_sqlite_context *context,
                                           int code) {
  if (cpkt_sqlite_native_context(context) != NULL)
    sqlite3_result_error_code(cpkt_sqlite_native_context(context), code);
}

void cpkt_sqlite_context_result_value(cpkt_sqlite_context *context,
                                      const cpkt_sqlite_value *value) {
  if (cpkt_sqlite_native_context(context) != NULL &&
      cpkt_sqlite_native_value(value) != NULL) {
    sqlite3_result_value(cpkt_sqlite_native_context(context),
                         cpkt_sqlite_native_value(value));
  }
}

void cpkt_sqlite_context_result_subtype(cpkt_sqlite_context *context,
                                        unsigned long subtype) {
  if (cpkt_sqlite_native_context(context) != NULL) {
    sqlite3_result_subtype(cpkt_sqlite_native_context(context),
                           (unsigned int)subtype);
  }
}

int cpkt_sqlite_value_type(const cpkt_sqlite_value *value) {
  return cpkt_sqlite_native_value(value) == NULL
             ? CPKT_SQLITE_NULL
             : sqlite3_value_type(cpkt_sqlite_native_value(value));
}

int cpkt_sqlite_value_numeric_type(cpkt_sqlite_value *value) {
  return cpkt_sqlite_native_value(value) == NULL
             ? CPKT_SQLITE_NULL
             : sqlite3_value_numeric_type(cpkt_sqlite_native_value(value));
}

int cpkt_sqlite_value_int(const cpkt_sqlite_value *value) {
  return cpkt_sqlite_native_value(value) == NULL
             ? 0
             : sqlite3_value_int(cpkt_sqlite_native_value(value));
}

cpkt_sqlite_i64 cpkt_sqlite_value_i64(const cpkt_sqlite_value *value) {
  if (cpkt_sqlite_native_value(value) == NULL)
    return cpkt_sqlite_i64_make(0, 0);
  return cpkt_sqlite_public_i64(
      sqlite3_value_int64(cpkt_sqlite_native_value(value)));
}

double cpkt_sqlite_value_double(const cpkt_sqlite_value *value) {
  return cpkt_sqlite_native_value(value) == NULL
             ? 0.0
             : sqlite3_value_double(cpkt_sqlite_native_value(value));
}

const void *cpkt_sqlite_value_blob(const cpkt_sqlite_value *value) {
  return cpkt_sqlite_native_value(value) == NULL
             ? NULL
             : sqlite3_value_blob(cpkt_sqlite_native_value(value));
}

const unsigned char *cpkt_sqlite_value_text(const cpkt_sqlite_value *value) {
  return cpkt_sqlite_native_value(value) == NULL
             ? NULL
             : sqlite3_value_text(cpkt_sqlite_native_value(value));
}

const void *cpkt_sqlite_value_text16(const cpkt_sqlite_value *value) {
  return cpkt_sqlite_native_value(value) == NULL
             ? NULL
             : sqlite3_value_text16(cpkt_sqlite_native_value(value));
}

const void *cpkt_sqlite_value_text16le(const cpkt_sqlite_value *value) {
  return cpkt_sqlite_native_value(value) == NULL
             ? NULL
             : sqlite3_value_text16le(cpkt_sqlite_native_value(value));
}

const void *cpkt_sqlite_value_text16be(const cpkt_sqlite_value *value) {
  return cpkt_sqlite_native_value(value) == NULL
             ? NULL
             : sqlite3_value_text16be(cpkt_sqlite_native_value(value));
}

void *cpkt_sqlite_value_pointer(const cpkt_sqlite_value *value,
                                const char *type_name) {
  if (cpkt_sqlite_native_value(value) == NULL || type_name == NULL)
    return NULL;
  return sqlite3_value_pointer(cpkt_sqlite_native_value(value), type_name);
}

int cpkt_sqlite_value_bytes(const cpkt_sqlite_value *value) {
  return cpkt_sqlite_native_value(value) == NULL
             ? 0
             : sqlite3_value_bytes(cpkt_sqlite_native_value(value));
}

int cpkt_sqlite_value_bytes16(const cpkt_sqlite_value *value) {
  return cpkt_sqlite_native_value(value) == NULL
             ? 0
             : sqlite3_value_bytes16(cpkt_sqlite_native_value(value));
}

int cpkt_sqlite_value_nochange(const cpkt_sqlite_value *value) {
  return cpkt_sqlite_native_value(value) == NULL
             ? 0
             : sqlite3_value_nochange(cpkt_sqlite_native_value(value));
}

int cpkt_sqlite_value_from_bind(const cpkt_sqlite_value *value) {
  return cpkt_sqlite_native_value(value) == NULL
             ? 0
             : sqlite3_value_frombind(cpkt_sqlite_native_value(value));
}

int cpkt_sqlite_value_encoding(const cpkt_sqlite_value *value) {
  return cpkt_sqlite_native_value(value) == NULL
             ? 0
             : sqlite3_value_encoding(cpkt_sqlite_native_value(value));
}

unsigned long cpkt_sqlite_value_subtype(const cpkt_sqlite_value *value) {
  return cpkt_sqlite_native_value(value) == NULL
             ? 0UL
             : (unsigned long)sqlite3_value_subtype(
                   cpkt_sqlite_native_value(value));
}

cpkt_sqlite_value *cpkt_sqlite_value_duplicate(const cpkt_sqlite_value *value) {
  cpkt_sqlite_value *copy;
  sqlite3_value *native_copy;
  if (cpkt_sqlite_native_value(value) == NULL)
    return NULL;
  native_copy = sqlite3_value_dup(cpkt_sqlite_native_value(value));
  if (native_copy == NULL)
    return NULL;
  copy = (cpkt_sqlite_value *)calloc(1, sizeof(*copy));
  if (copy == NULL) {
    sqlite3_value_free(native_copy);
    return NULL;
  }
  copy->value = native_copy;
  copy->owned = 1;
  copy->shell_owned = 1;
  return copy;
}

void cpkt_sqlite_value_free(cpkt_sqlite_value *value) {
  if (value == NULL)
    return;
  if (!value->shell_owned)
    return;
  if (value->owned && value->value != NULL)
    sqlite3_value_free((sqlite3_value *)value->value);
  value->value = NULL;
  free(value);
}

static int cpkt_sqlite_session_attach(cpkt_sqlite_session *self,
                                      const char *table_name) {
  if (cpkt_sqlite_native_session(self) == NULL)
    return CPKT_SQLITE_MISUSE;
  return sqlite3session_attach(cpkt_sqlite_native_session(self), table_name);
}

static int cpkt_sqlite_session_enable(cpkt_sqlite_session *self, int enabled) {
  if (cpkt_sqlite_native_session(self) == NULL)
    return 0;
  return sqlite3session_enable(cpkt_sqlite_native_session(self), enabled);
}

static int cpkt_sqlite_session_indirect(cpkt_sqlite_session *self,
                                        int indirect) {
  if (cpkt_sqlite_native_session(self) == NULL)
    return 0;
  return sqlite3session_indirect(cpkt_sqlite_native_session(self), indirect);
}

static int cpkt_sqlite_session_empty(const cpkt_sqlite_session *self) {
  if (cpkt_sqlite_native_session(self) == NULL)
    return 1;
  return sqlite3session_isempty(cpkt_sqlite_native_session(self));
}

static int cpkt_sqlite_session_object_config(cpkt_sqlite_session *self,
                                             int operation, int *value) {
  if (cpkt_sqlite_native_session(self) == NULL || value == NULL) {
    return CPKT_SQLITE_MISUSE;
  }
  return sqlite3session_object_config(cpkt_sqlite_native_session(self),
                                      operation, value);
}

static int cpkt_sqlite_session_filter_trampoline(void *context,
                                                 const char *table_name) {
  cpkt_sqlite_session *self;
  self = (cpkt_sqlite_session *)context;
  if (self == NULL || self->filter == NULL)
    return 1;
  return self->filter(self->filter_context, table_name);
}

static void
cpkt_sqlite_session_table_filter(cpkt_sqlite_session *self,
                                 cpkt_sqlite_session_filter_callback filter,
                                 void *context) {
  if (cpkt_sqlite_native_session(self) == NULL)
    return;
  self->filter = filter;
  self->filter_context = context;
  sqlite3session_table_filter(
      cpkt_sqlite_native_session(self),
      filter == NULL ? NULL : cpkt_sqlite_session_filter_trampoline,
      filter == NULL ? NULL : self);
}

static cpkt_sqlite_i64
cpkt_sqlite_session_changeset_size(const cpkt_sqlite_session *self) {
  if (cpkt_sqlite_native_session(self) == NULL)
    return cpkt_sqlite_i64_make(0, 0);
  return cpkt_sqlite_public_i64(
      sqlite3session_changeset_size(cpkt_sqlite_native_session(self)));
}

static cpkt_sqlite_i64
cpkt_sqlite_session_memory_used(const cpkt_sqlite_session *self) {
  if (cpkt_sqlite_native_session(self) == NULL)
    return cpkt_sqlite_i64_make(0, 0);
  return cpkt_sqlite_public_i64(
      sqlite3session_memory_used(cpkt_sqlite_native_session(self)));
}

static int cpkt_sqlite_session_diff(cpkt_sqlite_session *self,
                                    const char *from_database,
                                    const char *table_name, char **error_out) {
  if (error_out != NULL)
    *error_out = NULL;
  if (cpkt_sqlite_native_session(self) == NULL || from_database == NULL ||
      table_name == NULL)
    return CPKT_SQLITE_MISUSE;
  return sqlite3session_diff(cpkt_sqlite_native_session(self), from_database,
                             table_name, error_out);
}

static int cpkt_sqlite_session_stream(cpkt_sqlite_session *self,
                                      cpkt_sqlite_stream_output_callback output,
                                      void *context, int patchset) {
  cpkt_sqlite_stream_output_context output_context;
  if (cpkt_sqlite_native_session(self) == NULL || output == NULL) {
    return CPKT_SQLITE_MISUSE;
  }
  output_context.callback = output;
  output_context.user_context = context;
  if (patchset) {
    return sqlite3session_patchset_strm(cpkt_sqlite_native_session(self),
                                        cpkt_sqlite_stream_output_trampoline,
                                        &output_context);
  }
  return sqlite3session_changeset_strm(cpkt_sqlite_native_session(self),
                                       cpkt_sqlite_stream_output_trampoline,
                                       &output_context);
}

static int
cpkt_sqlite_session_changeset_strm(cpkt_sqlite_session *self,
                                   cpkt_sqlite_stream_output_callback output,
                                   void *context) {
  return cpkt_sqlite_session_stream(self, output, context, 0);
}

static int
cpkt_sqlite_session_patchset_strm(cpkt_sqlite_session *self,
                                  cpkt_sqlite_stream_output_callback output,
                                  void *context) {
  return cpkt_sqlite_session_stream(self, output, context, 1);
}

static void cpkt_sqlite_changeset_free(cpkt_sqlite_changeset *self) {
  if (self == NULL)
    return;
  if (self->owned_data != NULL)
    sqlite3_free(self->owned_data);
  self->data = NULL;
  self->owned_data = NULL;
  free(self);
}

static int
cpkt_sqlite_changeset_has_valid_data(const cpkt_sqlite_changeset *changeset) {
  return changeset != NULL && changeset->byte_count >= 0 &&
         (changeset->byte_count == 0 || changeset->data != NULL);
}

static int cpkt_sqlite_changeset_new_owned(void *data, int byte_count,
                                           cpkt_sqlite_changeset **out) {
  cpkt_sqlite_changeset *changeset;
  if (out != NULL)
    *out = NULL;
  if (out == NULL) {
    sqlite3_free(data);
    return CPKT_SQLITE_MISUSE;
  }
  changeset = (cpkt_sqlite_changeset *)calloc(1, sizeof(*changeset));
  if (changeset == NULL) {
    sqlite3_free(data);
    return CPKT_SQLITE_NOMEM;
  }
  changeset->data = data;
  changeset->byte_count = byte_count;
  changeset->iterator = cpkt_sqlite_changeset_start;
  changeset->free = cpkt_sqlite_changeset_free;
  changeset->owned_data = data;
  *out = changeset;
  return CPKT_SQLITE_OK;
}

static int cpkt_sqlite_session_changeset(cpkt_sqlite_session *self,
                                         cpkt_sqlite_changeset **out) {
  void *data;
  int byte_count;
  int status;
  if (out != NULL)
    *out = NULL;
  if (cpkt_sqlite_native_session(self) == NULL || out == NULL)
    return CPKT_SQLITE_MISUSE;
  data = NULL;
  byte_count = 0;
  status = sqlite3session_changeset(cpkt_sqlite_native_session(self),
                                    &byte_count, &data);
  if (status != SQLITE_OK)
    return status;
  return cpkt_sqlite_changeset_new_owned(data, byte_count, out);
}

static int cpkt_sqlite_session_patchset(cpkt_sqlite_session *self,
                                        cpkt_sqlite_changeset **out) {
  void *data;
  int byte_count;
  int status;
  if (out != NULL)
    *out = NULL;
  if (cpkt_sqlite_native_session(self) == NULL || out == NULL)
    return CPKT_SQLITE_MISUSE;
  data = NULL;
  byte_count = 0;
  status = sqlite3session_patchset(cpkt_sqlite_native_session(self),
                                   &byte_count, &data);
  if (status != SQLITE_OK)
    return status;
  return cpkt_sqlite_changeset_new_owned(data, byte_count, out);
}

static void cpkt_sqlite_session_close(cpkt_sqlite_session *self) {
  cpkt_sqlite_state *state;
  sqlite3_mutex *mutex;
  if (self == NULL)
    return;
  mutex = cpkt_sqlite_connection_lock(self->database);
  if (cpkt_sqlite_native_session(self) != NULL)
    sqlite3session_delete(cpkt_sqlite_native_session(self));
  state = cpkt_sqlite_state_for(self->database);
  if (state != NULL && state->session_count > 0)
    --state->session_count;
  cpkt_sqlite_connection_unlock(mutex);
  self->session = NULL;
  self->database = NULL;
  free(self);
}

int cpkt_sqlite_session_new(cpkt_sqlite *database, const char *schema,
                            cpkt_sqlite_session **out) {
  sqlite3_session *native_session;
  cpkt_sqlite_session *public_session;
  sqlite3_mutex *mutex;
  int status;
  if (out != NULL)
    *out = NULL;
  if (database == NULL || cpkt_sqlite_native(database) == NULL || out == NULL) {
    return CPKT_SQLITE_MISUSE;
  }
  if (cpkt_sqlite_state_for(database) == NULL)
    return CPKT_SQLITE_NOMEM;
  mutex = cpkt_sqlite_connection_lock(database);
  if (cpkt_sqlite_state_for(database)->preupdate_callback != NULL) {
    cpkt_sqlite_connection_unlock(mutex);
    return CPKT_SQLITE_MISUSE;
  }
  native_session = NULL;
  status = sqlite3session_create(cpkt_sqlite_native(database), schema,
                                 &native_session);
  if (status != SQLITE_OK) {
    cpkt_sqlite_connection_unlock(mutex);
    return status;
  }
  public_session = (cpkt_sqlite_session *)calloc(1, sizeof(*public_session));
  if (public_session == NULL) {
    sqlite3session_delete(native_session);
    cpkt_sqlite_connection_unlock(mutex);
    return CPKT_SQLITE_NOMEM;
  }
  public_session->attach = cpkt_sqlite_session_attach;
  public_session->object_config = cpkt_sqlite_session_object_config;
  public_session->table_filter = cpkt_sqlite_session_table_filter;
  public_session->enable = cpkt_sqlite_session_enable;
  public_session->indirect = cpkt_sqlite_session_indirect;
  public_session->empty = cpkt_sqlite_session_empty;
  public_session->changeset = cpkt_sqlite_session_changeset;
  public_session->patchset = cpkt_sqlite_session_patchset;
  public_session->changeset_size = cpkt_sqlite_session_changeset_size;
  public_session->memory_used = cpkt_sqlite_session_memory_used;
  public_session->diff = cpkt_sqlite_session_diff;
  public_session->changeset_strm = cpkt_sqlite_session_changeset_strm;
  public_session->patchset_strm = cpkt_sqlite_session_patchset_strm;
  public_session->close = cpkt_sqlite_session_close;
  public_session->session = native_session;
  public_session->database = database;
  ++cpkt_sqlite_state_for(database)->session_count;
  cpkt_sqlite_connection_unlock(mutex);
  *out = public_session;
  return CPKT_SQLITE_OK;
}

int cpkt_sqlite_session_config(int operation, int *value) {
  if (value == NULL)
    return CPKT_SQLITE_MISUSE;
  return sqlite3session_config(operation, value);
}

static int cpkt_sqlite_iterator_next(cpkt_sqlite_changeset_iterator *self) {
  if (cpkt_sqlite_native_iterator(self) == NULL)
    return CPKT_SQLITE_MISUSE;
  return sqlite3changeset_next(cpkt_sqlite_native_iterator(self));
}

static int cpkt_sqlite_iterator_operation(cpkt_sqlite_changeset_iterator *self,
                                          const char **table_name,
                                          int *column_count, int *operation,
                                          int *indirect) {
  if (cpkt_sqlite_native_iterator(self) == NULL)
    return CPKT_SQLITE_MISUSE;
  return sqlite3changeset_op(cpkt_sqlite_native_iterator(self), table_name,
                             column_count, operation, indirect);
}

static int
cpkt_sqlite_iterator_primary_key(cpkt_sqlite_changeset_iterator *self,
                                 const unsigned char **columns,
                                 int *column_count) {
  unsigned char *native_columns;
  int status;
  if (columns != NULL)
    *columns = NULL;
  if (column_count != NULL)
    *column_count = 0;
  if (cpkt_sqlite_native_iterator(self) == NULL || columns == NULL) {
    return CPKT_SQLITE_MISUSE;
  }
  native_columns = NULL;
  status = sqlite3changeset_pk(cpkt_sqlite_native_iterator(self),
                               &native_columns, column_count);
  if (status != SQLITE_OK)
    return status;
  *columns = native_columns;
  return CPKT_SQLITE_OK;
}

static int cpkt_sqlite_iterator_value(cpkt_sqlite_changeset_iterator *self,
                                      int column, cpkt_sqlite_value **out,
                                      int kind) {
  sqlite3_value *native_value;
  cpkt_sqlite_value *public_value;
  int status;
  if (out != NULL)
    *out = NULL;
  if (cpkt_sqlite_native_iterator(self) == NULL || out == NULL)
    return CPKT_SQLITE_MISUSE;
  native_value = NULL;
  if (kind == 0)
    status = sqlite3changeset_old(cpkt_sqlite_native_iterator(self), column,
                                  &native_value);
  else if (kind == 1)
    status = sqlite3changeset_new(cpkt_sqlite_native_iterator(self), column,
                                  &native_value);
  else
    status = sqlite3changeset_conflict(cpkt_sqlite_native_iterator(self),
                                       column, &native_value);
  if (status != SQLITE_OK)
    return status;
  if (native_value == NULL)
    return CPKT_SQLITE_OK;
  public_value = (cpkt_sqlite_value *)calloc(1, sizeof(*public_value));
  if (public_value == NULL)
    return CPKT_SQLITE_NOMEM;
  public_value->value = native_value;
  public_value->owned = 0;
  public_value->shell_owned = 1;
  *out = public_value;
  return CPKT_SQLITE_OK;
}

static int cpkt_sqlite_iterator_old_value(cpkt_sqlite_changeset_iterator *self,
                                          int column, cpkt_sqlite_value **out) {
  return cpkt_sqlite_iterator_value(self, column, out, 0);
}

static int cpkt_sqlite_iterator_new_value(cpkt_sqlite_changeset_iterator *self,
                                          int column, cpkt_sqlite_value **out) {
  return cpkt_sqlite_iterator_value(self, column, out, 1);
}

static int
cpkt_sqlite_iterator_conflict_value(cpkt_sqlite_changeset_iterator *self,
                                    int column, cpkt_sqlite_value **out) {
  return cpkt_sqlite_iterator_value(self, column, out, 2);
}

static int
cpkt_sqlite_iterator_foreign_key_conflicts(cpkt_sqlite_changeset_iterator *self,
                                           int *count_out) {
  if (count_out != NULL)
    *count_out = 0;
  if (cpkt_sqlite_native_iterator(self) == NULL || count_out == NULL) {
    return CPKT_SQLITE_MISUSE;
  }
  return sqlite3changeset_fk_conflicts(cpkt_sqlite_native_iterator(self),
                                       count_out);
}

static int cpkt_sqlite_iterator_close(cpkt_sqlite_changeset_iterator *self) {
  int status;
  if (self == NULL)
    return CPKT_SQLITE_MISUSE;
  status = cpkt_sqlite_native_iterator(self) == NULL
               ? CPKT_SQLITE_OK
               : sqlite3changeset_finalize(cpkt_sqlite_native_iterator(self));
  self->iterator = NULL;
  free(self);
  return status;
}

static void cpkt_sqlite_iterator_init(cpkt_sqlite_changeset_iterator *iterator,
                                      sqlite3_changeset_iter *native_iterator,
                                      int borrowed) {
  iterator->next = cpkt_sqlite_iterator_next;
  iterator->operation = cpkt_sqlite_iterator_operation;
  iterator->primary_key = cpkt_sqlite_iterator_primary_key;
  iterator->old_value = cpkt_sqlite_iterator_old_value;
  iterator->new_value = cpkt_sqlite_iterator_new_value;
  iterator->conflict_value = cpkt_sqlite_iterator_conflict_value;
  iterator->foreign_key_conflicts = cpkt_sqlite_iterator_foreign_key_conflicts;
  iterator->close = borrowed ? cpkt_sqlite_iterator_borrowed_close
                             : cpkt_sqlite_iterator_close;
  iterator->iterator = native_iterator;
}

int cpkt_sqlite_changeset_start(const cpkt_sqlite_changeset *changeset,
                                cpkt_sqlite_changeset_iterator **out) {
  return cpkt_sqlite_changeset_start_ex(changeset, 0, out);
}

int cpkt_sqlite_changeset_start_ex(const cpkt_sqlite_changeset *changeset,
                                   int flags,
                                   cpkt_sqlite_changeset_iterator **out) {
  sqlite3_changeset_iter *native_iterator;
  cpkt_sqlite_changeset_iterator *public_iterator;
  int status;
  if (out != NULL)
    *out = NULL;
  if (!cpkt_sqlite_changeset_has_valid_data(changeset) || out == NULL)
    return CPKT_SQLITE_MISUSE;
  native_iterator = NULL;
  status = sqlite3changeset_start_v2(&native_iterator, changeset->byte_count,
                                     (void *)changeset->data, flags);
  if (status != SQLITE_OK)
    return status;
  public_iterator =
      (cpkt_sqlite_changeset_iterator *)calloc(1, sizeof(*public_iterator));
  if (public_iterator == NULL) {
    sqlite3changeset_finalize(native_iterator);
    return CPKT_SQLITE_NOMEM;
  }
  cpkt_sqlite_iterator_init(public_iterator, native_iterator, 0);
  *out = public_iterator;
  return CPKT_SQLITE_OK;
}

int cpkt_sqlite_changeset_start_strm(cpkt_sqlite_stream_input_callback input,
                                     void *context, int flags,
                                     cpkt_sqlite_changeset_iterator **out) {
  sqlite3_changeset_iter *native_iterator;
  cpkt_sqlite_changeset_iterator *public_iterator;
  int status;
  if (out != NULL)
    *out = NULL;
  if (input == NULL || out == NULL)
    return CPKT_SQLITE_MISUSE;
  public_iterator =
      (cpkt_sqlite_changeset_iterator *)calloc(1, sizeof(*public_iterator));
  if (public_iterator == NULL)
    return CPKT_SQLITE_NOMEM;
  public_iterator->input = input;
  public_iterator->input_context = context;
  native_iterator = NULL;
  status = sqlite3changeset_start_v2_strm(
      &native_iterator, cpkt_sqlite_iterator_stream_input_trampoline,
      public_iterator, flags);
  if (status != SQLITE_OK) {
    free(public_iterator);
    return status;
  }
  cpkt_sqlite_iterator_init(public_iterator, native_iterator, 0);
  *out = public_iterator;
  return CPKT_SQLITE_OK;
}

int cpkt_sqlite_changeset_invert(const cpkt_sqlite_changeset *input,
                                 cpkt_sqlite_changeset **out) {
  void *data;
  int byte_count;
  int status;
  if (out != NULL)
    *out = NULL;
  if (!cpkt_sqlite_changeset_has_valid_data(input) || out == NULL)
    return CPKT_SQLITE_MISUSE;
  data = NULL;
  byte_count = 0;
  status = sqlite3changeset_invert(input->byte_count, input->data, &byte_count,
                                   &data);
  if (status != SQLITE_OK)
    return status;
  return cpkt_sqlite_changeset_new_owned(data, byte_count, out);
}

int cpkt_sqlite_changeset_concat(const cpkt_sqlite_changeset *left,
                                 const cpkt_sqlite_changeset *right,
                                 cpkt_sqlite_changeset **out) {
  void *data;
  int byte_count;
  int status;
  if (out != NULL)
    *out = NULL;
  if (!cpkt_sqlite_changeset_has_valid_data(left) ||
      !cpkt_sqlite_changeset_has_valid_data(right) || out == NULL)
    return CPKT_SQLITE_MISUSE;
  data = NULL;
  byte_count = 0;
  status = sqlite3changeset_concat(left->byte_count, (void *)left->data,
                                   right->byte_count, (void *)right->data,
                                   &byte_count, &data);
  if (status != SQLITE_OK)
    return status;
  return cpkt_sqlite_changeset_new_owned(data, byte_count, out);
}

int cpkt_sqlite_changeset_invert_strm(cpkt_sqlite_stream_input_callback input,
                                      void *input_context,
                                      cpkt_sqlite_stream_output_callback output,
                                      void *output_context) {
  cpkt_sqlite_stream_input_context public_input;
  cpkt_sqlite_stream_output_context public_output;
  if (input == NULL || output == NULL)
    return CPKT_SQLITE_MISUSE;
  public_input.callback = input;
  public_input.user_context = input_context;
  public_output.callback = output;
  public_output.user_context = output_context;
  return sqlite3changeset_invert_strm(
      cpkt_sqlite_stream_input_trampoline, &public_input,
      cpkt_sqlite_stream_output_trampoline, &public_output);
}

int cpkt_sqlite_changeset_concat_strm(
    cpkt_sqlite_stream_input_callback left_input, void *left_context,
    cpkt_sqlite_stream_input_callback right_input, void *right_context,
    cpkt_sqlite_stream_output_callback output, void *output_context) {
  cpkt_sqlite_stream_input_context public_left;
  cpkt_sqlite_stream_input_context public_right;
  cpkt_sqlite_stream_output_context public_output;
  if (left_input == NULL || right_input == NULL || output == NULL) {
    return CPKT_SQLITE_MISUSE;
  }
  public_left.callback = left_input;
  public_left.user_context = left_context;
  public_right.callback = right_input;
  public_right.user_context = right_context;
  public_output.callback = output;
  public_output.user_context = output_context;
  return sqlite3changeset_concat_strm(
      cpkt_sqlite_stream_input_trampoline, &public_left,
      cpkt_sqlite_stream_input_trampoline, &public_right,
      cpkt_sqlite_stream_output_trampoline, &public_output);
}

int cpkt_sqlite_changeset_apply(
    cpkt_sqlite *database, const cpkt_sqlite_changeset *changeset,
    cpkt_sqlite_changeset_filter_callback filter,
    cpkt_sqlite_changeset_conflict_callback conflict, void *context) {
  cpkt_sqlite_changeset_apply_context apply_context;
  if (database == NULL || cpkt_sqlite_native(database) == NULL ||
      !cpkt_sqlite_changeset_has_valid_data(changeset))
    return CPKT_SQLITE_MISUSE;
  apply_context.filter = filter;
  apply_context.iterator_filter = NULL;
  apply_context.conflict = conflict;
  apply_context.user_context = context;
  return sqlite3changeset_apply(
      cpkt_sqlite_native(database), changeset->byte_count,
      (void *)changeset->data,
      filter == NULL ? NULL : cpkt_sqlite_changeset_filter_trampoline,
      cpkt_sqlite_changeset_conflict_trampoline, &apply_context);
}

int cpkt_sqlite_changeset_apply_strm(
    cpkt_sqlite *database, cpkt_sqlite_stream_input_callback input,
    void *input_context, cpkt_sqlite_changeset_filter_callback filter,
    cpkt_sqlite_changeset_conflict_callback conflict, void *context) {
  cpkt_sqlite_stream_input_context stream_context;
  cpkt_sqlite_changeset_apply_context apply_context;
  if (database == NULL || cpkt_sqlite_native(database) == NULL ||
      input == NULL) {
    return CPKT_SQLITE_MISUSE;
  }
  stream_context.callback = input;
  stream_context.user_context = input_context;
  apply_context.filter = filter;
  apply_context.iterator_filter = NULL;
  apply_context.conflict = conflict;
  apply_context.user_context = context;
  return sqlite3changeset_apply_strm(
      cpkt_sqlite_native(database), cpkt_sqlite_stream_input_trampoline,
      &stream_context,
      filter == NULL ? NULL : cpkt_sqlite_changeset_filter_trampoline,
      cpkt_sqlite_changeset_conflict_trampoline, &apply_context);
}

static int
cpkt_sqlite_changeset_rebase_result(void *data, int byte_count,
                                    cpkt_sqlite_changeset **rebase_out) {
  if (rebase_out == NULL) {
    if (data != NULL)
      sqlite3_free(data);
    return CPKT_SQLITE_OK;
  }
  if (data == NULL) {
    *rebase_out = NULL;
    return CPKT_SQLITE_OK;
  }
  return cpkt_sqlite_changeset_new_owned(data, byte_count, rebase_out);
}

int cpkt_sqlite_changeset_apply_ex(
    cpkt_sqlite *database, const cpkt_sqlite_changeset *changeset,
    cpkt_sqlite_changeset_filter_callback filter,
    cpkt_sqlite_changeset_conflict_callback conflict, void *context, int flags,
    cpkt_sqlite_changeset **rebase_out) {
  cpkt_sqlite_changeset_apply_context apply_context;
  void *rebase_data;
  int rebase_size;
  int status;
  if (rebase_out != NULL)
    *rebase_out = NULL;
  if (database == NULL || cpkt_sqlite_native(database) == NULL ||
      !cpkt_sqlite_changeset_has_valid_data(changeset))
    return CPKT_SQLITE_MISUSE;
  apply_context.filter = filter;
  apply_context.iterator_filter = NULL;
  apply_context.conflict = conflict;
  apply_context.user_context = context;
  rebase_data = NULL;
  rebase_size = 0;
  status = sqlite3changeset_apply_v2(
      cpkt_sqlite_native(database), changeset->byte_count,
      (void *)changeset->data,
      filter == NULL ? NULL : cpkt_sqlite_changeset_filter_trampoline,
      cpkt_sqlite_changeset_conflict_trampoline, &apply_context,
      rebase_out == NULL ? NULL : &rebase_data,
      rebase_out == NULL ? NULL : &rebase_size, flags);
  if (status != SQLITE_OK) {
    if (rebase_data != NULL)
      sqlite3_free(rebase_data);
    return status;
  }
  return cpkt_sqlite_changeset_rebase_result(rebase_data, rebase_size,
                                             rebase_out);
}

int cpkt_sqlite_changeset_apply_ex_strm(
    cpkt_sqlite *database, cpkt_sqlite_stream_input_callback input,
    void *input_context, cpkt_sqlite_changeset_filter_callback filter,
    cpkt_sqlite_changeset_conflict_callback conflict, void *context, int flags,
    cpkt_sqlite_changeset **rebase_out) {
  cpkt_sqlite_stream_input_context stream_context;
  cpkt_sqlite_changeset_apply_context apply_context;
  void *rebase_data;
  int rebase_size;
  int status;
  if (rebase_out != NULL)
    *rebase_out = NULL;
  if (database == NULL || cpkt_sqlite_native(database) == NULL ||
      input == NULL) {
    return CPKT_SQLITE_MISUSE;
  }
  stream_context.callback = input;
  stream_context.user_context = input_context;
  apply_context.filter = filter;
  apply_context.iterator_filter = NULL;
  apply_context.conflict = conflict;
  apply_context.user_context = context;
  rebase_data = NULL;
  rebase_size = 0;
  status = sqlite3changeset_apply_v2_strm(
      cpkt_sqlite_native(database), cpkt_sqlite_stream_input_trampoline,
      &stream_context,
      filter == NULL ? NULL : cpkt_sqlite_changeset_filter_trampoline,
      cpkt_sqlite_changeset_conflict_trampoline, &apply_context,
      rebase_out == NULL ? NULL : &rebase_data,
      rebase_out == NULL ? NULL : &rebase_size, flags);
  if (status != SQLITE_OK) {
    if (rebase_data != NULL)
      sqlite3_free(rebase_data);
    return status;
  }
  return cpkt_sqlite_changeset_rebase_result(rebase_data, rebase_size,
                                             rebase_out);
}

int cpkt_sqlite_changeset_apply_v3(
    cpkt_sqlite *database, const cpkt_sqlite_changeset *changeset,
    cpkt_sqlite_changeset_iterator_filter_callback filter,
    cpkt_sqlite_changeset_conflict_callback conflict, void *context, int flags,
    cpkt_sqlite_changeset **rebase_out) {
  cpkt_sqlite_changeset_apply_context apply_context;
  void *rebase_data;
  int rebase_size;
  int status;
  if (rebase_out != NULL)
    *rebase_out = NULL;
  if (database == NULL || cpkt_sqlite_native(database) == NULL ||
      !cpkt_sqlite_changeset_has_valid_data(changeset))
    return CPKT_SQLITE_MISUSE;
  apply_context.filter = NULL;
  apply_context.iterator_filter = filter;
  apply_context.conflict = conflict;
  apply_context.user_context = context;
  rebase_data = NULL;
  rebase_size = 0;
  status = sqlite3changeset_apply_v3(
      cpkt_sqlite_native(database), changeset->byte_count,
      (void *)changeset->data,
      filter == NULL ? NULL : cpkt_sqlite_changeset_iterator_filter_trampoline,
      cpkt_sqlite_changeset_conflict_trampoline, &apply_context,
      rebase_out == NULL ? NULL : &rebase_data,
      rebase_out == NULL ? NULL : &rebase_size, flags);
  if (status != SQLITE_OK) {
    if (rebase_data != NULL)
      sqlite3_free(rebase_data);
    return status;
  }
  return cpkt_sqlite_changeset_rebase_result(rebase_data, rebase_size,
                                             rebase_out);
}

int cpkt_sqlite_changeset_apply_v3_strm(
    cpkt_sqlite *database, cpkt_sqlite_stream_input_callback input,
    void *input_context, cpkt_sqlite_changeset_iterator_filter_callback filter,
    cpkt_sqlite_changeset_conflict_callback conflict, void *context, int flags,
    cpkt_sqlite_changeset **rebase_out) {
  cpkt_sqlite_stream_input_context stream_context;
  cpkt_sqlite_changeset_apply_context apply_context;
  void *rebase_data;
  int rebase_size;
  int status;
  if (rebase_out != NULL)
    *rebase_out = NULL;
  if (database == NULL || cpkt_sqlite_native(database) == NULL ||
      input == NULL) {
    return CPKT_SQLITE_MISUSE;
  }
  stream_context.callback = input;
  stream_context.user_context = input_context;
  apply_context.filter = NULL;
  apply_context.iterator_filter = filter;
  apply_context.conflict = conflict;
  apply_context.user_context = context;
  rebase_data = NULL;
  rebase_size = 0;
  status = sqlite3changeset_apply_v3_strm(
      cpkt_sqlite_native(database), cpkt_sqlite_stream_input_trampoline,
      &stream_context,
      filter == NULL ? NULL : cpkt_sqlite_changeset_iterator_filter_trampoline,
      cpkt_sqlite_changeset_conflict_trampoline, &apply_context,
      rebase_out == NULL ? NULL : &rebase_data,
      rebase_out == NULL ? NULL : &rebase_size, flags);
  if (status != SQLITE_OK) {
    if (rebase_data != NULL)
      sqlite3_free(rebase_data);
    return status;
  }
  return cpkt_sqlite_changeset_rebase_result(rebase_data, rebase_size,
                                             rebase_out);
}

static int cpkt_sqlite_rebaser_configure(cpkt_sqlite_rebaser *self,
                                         const void *data, int byte_count) {
  if (cpkt_sqlite_native_rebaser(self) == NULL || byte_count < 0 ||
      (data == NULL && byte_count != 0))
    return CPKT_SQLITE_MISUSE;
  return sqlite3rebaser_configure(cpkt_sqlite_native_rebaser(self), byte_count,
                                  (const void *)data);
}

static int cpkt_sqlite_rebaser_rebase(cpkt_sqlite_rebaser *self,
                                      const void *data, int byte_count,
                                      cpkt_sqlite_changeset **out) {
  void *result;
  int result_size;
  int status;
  if (out != NULL)
    *out = NULL;
  if (cpkt_sqlite_native_rebaser(self) == NULL || byte_count < 0 ||
      (data == NULL && byte_count != 0) || out == NULL) {
    return CPKT_SQLITE_MISUSE;
  }
  result = NULL;
  result_size = 0;
  status = sqlite3rebaser_rebase(cpkt_sqlite_native_rebaser(self), byte_count,
                                 (const void *)data, &result_size, &result);
  if (status != SQLITE_OK)
    return status;
  return cpkt_sqlite_changeset_new_owned(result, result_size, out);
}

static int cpkt_sqlite_rebaser_rebase_strm(
    cpkt_sqlite_rebaser *self, cpkt_sqlite_stream_input_callback input,
    void *input_context, cpkt_sqlite_stream_output_callback output,
    void *output_context) {
  cpkt_sqlite_stream_input_context input_state;
  cpkt_sqlite_stream_output_context output_state;
  if (cpkt_sqlite_native_rebaser(self) == NULL || input == NULL ||
      output == NULL) {
    return CPKT_SQLITE_MISUSE;
  }
  input_state.callback = input;
  input_state.user_context = input_context;
  output_state.callback = output;
  output_state.user_context = output_context;
  return sqlite3rebaser_rebase_strm(
      cpkt_sqlite_native_rebaser(self), cpkt_sqlite_stream_input_trampoline,
      &input_state, cpkt_sqlite_stream_output_trampoline, &output_state);
}

static void cpkt_sqlite_rebaser_close(cpkt_sqlite_rebaser *self) {
  if (self == NULL)
    return;
  if (cpkt_sqlite_native_rebaser(self) != NULL)
    sqlite3rebaser_delete(cpkt_sqlite_native_rebaser(self));
  self->rebaser = NULL;
  free(self);
}

int cpkt_sqlite_rebaser_new(cpkt_sqlite_rebaser **out) {
  sqlite3_rebaser *native_rebaser;
  cpkt_sqlite_rebaser *public_rebaser;
  int status;
  if (out != NULL)
    *out = NULL;
  if (out == NULL)
    return CPKT_SQLITE_MISUSE;
  native_rebaser = NULL;
  status = sqlite3rebaser_create(&native_rebaser);
  if (status != SQLITE_OK)
    return status;
  public_rebaser = (cpkt_sqlite_rebaser *)calloc(1, sizeof(*public_rebaser));
  if (public_rebaser == NULL) {
    sqlite3rebaser_delete(native_rebaser);
    return CPKT_SQLITE_NOMEM;
  }
  public_rebaser->configure = cpkt_sqlite_rebaser_configure;
  public_rebaser->rebase = cpkt_sqlite_rebaser_rebase;
  public_rebaser->rebase_strm = cpkt_sqlite_rebaser_rebase_strm;
  public_rebaser->close = cpkt_sqlite_rebaser_close;
  public_rebaser->rebaser = native_rebaser;
  *out = public_rebaser;
  return CPKT_SQLITE_OK;
}

static int cpkt_sqlite_changegroup_config(cpkt_sqlite_changegroup *self,
                                          int operation, int *value) {
  if (cpkt_sqlite_native_changegroup(self) == NULL || value == NULL) {
    return CPKT_SQLITE_MISUSE;
  }
  return sqlite3changegroup_config(cpkt_sqlite_native_changegroup(self),
                                   operation, value);
}

static int cpkt_sqlite_changegroup_schema(cpkt_sqlite_changegroup *self,
                                          cpkt_sqlite *database,
                                          const char *schema_name) {
  if (cpkt_sqlite_native_changegroup(self) == NULL ||
      cpkt_sqlite_native(database) == NULL) {
    return CPKT_SQLITE_MISUSE;
  }
  return sqlite3changegroup_schema(cpkt_sqlite_native_changegroup(self),
                                   cpkt_sqlite_native(database), schema_name);
}

static int cpkt_sqlite_changegroup_add(cpkt_sqlite_changegroup *self,
                                       const cpkt_sqlite_changeset *changeset) {
  if (cpkt_sqlite_native_changegroup(self) == NULL ||
      !cpkt_sqlite_changeset_has_valid_data(changeset))
    return CPKT_SQLITE_MISUSE;
  return sqlite3changegroup_add(cpkt_sqlite_native_changegroup(self),
                                changeset->byte_count, (void *)changeset->data);
}

static int
cpkt_sqlite_changegroup_add_strm(cpkt_sqlite_changegroup *self,
                                 cpkt_sqlite_stream_input_callback input,
                                 void *context) {
  cpkt_sqlite_stream_input_context input_context;
  if (cpkt_sqlite_native_changegroup(self) == NULL || input == NULL) {
    return CPKT_SQLITE_MISUSE;
  }
  input_context.callback = input;
  input_context.user_context = context;
  return sqlite3changegroup_add_strm(cpkt_sqlite_native_changegroup(self),
                                     cpkt_sqlite_stream_input_trampoline,
                                     &input_context);
}

static int
cpkt_sqlite_changegroup_add_change(cpkt_sqlite_changegroup *self,
                                   cpkt_sqlite_changeset_iterator *iterator) {
  if (cpkt_sqlite_native_changegroup(self) == NULL ||
      cpkt_sqlite_native_iterator(iterator) == NULL)
    return CPKT_SQLITE_MISUSE;
  return sqlite3changegroup_add_change(cpkt_sqlite_native_changegroup(self),
                                       cpkt_sqlite_native_iterator(iterator));
}

static int cpkt_sqlite_changegroup_output(cpkt_sqlite_changegroup *self,
                                          cpkt_sqlite_changeset **out) {
  void *data;
  int byte_count;
  int status;
  if (out != NULL)
    *out = NULL;
  if (cpkt_sqlite_native_changegroup(self) == NULL || out == NULL) {
    return CPKT_SQLITE_MISUSE;
  }
  data = NULL;
  byte_count = 0;
  status = sqlite3changegroup_output(cpkt_sqlite_native_changegroup(self),
                                     &byte_count, &data);
  if (status != SQLITE_OK)
    return status;
  return cpkt_sqlite_changeset_new_owned(data, byte_count, out);
}

static int
cpkt_sqlite_changegroup_output_strm(cpkt_sqlite_changegroup *self,
                                    cpkt_sqlite_stream_output_callback output,
                                    void *context) {
  cpkt_sqlite_stream_output_context output_context;
  if (cpkt_sqlite_native_changegroup(self) == NULL || output == NULL) {
    return CPKT_SQLITE_MISUSE;
  }
  output_context.callback = output;
  output_context.user_context = context;
  return sqlite3changegroup_output_strm(cpkt_sqlite_native_changegroup(self),
                                        cpkt_sqlite_stream_output_trampoline,
                                        &output_context);
}

static int cpkt_sqlite_changegroup_change_begin(cpkt_sqlite_changegroup *self,
                                                int operation,
                                                const char *table_name,
                                                int indirect,
                                                char **error_out) {
  if (error_out != NULL)
    *error_out = NULL;
  if (cpkt_sqlite_native_changegroup(self) == NULL || table_name == NULL) {
    return CPKT_SQLITE_MISUSE;
  }
  return sqlite3changegroup_change_begin(cpkt_sqlite_native_changegroup(self),
                                         operation, table_name, indirect,
                                         error_out);
}

static int cpkt_sqlite_changegroup_change_i64(cpkt_sqlite_changegroup *self,
                                              int is_new, int column,
                                              cpkt_sqlite_i64 value) {
  if (cpkt_sqlite_native_changegroup(self) == NULL)
    return CPKT_SQLITE_MISUSE;
  return sqlite3changegroup_change_int64(cpkt_sqlite_native_changegroup(self),
                                         is_new, column,
                                         cpkt_sqlite_native_i64(value));
}

static int cpkt_sqlite_changegroup_change_null(cpkt_sqlite_changegroup *self,
                                               int is_new, int column) {
  if (cpkt_sqlite_native_changegroup(self) == NULL)
    return CPKT_SQLITE_MISUSE;
  return sqlite3changegroup_change_null(cpkt_sqlite_native_changegroup(self),
                                        is_new, column);
}

static int cpkt_sqlite_changegroup_change_double(cpkt_sqlite_changegroup *self,
                                                 int is_new, int column,
                                                 double value) {
  if (cpkt_sqlite_native_changegroup(self) == NULL)
    return CPKT_SQLITE_MISUSE;
  return sqlite3changegroup_change_double(cpkt_sqlite_native_changegroup(self),
                                          is_new, column, value);
}

static int cpkt_sqlite_changegroup_change_text(cpkt_sqlite_changegroup *self,
                                               int is_new, int column,
                                               const char *value,
                                               int byte_count) {
  if (cpkt_sqlite_native_changegroup(self) == NULL || value == NULL) {
    return CPKT_SQLITE_MISUSE;
  }
  return sqlite3changegroup_change_text(cpkt_sqlite_native_changegroup(self),
                                        is_new, column, value, byte_count);
}

static int cpkt_sqlite_changegroup_change_blob(cpkt_sqlite_changegroup *self,
                                               int is_new, int column,
                                               const void *value,
                                               int byte_count) {
  if (cpkt_sqlite_native_changegroup(self) == NULL || value == NULL) {
    return CPKT_SQLITE_MISUSE;
  }
  return sqlite3changegroup_change_blob(cpkt_sqlite_native_changegroup(self),
                                        is_new, column, value, byte_count);
}

static int cpkt_sqlite_changegroup_change_finish(cpkt_sqlite_changegroup *self,
                                                 int discard,
                                                 char **error_out) {
  if (error_out != NULL)
    *error_out = NULL;
  if (cpkt_sqlite_native_changegroup(self) == NULL)
    return CPKT_SQLITE_MISUSE;
  return sqlite3changegroup_change_finish(cpkt_sqlite_native_changegroup(self),
                                          discard, error_out);
}

static void cpkt_sqlite_changegroup_close(cpkt_sqlite_changegroup *self) {
  if (self == NULL)
    return;
  if (cpkt_sqlite_native_changegroup(self) != NULL) {
    sqlite3changegroup_delete(cpkt_sqlite_native_changegroup(self));
  }
  self->changegroup = NULL;
  free(self);
}

int cpkt_sqlite_changegroup_new(cpkt_sqlite_changegroup **out) {
  sqlite3_changegroup *native_changegroup;
  cpkt_sqlite_changegroup *public_changegroup;
  int status;
  if (out != NULL)
    *out = NULL;
  if (out == NULL)
    return CPKT_SQLITE_MISUSE;
  native_changegroup = NULL;
  status = sqlite3changegroup_new(&native_changegroup);
  if (status != SQLITE_OK)
    return status;
  public_changegroup =
      (cpkt_sqlite_changegroup *)calloc(1, sizeof(*public_changegroup));
  if (public_changegroup == NULL) {
    sqlite3changegroup_delete(native_changegroup);
    return CPKT_SQLITE_NOMEM;
  }
  public_changegroup->config = cpkt_sqlite_changegroup_config;
  public_changegroup->schema = cpkt_sqlite_changegroup_schema;
  public_changegroup->add = cpkt_sqlite_changegroup_add;
  public_changegroup->add_strm = cpkt_sqlite_changegroup_add_strm;
  public_changegroup->add_change = cpkt_sqlite_changegroup_add_change;
  public_changegroup->output = cpkt_sqlite_changegroup_output;
  public_changegroup->output_strm = cpkt_sqlite_changegroup_output_strm;
  public_changegroup->change_begin = cpkt_sqlite_changegroup_change_begin;
  public_changegroup->change_i64 = cpkt_sqlite_changegroup_change_i64;
  public_changegroup->change_null = cpkt_sqlite_changegroup_change_null;
  public_changegroup->change_double = cpkt_sqlite_changegroup_change_double;
  public_changegroup->change_text = cpkt_sqlite_changegroup_change_text;
  public_changegroup->change_blob = cpkt_sqlite_changegroup_change_blob;
  public_changegroup->change_finish = cpkt_sqlite_changegroup_change_finish;
  public_changegroup->close = cpkt_sqlite_changegroup_close;
  public_changegroup->changegroup = native_changegroup;
  *out = public_changegroup;
  return CPKT_SQLITE_OK;
}

static void cpkt_sqlite_string_append(cpkt_sqlite_string *self,
                                      const char *text, int byte_count) {
  if (cpkt_sqlite_native_string(self) != NULL) {
    sqlite3_str_append(cpkt_sqlite_native_string(self), text, byte_count);
  }
}

static void cpkt_sqlite_string_append_all(cpkt_sqlite_string *self,
                                          const char *text) {
  if (cpkt_sqlite_native_string(self) != NULL)
    sqlite3_str_appendall(cpkt_sqlite_native_string(self), text);
}

static void cpkt_sqlite_string_append_char(cpkt_sqlite_string *self, int count,
                                           char character) {
  if (cpkt_sqlite_native_string(self) != NULL) {
    sqlite3_str_appendchar(cpkt_sqlite_native_string(self), count, character);
  }
}

static void cpkt_sqlite_string_append_format_v(cpkt_sqlite_string *self,
                                               const char *format,
                                               va_list arguments) {
  if (cpkt_sqlite_native_string(self) != NULL && format != NULL) {
    sqlite3_str_vappendf(cpkt_sqlite_native_string(self), format, arguments);
  }
}

static void cpkt_sqlite_string_append_format(cpkt_sqlite_string *self,
                                             const char *format, ...) {
  va_list arguments;
  va_start(arguments, format);
  cpkt_sqlite_string_append_format_v(self, format, arguments);
  va_end(arguments);
}

void cpkt_sqlite_string_append_format_i64(cpkt_sqlite_string *self,
                                          const char *format,
                                          cpkt_sqlite_i64 value) {
  if (cpkt_sqlite_native_string(self) != NULL && format != NULL)
    sqlite3_str_appendf(cpkt_sqlite_native_string(self), format,
                        cpkt_sqlite_native_i64(value));
}

void cpkt_sqlite_string_append_format_u64(cpkt_sqlite_string *self,
                                          const char *format,
                                          cpkt_sqlite_u64 value) {
  if (cpkt_sqlite_native_string(self) != NULL && format != NULL)
    sqlite3_str_appendf(cpkt_sqlite_native_string(self), format,
                        cpkt_sqlite_native_u64(value));
}

static void cpkt_sqlite_string_reset(cpkt_sqlite_string *self) {
  if (cpkt_sqlite_native_string(self) != NULL)
    sqlite3_str_reset(cpkt_sqlite_native_string(self));
}

static void cpkt_sqlite_string_truncate(cpkt_sqlite_string *self,
                                        int byte_count) {
  if (cpkt_sqlite_native_string(self) != NULL)
    sqlite3_str_truncate(cpkt_sqlite_native_string(self), byte_count);
}

static int cpkt_sqlite_string_error_code(const cpkt_sqlite_string *self) {
  return cpkt_sqlite_native_string(self) == NULL
             ? CPKT_SQLITE_MISUSE
             : sqlite3_str_errcode(cpkt_sqlite_native_string(self));
}

static int cpkt_sqlite_string_length(const cpkt_sqlite_string *self) {
  return cpkt_sqlite_native_string(self) == NULL
             ? 0
             : sqlite3_str_length(cpkt_sqlite_native_string(self));
}

static const char *cpkt_sqlite_string_value(const cpkt_sqlite_string *self) {
  return cpkt_sqlite_native_string(self) == NULL
             ? NULL
             : sqlite3_str_value(cpkt_sqlite_native_string(self));
}

static char *cpkt_sqlite_string_finish(cpkt_sqlite_string *self) {
  char *value;
  if (self == NULL || cpkt_sqlite_native_string(self) == NULL)
    return NULL;
  value = sqlite3_str_finish(cpkt_sqlite_native_string(self));
  self->string = NULL;
  free(self);
  return value;
}

static void cpkt_sqlite_string_close(cpkt_sqlite_string *self) {
  if (self == NULL)
    return;
  if (cpkt_sqlite_native_string(self) != NULL)
    sqlite3_str_free(cpkt_sqlite_native_string(self));
  self->string = NULL;
  free(self);
}

cpkt_sqlite_string *cpkt_sqlite_string_new(cpkt_sqlite *database) {
  sqlite3_str *native_string;
  cpkt_sqlite_string *public_string;
  native_string =
      sqlite3_str_new(database == NULL ? NULL : cpkt_sqlite_native(database));
  if (native_string == NULL)
    return NULL;
  public_string = (cpkt_sqlite_string *)calloc(1, sizeof(*public_string));
  if (public_string == NULL) {
    sqlite3_str_free(native_string);
    return NULL;
  }
  public_string->append = cpkt_sqlite_string_append;
  public_string->append_all = cpkt_sqlite_string_append_all;
  public_string->append_char = cpkt_sqlite_string_append_char;
  public_string->append_format = cpkt_sqlite_string_append_format;
  public_string->append_format_v = cpkt_sqlite_string_append_format_v;
  public_string->reset = cpkt_sqlite_string_reset;
  public_string->truncate = cpkt_sqlite_string_truncate;
  public_string->error_code = cpkt_sqlite_string_error_code;
  public_string->length = cpkt_sqlite_string_length;
  public_string->value = cpkt_sqlite_string_value;
  public_string->finish = cpkt_sqlite_string_finish;
  public_string->close = cpkt_sqlite_string_close;
  public_string->string = native_string;
  return public_string;
}
