#include "sqlite_native_views.h"

#include <stdlib.h>
#include <string.h>

typedef struct cpkt_sqlite_native_file_binding {
  sqlite3_file *native;
  cpkt_sqlite_io_methods methods;
  int owns_native;
  int owns_public;
} cpkt_sqlite_native_file_binding;

typedef struct cpkt_sqlite_native_vfs_binding {
  sqlite3_vfs *native;
} cpkt_sqlite_native_vfs_binding;

static sqlite3_int64 cpkt_sqlite_view_native_i64(cpkt_sqlite_i64 value) {
  sqlite3_uint64 bits;
  bits = ((sqlite3_uint64)(value.high & 0xffffffffUL) << 32) |
         (sqlite3_uint64)(value.low & 0xffffffffUL);
  return (sqlite3_int64)bits;
}

static cpkt_sqlite_i64 cpkt_sqlite_view_public_i64(sqlite3_int64 value) {
  sqlite3_uint64 bits;
  cpkt_sqlite_i64 result;
  bits = (sqlite3_uint64)value;
  result.high = (unsigned long)((bits >> 32) & 0xffffffffUL);
  result.low = (unsigned long)(bits & 0xffffffffUL);
  return result;
}

static cpkt_sqlite_native_file_binding *
cpkt_sqlite_view_file(const cpkt_sqlite_file *file) {
  return file == NULL ? NULL
                      : (cpkt_sqlite_native_file_binding *)file->internal;
}

static int cpkt_sqlite_view_file_close(cpkt_sqlite_file *file) {
  cpkt_sqlite_native_file_binding *binding;
  int status;
  binding = cpkt_sqlite_view_file(file);
  if (binding == NULL)
    return SQLITE_MISUSE;
  status = SQLITE_OK;
  if (binding->owns_native && binding->native != NULL &&
      binding->native->pMethods != NULL &&
      binding->native->pMethods->xClose != NULL)
    status = binding->native->pMethods->xClose(binding->native);
  if (binding->owns_native)
    free(binding->native);
  file->methods = NULL;
  file->internal = NULL;
  if (binding->owns_public)
    free(file);
  free(binding);
  return status;
}

static int cpkt_sqlite_view_file_borrowed_close(cpkt_sqlite_file *file) {
  (void)file;
  return SQLITE_MISUSE;
}

/** Release borrowed file view metadata without closing SQLite's file. */
void cpkt_sqlite_native_file_view_release(cpkt_sqlite_file *view) {
  cpkt_sqlite_native_file_binding *binding;
  binding = cpkt_sqlite_view_file(view);
  if (binding == NULL || binding->owns_native)
    return;
  view->methods = NULL;
  view->internal = NULL;
  if (binding->owns_public)
    free(view);
  free(binding);
}

/** Free an independently owned view, retaining SQLite's underlying file. */
static int cpkt_sqlite_view_file_metadata_close(cpkt_sqlite_file *file) {
  cpkt_sqlite_native_file_binding *binding;
  binding = cpkt_sqlite_view_file(file);
  if (binding == NULL || binding->owns_native || !binding->owns_public)
    return SQLITE_MISUSE;
  cpkt_sqlite_native_file_view_release(file);
  return SQLITE_OK;
}

static int cpkt_sqlite_view_file_read(cpkt_sqlite_file *file, void *buffer,
                                      int count, cpkt_sqlite_i64 offset) {
  cpkt_sqlite_native_file_binding *binding;
  binding = cpkt_sqlite_view_file(file);
  if (binding == NULL || buffer == NULL || count < 0)
    return SQLITE_MISUSE;
  return binding->native->pMethods->xRead(binding->native, buffer, count,
                                          cpkt_sqlite_view_native_i64(offset));
}

static int cpkt_sqlite_view_file_write(cpkt_sqlite_file *file,
                                       const void *buffer, int count,
                                       cpkt_sqlite_i64 offset) {
  cpkt_sqlite_native_file_binding *binding;
  binding = cpkt_sqlite_view_file(file);
  if (binding == NULL || buffer == NULL || count < 0)
    return SQLITE_MISUSE;
  return binding->native->pMethods->xWrite(binding->native, buffer, count,
                                           cpkt_sqlite_view_native_i64(offset));
}

