#include <cpkt/sqlite.h>

#include <sqlite3.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static sqlite3_vfs *base_vfs;
static cpkt_sqlite_io_methods io_methods;
static int mmap_control_calls;
static int size_hint_control_calls;
static int size_limit_control_calls;
static int file_close_calls;
static sqlite3_mem_methods original_memory;
static int fail_proxy_allocation;
static int proxy_mode;
static const char auto_proxy_path[] = ":auto: (not held)";
static char proxy_path[64] = "/tmp/cpkt-owned-lock-proxy";
int cpkt_test_borrowed_file_view(cpkt_sqlite *database, int *close_calls);
int cpkt_test_vfs_provider_identity(cpkt_sqlite *database,
                                    cpkt_sqlite_vfs *original);

static void *proxy_malloc(int size) {
  return fail_proxy_allocation ? NULL : original_memory.xMalloc(size);
}

/* Model both forms returned by Darwin's native proxy file-control method. */
const char *cpkt_test_proxy_path(int mode) {
  proxy_mode = mode;
  fail_proxy_allocation = 0;
  strcpy(proxy_path, "/tmp/cpkt-owned-lock-proxy");
  return mode == 1 ? proxy_path : auto_proxy_path;
}

void cpkt_test_proxy_path_changed(void) { strcpy(proxy_path, "/tmp/changed"); }

static sqlite3_file *native_file(cpkt_sqlite_file *file) {
  return (sqlite3_file *)file->state;
}

static sqlite3_int64 native_offset(cpkt_sqlite_i64 offset) {
  sqlite3_uint64 bits = ((sqlite3_uint64)(uint32_t)offset.high << 32) |
                        (sqlite3_uint64)(uint32_t)offset.low;
  return (sqlite3_int64)bits;
}

static int file_close(cpkt_sqlite_file *file) {
  sqlite3_file *native = native_file(file);
  int status = native->pMethods->xClose(native);
  ++file_close_calls;
  free(native);
  return status;
}

static int file_read(cpkt_sqlite_file *file, void *buffer, int count,
                     cpkt_sqlite_i64 offset) {
  sqlite3_file *native = native_file(file);
  return native->pMethods->xRead(native, buffer, count, native_offset(offset));
}

static int file_write(cpkt_sqlite_file *file, const void *buffer, int count,
                      cpkt_sqlite_i64 offset) {
  sqlite3_file *native = native_file(file);
  return native->pMethods->xWrite(native, buffer, count, native_offset(offset));
}

static int file_truncate(cpkt_sqlite_file *file, cpkt_sqlite_i64 size) {
  sqlite3_file *native = native_file(file);
  return native->pMethods->xTruncate(native, native_offset(size));
}

static int file_sync(cpkt_sqlite_file *file, int flags) {
  sqlite3_file *native = native_file(file);
  return native->pMethods->xSync(native, flags);
}

static int file_size(cpkt_sqlite_file *file, cpkt_sqlite_i64 *size_out) {
  sqlite3_file *native = native_file(file);
  sqlite3_int64 size;
  int status = native->pMethods->xFileSize(native, &size);
  if (status == SQLITE_OK) {
    size_out->high = (unsigned long)((uint64_t)size >> 32);
    size_out->low = (unsigned long)((uint64_t)size & UINT32_MAX);
  }
  return status;
}

static int file_lock(cpkt_sqlite_file *file, int level) {
  sqlite3_file *native = native_file(file);
  return native->pMethods->xLock(native, level);
}

static int file_unlock(cpkt_sqlite_file *file, int level) {
  sqlite3_file *native = native_file(file);
  return native->pMethods->xUnlock(native, level);
}

static int file_reserved_lock(cpkt_sqlite_file *file, int *result_out) {
  sqlite3_file *native = native_file(file);
  return native->pMethods->xCheckReservedLock(native, result_out);
}