static int cpkt_sqlite_view_file_truncate(cpkt_sqlite_file *file,
                                          cpkt_sqlite_i64 size) {
  cpkt_sqlite_native_file_binding *binding;
  binding = cpkt_sqlite_view_file(file);
  if (binding == NULL)
    return SQLITE_MISUSE;
  return binding->native->pMethods->xTruncate(
      binding->native, cpkt_sqlite_view_native_i64(size));
}

static int cpkt_sqlite_view_file_sync(cpkt_sqlite_file *file, int flags) {
  cpkt_sqlite_native_file_binding *binding;
  binding = cpkt_sqlite_view_file(file);
  if (binding == NULL)
    return SQLITE_MISUSE;
  return binding->native->pMethods->xSync(binding->native, flags);
}

static int cpkt_sqlite_view_file_size(cpkt_sqlite_file *file,
                                      cpkt_sqlite_i64 *out) {
  cpkt_sqlite_native_file_binding *binding;
  sqlite3_int64 native_size;
  int status;
  binding = cpkt_sqlite_view_file(file);
  if (binding == NULL || out == NULL)
    return SQLITE_MISUSE;
  native_size = 0;
  status = binding->native->pMethods->xFileSize(binding->native, &native_size);
  if (status == SQLITE_OK)
    *out = cpkt_sqlite_view_public_i64(native_size);
  return status;
}

static int cpkt_sqlite_view_file_lock(cpkt_sqlite_file *file, int level) {
  cpkt_sqlite_native_file_binding *binding;
  binding = cpkt_sqlite_view_file(file);
  return binding == NULL
             ? SQLITE_MISUSE
             : binding->native->pMethods->xLock(binding->native, level);
}

static int cpkt_sqlite_view_file_unlock(cpkt_sqlite_file *file, int level) {
  cpkt_sqlite_native_file_binding *binding;
  binding = cpkt_sqlite_view_file(file);
  return binding == NULL
             ? SQLITE_MISUSE
             : binding->native->pMethods->xUnlock(binding->native, level);
}

static int cpkt_sqlite_view_file_reserved(cpkt_sqlite_file *file, int *out) {
  cpkt_sqlite_native_file_binding *binding;
  binding = cpkt_sqlite_view_file(file);
  if (binding == NULL || out == NULL)
    return SQLITE_MISUSE;
  return binding->native->pMethods->xCheckReservedLock(binding->native, out);
}

static int cpkt_sqlite_view_file_control(cpkt_sqlite_file *file, int operation,
                                         void *argument) {
  cpkt_sqlite_native_file_binding *binding;
  sqlite3_int64 native_value;
  int status;
  binding = cpkt_sqlite_view_file(file);
  if (binding == NULL)
    return SQLITE_MISUSE;
  if (argument == NULL || (operation != SQLITE_FCNTL_SIZE_HINT &&
                           operation != SQLITE_FCNTL_MMAP_SIZE &&
                           operation != SQLITE_FCNTL_SIZE_LIMIT))
    return binding->native->pMethods->xFileControl(binding->native, operation,
                                                   argument);
  native_value = cpkt_sqlite_view_native_i64(*(cpkt_sqlite_i64 *)argument);
  status = binding->native->pMethods->xFileControl(binding->native, operation,
                                                   &native_value);
  if (status == SQLITE_OK)
    *(cpkt_sqlite_i64 *)argument = cpkt_sqlite_view_public_i64(native_value);
  return status;
}

static int cpkt_sqlite_view_file_sector_size(cpkt_sqlite_file *file) {
  cpkt_sqlite_native_file_binding *binding;
  binding = cpkt_sqlite_view_file(file);
  return binding == NULL
             ? 0
             : binding->native->pMethods->xSectorSize(binding->native);
}