static int file_control(cpkt_sqlite_file *file, int operation, void *argument) {
  sqlite3_file *native = native_file(file);
  if (operation == SQLITE_FCNTL_GET_LOCKPROXYFILE) {
    *(const char **)argument = proxy_mode == 2   ? NULL
                               : proxy_mode == 1 ? proxy_path
                                                 : auto_proxy_path;
    if (proxy_mode == 4)
      fail_proxy_allocation = 1;
    return proxy_mode == 3 ? SQLITE_IOERR : SQLITE_OK;
  }
  if (argument != NULL && (operation == SQLITE_FCNTL_SIZE_HINT ||
                           operation == SQLITE_FCNTL_MMAP_SIZE ||
                           operation == SQLITE_FCNTL_SIZE_LIMIT)) {
    cpkt_sqlite_i64 *public_value = (cpkt_sqlite_i64 *)argument;
    sqlite3_int64 native_value = native_offset(*public_value);
    int status;
    if (operation == SQLITE_FCNTL_MMAP_SIZE)
      ++mmap_control_calls;
    if (operation == SQLITE_FCNTL_SIZE_HINT)
      ++size_hint_control_calls;
    if (operation == SQLITE_FCNTL_SIZE_LIMIT) {
      ++size_limit_control_calls;
      if (public_value->high != 1 || public_value->low != 2)
        return SQLITE_MISUSE;
      public_value->high = 3;
      public_value->low = 4;
      return SQLITE_OK;
    }
    status = native->pMethods->xFileControl(native, operation, &native_value);
    if (status == SQLITE_OK) {
      public_value->high = (unsigned long)((uint64_t)native_value >> 32);
      public_value->low = (unsigned long)((uint64_t)native_value & UINT32_MAX);
    }
    return status;
  }
  return native->pMethods->xFileControl(native, operation, argument);
}

static int file_sector_size(cpkt_sqlite_file *file) {
  sqlite3_file *native = native_file(file);
  return native->pMethods->xSectorSize(native);
}

static int file_characteristics(cpkt_sqlite_file *file) {
  sqlite3_file *native = native_file(file);
  return native->pMethods->xDeviceCharacteristics(native);
}

static int vfs_open(cpkt_sqlite_vfs *vfs, const char *name,
                    cpkt_sqlite_file *file, int flags, int *flags_out) {
  sqlite3_file *native;
  int status;
  (void)vfs;
  native = (sqlite3_file *)calloc(1, (size_t)base_vfs->szOsFile);
  if (native == NULL)
    return SQLITE_NOMEM;
  status = base_vfs->xOpen(base_vfs, name, native, flags, flags_out);
  if (status != SQLITE_OK) {
    free(native);
    return status;
  }
  file->state = native;
  file->methods = &io_methods;
  return SQLITE_OK;
}

static int vfs_delete(cpkt_sqlite_vfs *vfs, const char *name, int sync_dir) {
  (void)vfs;
  return base_vfs->xDelete(base_vfs, name, sync_dir);
}

static int vfs_access(cpkt_sqlite_vfs *vfs, const char *name, int flags,
                      int *result_out) {
  if (strcmp(name, "cpkt-vfs-identity-probe") == 0) {
    *result_out = *(int *)vfs->state;
    return SQLITE_OK;
  }
  return base_vfs->xAccess(base_vfs, name, flags, result_out);
}

static int vfs_full_path(cpkt_sqlite_vfs *vfs, const char *name, int size,
                         char *output) {
  (void)vfs;
  return base_vfs->xFullPathname(base_vfs, name, size, output);
}

static int vfs_randomness(cpkt_sqlite_vfs *vfs, int size, char *output) {
  (void)vfs;
  return base_vfs->xRandomness(base_vfs, size, output);
}

static int vfs_sleep(cpkt_sqlite_vfs *vfs, int microseconds) {
  (void)vfs;
  return base_vfs->xSleep(base_vfs, microseconds);
}

static int vfs_current_time(cpkt_sqlite_vfs *vfs, double *time_out) {
  (void)vfs;
  return base_vfs->xCurrentTime(base_vfs, time_out);
}

static int capture_journal_mode(void *context, int count,
                                const char *const *values,
                                const char *const *names) {
  char *mode = (char *)context;
  (void)names;
  if (count > 0 && values[0] != NULL)
    snprintf(mode, 16, "%s", values[0]);
  return 0;
}

int main(int argc, char **argv) {
  cpkt_sqlite_vfs_methods methods;
  cpkt_sqlite_vfs *vfs;
  cpkt_sqlite *database;
  char journal_mode[16] = {0};
  cpkt_sqlite_i64 control_value;
  int status;
  int marker = 11;
  sqlite3_mem_methods memory;

  if (argc != 2 && argc != 3)
    return 1;
  if (sqlite3_config(SQLITE_CONFIG_GETMALLOC, &original_memory) != SQLITE_OK)
    return 1;
  memory = original_memory;
  memory.xMalloc = proxy_malloc;
  if (sqlite3_config(SQLITE_CONFIG_MALLOC, &memory) != SQLITE_OK ||
      cpkt_sqlite_initialize() != CPKT_SQLITE_OK)
    return 1;
  base_vfs = sqlite3_vfs_find(NULL);
  if (base_vfs == NULL)
    return 2;
  memset(&io_methods, 0, sizeof(io_methods));
  io_methods.version = 3;
  io_methods.close = file_close;
  io_methods.read = file_read;
  io_methods.write = file_write;
  io_methods.truncate = file_truncate;
  io_methods.sync = file_sync;
  io_methods.size = file_size;
  io_methods.lock = file_lock;
  io_methods.unlock = file_unlock;
  io_methods.check_reserved_lock = file_reserved_lock;
  io_methods.control = file_control;
  io_methods.sector_size = file_sector_size;
  io_methods.characteristics = file_characteristics;
  memset(&methods, 0, sizeof(methods));
  methods.version = 1;
  methods.open = vfs_open;
  methods.delete_file = vfs_delete;
  methods.access = vfs_access;
  methods.full_path = vfs_full_path;
  methods.randomness = vfs_randomness;
  methods.sleep = vfs_sleep;
  methods.current_time = vfs_current_time;
  vfs = cpkt_sqlite_vfs_new("cpkt-no-shm-vfs", 1024, &marker, &methods);
  if (vfs == NULL || vfs->register_vfs(vfs, 0) != CPKT_SQLITE_OK)
    return 3;
  (void)remove(argv[1]);
  database = cpkt_sqlite_open(
      argv[1], CPKT_SQLITE_OPEN_READWRITE | CPKT_SQLITE_OPEN_CREATE,
      "cpkt-no-shm-vfs");
  if (database == NULL)
    return 4;
  if (argc == 3) {
    if (strcmp(argv[2], "borrowed-file") == 0)
      status = cpkt_test_borrowed_file_view(database, &file_close_calls);
    else if (strcmp(argv[2], "provider-identity") == 0)
      status = cpkt_test_vfs_provider_identity(database, vfs);
    else
      return 11;
    if (status != 0)
      return status;
    status = file_close_calls;
    database->close(database);
    if (strcmp(argv[2], "borrowed-file") == 0 && file_close_calls != status + 1)
      return 12;
    vfs->close(vfs);
    (void)remove(argv[1]);
    return 0;
  }
  status = database->tx(database, "PRAGMA journal_mode=WAL",
                        capture_journal_mode, journal_mode);
  if (status != CPKT_SQLITE_OK || strcmp(journal_mode, "delete") != 0) {
    fprintf(stderr, "journal_mode=WAL returned %d, mode '%s'\n", status,
            journal_mode);
    return 5;
  }
  status = database->tx(database, "CREATE TABLE t(value INTEGER)", NULL, NULL);
  if (status != CPKT_SQLITE_OK)
    return 6;
  control_value = cpkt_sqlite_i64_make(0, 1048576);
  status = cpkt_sqlite_file_control(database, "main", SQLITE_FCNTL_MMAP_SIZE,
                                    &control_value);
  if (status != CPKT_SQLITE_OK || mmap_control_calls == 0 ||
      control_value.high != 0)
    return 7;
  control_value = cpkt_sqlite_i64_make(0xffffffffUL, 0xffffffffUL);
  status = cpkt_sqlite_file_control(database, "main", SQLITE_FCNTL_MMAP_SIZE,
                                    &control_value);
  if (status != CPKT_SQLITE_OK || control_value.high != 0 ||
      control_value.low != 1048576)
    return 8;
  control_value = cpkt_sqlite_i64_make(0, 4096);
  status = cpkt_sqlite_file_control(database, "main", SQLITE_FCNTL_SIZE_HINT,
                                    &control_value);
  if (status != CPKT_SQLITE_OK || size_hint_control_calls == 0)
    return 9;
  control_value = cpkt_sqlite_i64_make(1, 2);
  status = cpkt_sqlite_file_control(database, "main", SQLITE_FCNTL_SIZE_LIMIT,
                                    &control_value);
  if (status != CPKT_SQLITE_OK || size_limit_control_calls != 1 ||
      control_value.high != 3 || control_value.low != 4)
    return 10;
  database->close(database);
  vfs->close(vfs);
  (void)remove(argv[1]);
  return 0;
}