static int cpkt_sqlite_view_file_characteristics(cpkt_sqlite_file *file) {
  cpkt_sqlite_native_file_binding *binding;
  binding = cpkt_sqlite_view_file(file);
  return binding == NULL ? 0
                         : binding->native->pMethods->xDeviceCharacteristics(
                               binding->native);
}

static int cpkt_sqlite_view_file_shm_map(cpkt_sqlite_file *file, int page,
                                         int page_size, int extend,
                                         void volatile **out) {
  cpkt_sqlite_native_file_binding *binding;
  binding = cpkt_sqlite_view_file(file);
  if (binding == NULL || out == NULL)
    return SQLITE_MISUSE;
  return binding->native->pMethods->xShmMap(binding->native, page, page_size,
                                            extend, out);
}

static int cpkt_sqlite_view_file_shm_lock(cpkt_sqlite_file *file, int offset,
                                          int count, int flags) {
  cpkt_sqlite_native_file_binding *binding;
  binding = cpkt_sqlite_view_file(file);
  return binding == NULL ? SQLITE_MISUSE
                         : binding->native->pMethods->xShmLock(
                               binding->native, offset, count, flags);
}

static void cpkt_sqlite_view_file_shm_barrier(cpkt_sqlite_file *file) {
  cpkt_sqlite_native_file_binding *binding;
  binding = cpkt_sqlite_view_file(file);
  if (binding != NULL)
    binding->native->pMethods->xShmBarrier(binding->native);
}

static int cpkt_sqlite_view_file_shm_unmap(cpkt_sqlite_file *file,
                                           int delete_flag) {
  cpkt_sqlite_native_file_binding *binding;
  binding = cpkt_sqlite_view_file(file);
  return binding == NULL ? SQLITE_MISUSE
                         : binding->native->pMethods->xShmUnmap(binding->native,
                                                                delete_flag);
}

static int cpkt_sqlite_view_file_fetch(cpkt_sqlite_file *file,
                                       cpkt_sqlite_i64 offset, int count,
                                       void **out) {
  cpkt_sqlite_native_file_binding *binding;
  binding = cpkt_sqlite_view_file(file);
  if (binding == NULL || out == NULL)
    return SQLITE_MISUSE;
  return binding->native->pMethods->xFetch(
      binding->native, cpkt_sqlite_view_native_i64(offset), count, out);
}

static int cpkt_sqlite_view_file_unfetch(cpkt_sqlite_file *file,
                                         cpkt_sqlite_i64 offset, void *memory) {
  cpkt_sqlite_native_file_binding *binding;
  binding = cpkt_sqlite_view_file(file);
  return binding == NULL ? SQLITE_MISUSE
                         : binding->native->pMethods->xUnfetch(
                               binding->native,
                               cpkt_sqlite_view_native_i64(offset), memory);
}

/** Bind a native file to a C89 file receiver. */
int cpkt_sqlite_native_file_view_init(cpkt_sqlite_file *public_file,
                                      sqlite3_file *native, int owns_native) {
  cpkt_sqlite_native_file_binding *binding;
  const sqlite3_io_methods *methods;
  if (public_file == NULL || native == NULL || native->pMethods == NULL)
    return SQLITE_MISUSE;
  binding = (cpkt_sqlite_native_file_binding *)calloc(1, sizeof(*binding));
  if (binding == NULL)
    return SQLITE_NOMEM;
  methods = native->pMethods;
  binding->native = native;
  binding->owns_native = owns_native;
  binding->methods.version = methods->iVersion;
  binding->methods.close = owns_native ? cpkt_sqlite_view_file_close
                                       : cpkt_sqlite_view_file_borrowed_close;
  binding->methods.read =
      methods->xRead == NULL ? NULL : cpkt_sqlite_view_file_read;
  binding->methods.write =
      methods->xWrite == NULL ? NULL : cpkt_sqlite_view_file_write;
  binding->methods.truncate =
      methods->xTruncate == NULL ? NULL : cpkt_sqlite_view_file_truncate;
  binding->methods.sync =
      methods->xSync == NULL ? NULL : cpkt_sqlite_view_file_sync;
  binding->methods.size =
      methods->xFileSize == NULL ? NULL : cpkt_sqlite_view_file_size;
  binding->methods.lock =
      methods->xLock == NULL ? NULL : cpkt_sqlite_view_file_lock;
  binding->methods.unlock =
      methods->xUnlock == NULL ? NULL : cpkt_sqlite_view_file_unlock;
  binding->methods.check_reserved_lock = methods->xCheckReservedLock == NULL
                                             ? NULL
                                             : cpkt_sqlite_view_file_reserved;
  binding->methods.control =
      methods->xFileControl == NULL ? NULL : cpkt_sqlite_view_file_control;
  binding->methods.sector_size =
      methods->xSectorSize == NULL ? NULL : cpkt_sqlite_view_file_sector_size;
  binding->methods.characteristics =
      methods->xDeviceCharacteristics == NULL
          ? NULL
          : cpkt_sqlite_view_file_characteristics;
  if (methods->iVersion >= 2) {
    binding->methods.shm_map =
        methods->xShmMap == NULL ? NULL : cpkt_sqlite_view_file_shm_map;
    binding->methods.shm_lock =
        methods->xShmLock == NULL ? NULL : cpkt_sqlite_view_file_shm_lock;
    binding->methods.shm_barrier =
        methods->xShmBarrier == NULL ? NULL : cpkt_sqlite_view_file_shm_barrier;
    binding->methods.shm_unmap =
        methods->xShmUnmap == NULL ? NULL : cpkt_sqlite_view_file_shm_unmap;
  }
  if (methods->iVersion >= 3) {
    binding->methods.fetch =
        methods->xFetch == NULL ? NULL : cpkt_sqlite_view_file_fetch;
    binding->methods.unfetch =
        methods->xUnfetch == NULL ? NULL : cpkt_sqlite_view_file_unfetch;
  }
  public_file->methods = &binding->methods;
  public_file->internal = binding;
  public_file->state = NULL;
  return SQLITE_OK;
}

/** Allocate a borrowed C89 file receiver for a native open file. */
cpkt_sqlite_file *cpkt_sqlite_native_file_view(sqlite3_file *native) {
  cpkt_sqlite_file *file;
  cpkt_sqlite_native_file_binding *binding;
  file = (cpkt_sqlite_file *)calloc(1, sizeof(*file));
  if (file == NULL)
    return NULL;
  if (cpkt_sqlite_native_file_view_init(file, native, 0) != SQLITE_OK) {
    free(file);
    return NULL;
  }
  binding = cpkt_sqlite_view_file(file);
  binding->owns_public = 1;
  return file;
}

/** Own only the metadata for a borrowed journal/WAL database-file view. */
cpkt_sqlite_file *cpkt_sqlite_native_file_metadata_view(sqlite3_file *native) {
  cpkt_sqlite_file *view;
  cpkt_sqlite_native_file_binding *binding;
  view = cpkt_sqlite_native_file_view(native);
  if (view == NULL)
    return NULL;
  binding = cpkt_sqlite_view_file(view);
  binding->methods.close = cpkt_sqlite_view_file_metadata_close;
  return view;
}

static sqlite3_vfs *cpkt_sqlite_view_vfs(cpkt_sqlite_vfs *view) {
  cpkt_sqlite_native_vfs_binding *binding;
  binding =
      view == NULL ? NULL : (cpkt_sqlite_native_vfs_binding *)view->internal;
  return binding == NULL ? NULL : binding->native;
}

static int cpkt_sqlite_view_vfs_register(cpkt_sqlite_vfs *view,
                                         int make_default) {
  sqlite3_vfs *native;
  native = cpkt_sqlite_view_vfs(view);
  return native == NULL ? SQLITE_MISUSE
                        : sqlite3_vfs_register(native, make_default);
}

static int cpkt_sqlite_view_vfs_unregister(cpkt_sqlite_vfs *view) {
  sqlite3_vfs *native;
  native = cpkt_sqlite_view_vfs(view);
  return native == NULL ? SQLITE_MISUSE : sqlite3_vfs_unregister(native);
}

static void cpkt_sqlite_view_vfs_close(cpkt_sqlite_vfs *view) {
  if (view == NULL)
    return;
  free(view->internal);
  free(view);
}

static int cpkt_sqlite_view_vfs_open(cpkt_sqlite_vfs *view, const char *name,
                                     cpkt_sqlite_file *file, int flags,
                                     int *flags_out) {
  sqlite3_vfs *native_vfs;
  sqlite3_file *native_file;
  int status;
  native_vfs = cpkt_sqlite_view_vfs(view);
  if (native_vfs == NULL || file == NULL || file->internal != NULL ||
      native_vfs->xOpen == NULL || native_vfs->szOsFile <= 0)
    return SQLITE_MISUSE;
  native_file = (sqlite3_file *)calloc(1, (size_t)native_vfs->szOsFile);
  if (native_file == NULL)
    return SQLITE_NOMEM;
  status = native_vfs->xOpen(native_vfs, name, native_file, flags, flags_out);
  if (status != SQLITE_OK) {
    if (native_file->pMethods != NULL && native_file->pMethods->xClose != NULL)
      (void)native_file->pMethods->xClose(native_file);
    free(native_file);
    return status;
  }
  status = cpkt_sqlite_native_file_view_init(file, native_file, 1);
  if (status != SQLITE_OK) {
    (void)native_file->pMethods->xClose(native_file);
    free(native_file);
  }
  return status;
}

static int cpkt_sqlite_view_vfs_delete(cpkt_sqlite_vfs *view, const char *name,
                                       int sync_directory) {
  sqlite3_vfs *native;
  native = cpkt_sqlite_view_vfs(view);
  return native == NULL || native->xDelete == NULL
             ? SQLITE_MISUSE
             : native->xDelete(native, name, sync_directory);
}

static int cpkt_sqlite_view_vfs_access(cpkt_sqlite_vfs *view, const char *name,
                                       int flags, int *out) {
  sqlite3_vfs *native;
  native = cpkt_sqlite_view_vfs(view);
  if (native == NULL || native->xAccess == NULL || name == NULL || out == NULL)
    return SQLITE_MISUSE;
  return native->xAccess(native, name, flags, out);
}

static int cpkt_sqlite_view_vfs_full_path(cpkt_sqlite_vfs *view,
                                          const char *name, int count,
                                          char *output) {
  sqlite3_vfs *native;
  native = cpkt_sqlite_view_vfs(view);
  if (native == NULL || native->xFullPathname == NULL || name == NULL ||
      count <= 0 || output == NULL)
    return SQLITE_MISUSE;
  return native->xFullPathname(native, name, count, output);
}

static void *cpkt_sqlite_view_vfs_dl_open(cpkt_sqlite_vfs *view,
                                          const char *name) {
  sqlite3_vfs *native;
  native = cpkt_sqlite_view_vfs(view);
  return native == NULL || native->xDlOpen == NULL
             ? NULL
             : native->xDlOpen(native, name);
}

static void cpkt_sqlite_view_vfs_dl_error(cpkt_sqlite_vfs *view, int count,
                                          char *message) {
  sqlite3_vfs *native;
  native = cpkt_sqlite_view_vfs(view);
  if (native != NULL && native->xDlError != NULL)
    native->xDlError(native, count, message);
}

static cpkt_sqlite_vfs_symbol_callback
cpkt_sqlite_view_vfs_dl_symbol(cpkt_sqlite_vfs *view, void *handle,
                               const char *name) {
  sqlite3_vfs *native;
  native = cpkt_sqlite_view_vfs(view);
  return native == NULL || native->xDlSym == NULL
             ? NULL
             : native->xDlSym(native, handle, name);
}

static void cpkt_sqlite_view_vfs_dl_close(cpkt_sqlite_vfs *view, void *handle) {
  sqlite3_vfs *native;
  native = cpkt_sqlite_view_vfs(view);
  if (native != NULL && native->xDlClose != NULL)
    native->xDlClose(native, handle);
}

static int cpkt_sqlite_view_vfs_randomness(cpkt_sqlite_vfs *view, int count,
                                           char *output) {
  sqlite3_vfs *native;
  native = cpkt_sqlite_view_vfs(view);
  return native == NULL || native->xRandomness == NULL
             ? 0
             : native->xRandomness(native, count, output);
}

static int cpkt_sqlite_view_vfs_sleep(cpkt_sqlite_vfs *view, int microseconds) {
  sqlite3_vfs *native;
  native = cpkt_sqlite_view_vfs(view);
  return native == NULL || native->xSleep == NULL
             ? 0
             : native->xSleep(native, microseconds);
}

static int cpkt_sqlite_view_vfs_time(cpkt_sqlite_vfs *view, double *out) {
  sqlite3_vfs *native;
  native = cpkt_sqlite_view_vfs(view);
  if (native == NULL || native->xCurrentTime == NULL || out == NULL)
    return SQLITE_MISUSE;
  return native->xCurrentTime(native, out);
}

static int cpkt_sqlite_view_vfs_error(cpkt_sqlite_vfs *view, int count,
                                      char *message) {
  sqlite3_vfs *native;
  native = cpkt_sqlite_view_vfs(view);
  return native == NULL || native->xGetLastError == NULL
             ? SQLITE_MISUSE
             : native->xGetLastError(native, count, message);
}

static int cpkt_sqlite_view_vfs_time_i64(cpkt_sqlite_vfs *view,
                                         cpkt_sqlite_i64 *out) {
  sqlite3_vfs *native;
  sqlite3_int64 native_value;
  int status;
  native = cpkt_sqlite_view_vfs(view);
  if (native == NULL || native->iVersion < 2 ||
      native->xCurrentTimeInt64 == NULL || out == NULL)
    return SQLITE_MISUSE;
  status = native->xCurrentTimeInt64(native, &native_value);
  if (status == SQLITE_OK)
    *out = cpkt_sqlite_view_public_i64(native_value);
  return status;
}

static int
cpkt_sqlite_view_vfs_set_syscall(cpkt_sqlite_vfs *view, const char *name,
                                 cpkt_sqlite_vfs_symbol_callback callback) {
  sqlite3_vfs *native;
  native = cpkt_sqlite_view_vfs(view);
  return native == NULL || native->iVersion < 3 ||
                 native->xSetSystemCall == NULL
             ? SQLITE_MISUSE
             : native->xSetSystemCall(native, name, callback);
}

static cpkt_sqlite_vfs_symbol_callback
cpkt_sqlite_view_vfs_get_syscall(cpkt_sqlite_vfs *view, const char *name) {
  sqlite3_vfs *native;
  native = cpkt_sqlite_view_vfs(view);
  return native == NULL || native->iVersion < 3 ||
                 native->xGetSystemCall == NULL
             ? NULL
             : native->xGetSystemCall(native, name);
}

static const char *cpkt_sqlite_view_vfs_next_syscall(cpkt_sqlite_vfs *view,
                                                     const char *name) {
  sqlite3_vfs *native;
  native = cpkt_sqlite_view_vfs(view);
  return native == NULL || native->iVersion < 3 ||
                 native->xNextSystemCall == NULL
             ? NULL
             : native->xNextSystemCall(native, name);
}

/** Allocate a callable C89 view of a native VFS registration. */
cpkt_sqlite_vfs *cpkt_sqlite_native_vfs_view(sqlite3_vfs *native) {
  cpkt_sqlite_native_vfs_binding *binding;
  cpkt_sqlite_vfs *view;
  if (native == NULL)
    return NULL;
  view = (cpkt_sqlite_vfs *)calloc(1, sizeof(*view));
  binding = (cpkt_sqlite_native_vfs_binding *)calloc(1, sizeof(*binding));
  if (view == NULL || binding == NULL) {
    free(view);
    free(binding);
    return NULL;
  }
  binding->native = native;
  view->internal = binding;
  view->register_vfs = cpkt_sqlite_view_vfs_register;
  view->unregister_vfs = cpkt_sqlite_view_vfs_unregister;
  view->close = cpkt_sqlite_view_vfs_close;
  view->name = native->zName;
  view->maximum_pathname_bytes = native->mxPathname;
  view->methods.version = native->iVersion;
  view->methods.open = cpkt_sqlite_view_vfs_open;
  view->methods.delete_file =
      native->xDelete == NULL ? NULL : cpkt_sqlite_view_vfs_delete;
  view->methods.access =
      native->xAccess == NULL ? NULL : cpkt_sqlite_view_vfs_access;
  view->methods.full_path =
      native->xFullPathname == NULL ? NULL : cpkt_sqlite_view_vfs_full_path;
  view->methods.dl_open =
      native->xDlOpen == NULL ? NULL : cpkt_sqlite_view_vfs_dl_open;
  view->methods.dl_error =
      native->xDlError == NULL ? NULL : cpkt_sqlite_view_vfs_dl_error;
  view->methods.dl_symbol =
      native->xDlSym == NULL ? NULL : cpkt_sqlite_view_vfs_dl_symbol;
  view->methods.dl_close =
      native->xDlClose == NULL ? NULL : cpkt_sqlite_view_vfs_dl_close;
  view->methods.randomness =
      native->xRandomness == NULL ? NULL : cpkt_sqlite_view_vfs_randomness;
  view->methods.sleep =
      native->xSleep == NULL ? NULL : cpkt_sqlite_view_vfs_sleep;
  view->methods.current_time =
      native->xCurrentTime == NULL ? NULL : cpkt_sqlite_view_vfs_time;
  view->methods.last_error =
      native->xGetLastError == NULL ? NULL : cpkt_sqlite_view_vfs_error;
  if (native->iVersion >= 2)
    view->methods.current_time_i64 = native->xCurrentTimeInt64 == NULL
                                         ? NULL
                                         : cpkt_sqlite_view_vfs_time_i64;
  if (native->iVersion >= 3) {
    view->methods.set_system_call = native->xSetSystemCall == NULL
                                        ? NULL
                                        : cpkt_sqlite_view_vfs_set_syscall;
    view->methods.get_system_call = native->xGetSystemCall == NULL
                                        ? NULL
                                        : cpkt_sqlite_view_vfs_get_syscall;
    view->methods.next_system_call = native->xNextSystemCall == NULL
                                         ? NULL
                                         : cpkt_sqlite_view_vfs_next_syscall;
  }
  return view;
}

/** Identify a native VFS view when converting file-control payloads. */
int cpkt_sqlite_native_vfs_view_is(const cpkt_sqlite_vfs *view) {
  return view != NULL && view->close == cpkt_sqlite_view_vfs_close;
}

/** Register the provider referenced by an owned native VFS view. */
int cpkt_sqlite_native_vfs_view_register(cpkt_sqlite_vfs *view,
                                         int make_default) {
  return cpkt_sqlite_native_vfs_view_is(view)
             ? cpkt_sqlite_view_vfs_register(view, make_default)
             : SQLITE_MISUSE;
}

/** Unregister the provider referenced by an owned native VFS view. */
int cpkt_sqlite_native_vfs_view_unregister(cpkt_sqlite_vfs *view) {
  return cpkt_sqlite_native_vfs_view_is(view)
             ? cpkt_sqlite_view_vfs_unregister(view)
             : SQLITE_MISUSE;
}

/** Release a native VFS view without unregistering its provider. */
void cpkt_sqlite_native_vfs_view_close(cpkt_sqlite_vfs *view) {
  if (cpkt_sqlite_native_vfs_view_is(view))
    cpkt_sqlite_view_vfs_close(view);
}
