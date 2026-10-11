#include <cpkt/sqlite.h>

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
  cpkt_sqlite_mutex_methods mutex_methods;
  cpkt_sqlite_page_cache_methods cache_methods;
  cpkt_sqlite_page_cache_methods second_cache_methods;
  cpkt_sqlite_page_cache *cache;
  cpkt_sqlite_page *page;
  cpkt_sqlite_page *same_page;
  cpkt_sqlite_vfs *vfs;
  cpkt_sqlite_file sparse_file;
  cpkt_sqlite *database;
  cpkt_sqlite_file_handle *file_handle;
  cpkt_sqlite_vfs_handle *vfs_handle;
  cpkt_sqlite_file *file_view;
  cpkt_sqlite_vfs *vfs_view;
  cpkt_sqlite_i64 offset;
  cpkt_sqlite_i64 size;
  void *mutex;
  unsigned long version;
  char byte;
  char header[16];
  int accessible;
  int index;
  int status;
  int stage;
  if (argc != 3)
    return 2;
  stage = 1;
  cache = NULL;
  page = NULL;
  vfs = NULL;
  database = NULL;
  file_handle = NULL;
  vfs_handle = NULL;
  mutex = NULL;
  memset(&cache_methods, 0, sizeof(cache_methods));
  memset(&second_cache_methods, 0, sizeof(second_cache_methods));
  memset(&mutex_methods, 0, sizeof(mutex_methods));
  memset(&sparse_file, 0, sizeof(sparse_file));
  (void)remove(argv[1]);
  (void)remove(argv[2]);
  if (cpkt_sqlite_global_config_page_cache_methods_get(&cache_methods) !=
          CPKT_SQLITE_OK ||
      cache_methods.create == NULL || cache_methods.fetch == NULL ||
      cache_methods.unpin == NULL || cache_methods.destroy == NULL)
    goto fail;
  if (cpkt_sqlite_global_config_page_cache_methods_get(&second_cache_methods) !=
          CPKT_SQLITE_OK ||
      second_cache_methods.context == cache_methods.context ||
      second_cache_methods.create == NULL)
    goto fail;
  stage = 2;
  if (cpkt_sqlite_global_config_mutex_methods_get(&mutex_methods) !=
          CPKT_SQLITE_OK ||
      mutex_methods.allocate != NULL)
    goto fail;
  if (cpkt_sqlite_initialize() != CPKT_SQLITE_OK)
    goto fail;
  cache = cache_methods.create(cache_methods.context, 1024, 64, 1);
  if (cache == NULL)
    goto fail;
  page = cache_methods.fetch(cache, 1UL, 2);
  if (page == NULL || page->buffer == NULL || page->extra == NULL)
    goto fail;
  ((char *)page->buffer)[0] = 'x';
  same_page = cache_methods.fetch(cache, 1UL, 0);
  if (same_page != page || ((char *)same_page->buffer)[0] != 'x')
    goto fail;
  cache_methods.unpin(cache, page, 1);
  page = NULL;
  page = cache_methods.fetch(cache, 2UL, 2);
  if (page == NULL)
    goto fail;
  cache_methods.rekey(cache, page, 2UL, 3UL);
  if (cache_methods.fetch(cache, 3UL, 0) != page)
    goto fail;
  cache_methods.truncate(cache, 3UL);
  page = NULL;
  cache_methods.cache_size(cache, 512);
  for (index = 10; index < 150; ++index) {
    page = cache_methods.fetch(cache, (unsigned long)index, 2);
    if (page == NULL)
      goto fail;
  }
  if (cache_methods.fetch(cache, 10UL, 0) == NULL ||
      cache_methods.fetch(cache, 149UL, 0) != page)
    goto fail;
  cache_methods.truncate(cache, 100UL);
  page = NULL;
  page = cache_methods.fetch(cache, 4UL, 2);
  if (page == NULL)
    goto fail;
  cache_methods.destroy(cache);
  cache = NULL;
  page = NULL;
  cpkt_sqlite_page_cache_methods_release(&cache_methods);
  cpkt_sqlite_page_cache_methods_release(&second_cache_methods);
  stage = 3;
  vfs = cpkt_sqlite_vfs_find(NULL);
  if (vfs == NULL || vfs->methods.open == NULL || vfs->methods.access == NULL ||
      vfs->methods.full_path == NULL || vfs->methods.current_time_i64 == NULL)
    goto fail;
  accessible = -1;
  if (vfs->methods.access(vfs, argv[2], CPKT_SQLITE_ACCESS_EXISTS,
                          &accessible) != CPKT_SQLITE_OK ||
      accessible != 0)
    goto fail;
  status =
      vfs->methods.open(vfs, argv[2], &sparse_file,
                        CPKT_SQLITE_OPEN_MAIN_DB | CPKT_SQLITE_OPEN_READWRITE |
                            CPKT_SQLITE_OPEN_CREATE,
                        NULL);
  if (status != CPKT_SQLITE_OK || sparse_file.methods == NULL)
    goto fail;
  if (vfs->methods.open(vfs, argv[2], &sparse_file,
                        CPKT_SQLITE_OPEN_MAIN_DB | CPKT_SQLITE_OPEN_READWRITE,
                        NULL) != CPKT_SQLITE_MISUSE)
    goto fail;
  offset = cpkt_sqlite_i64_make(1UL, 4096UL);
  byte = 'Z';
  if (sparse_file.methods->write(&sparse_file, &byte, 1, offset) !=
          CPKT_SQLITE_OK ||
      sparse_file.methods->size(&sparse_file, &size) != CPKT_SQLITE_OK ||
      size.high != 1UL || size.low != 4097UL)
    goto fail;
  byte = 0;
  if (sparse_file.methods->read(&sparse_file, &byte, 1, offset) !=
          CPKT_SQLITE_OK ||
      byte != 'Z')
    goto fail;
  if (sparse_file.methods->close(&sparse_file) != CPKT_SQLITE_OK)
    goto fail;
  if (vfs->methods.delete_file(vfs, argv[2], 0) != CPKT_SQLITE_OK)
    goto fail;
  vfs->close(vfs);
  vfs = NULL;
  stage = 4;
  database = cpkt_sqlite_open(
      argv[1], CPKT_SQLITE_OPEN_READWRITE | CPKT_SQLITE_OPEN_CREATE, NULL);
  if (database == NULL ||
      database->tx(database,
                   "CREATE TABLE t(x INTEGER); INSERT INTO t VALUES(9)", NULL,
                   NULL) != CPKT_SQLITE_OK)
    goto fail;
  if (cpkt_sqlite_file_control_file(database, "main",
                                    CPKT_SQLITE_FCNTL_FILE_POINTER,
                                    &file_handle) != CPKT_SQLITE_OK)
    goto fail;
  file_view = cpkt_sqlite_file_handle_view(file_handle);
  if (file_view == NULL || file_view->methods == NULL ||
      file_view->methods->close(file_view) != CPKT_SQLITE_MISUSE ||
      file_view->methods->size(file_view, &size) != CPKT_SQLITE_OK ||
      size.high != 0UL || size.low < 16UL ||
      file_view->methods->read(file_view, header, 16,
                               cpkt_sqlite_i64_make(0UL, 0UL)) !=
          CPKT_SQLITE_OK ||
      memcmp(header, "SQLite format 3", 15) != 0)
    goto fail;
  if (cpkt_sqlite_file_control_vfs(database, "main", &vfs_handle) !=
      CPKT_SQLITE_OK)
    goto fail;
  vfs_view = cpkt_sqlite_vfs_handle_view(vfs_handle);
  accessible = 0;
  if (vfs_view == NULL || vfs_view->methods.access == NULL ||
      vfs_view->methods.access(vfs_view, argv[1], CPKT_SQLITE_ACCESS_EXISTS,
                               &accessible) != CPKT_SQLITE_OK ||
      accessible != 1 || cpkt_sqlite_vfs_handle_name(vfs_handle) == NULL)
    goto fail;
  version = 0;
  if (cpkt_sqlite_file_control_unsigned(database, "main",
                                        CPKT_SQLITE_FCNTL_DATA_VERSION,
                                        &version) != CPKT_SQLITE_OK ||
      version == 0UL)
    goto fail;
  cpkt_sqlite_vfs_handle_close(vfs_handle);
  vfs_handle = NULL;
  cpkt_sqlite_file_handle_close(file_handle);
  file_handle = NULL;
  database->close(database);
  database = NULL;
  stage = 5;
  if (cpkt_sqlite_shutdown() != CPKT_SQLITE_OK ||
      cpkt_sqlite_global_config_mutex_methods_get(&mutex_methods) !=
          CPKT_SQLITE_OK ||
      mutex_methods.allocate == NULL ||
      cpkt_sqlite_global_config_mutex_methods_set(&mutex_methods) !=
          CPKT_SQLITE_OK ||
      cpkt_sqlite_initialize() != CPKT_SQLITE_OK)
    goto fail;
  mutex = mutex_methods.allocate(mutex_methods.context, CPKT_SQLITE_MUTEX_FAST);
  if (mutex == NULL)
    goto fail;
  mutex_methods.enter(mutex_methods.context, mutex);
  mutex_methods.leave(mutex_methods.context, mutex);
  mutex_methods.free(mutex_methods.context, mutex);
  mutex = NULL;
  if (cpkt_sqlite_shutdown() != CPKT_SQLITE_OK)
    goto fail;
  cpkt_sqlite_mutex_methods_release(&mutex_methods);
  (void)remove(argv[1]);
  (void)remove(argv[2]);
  return 0;
fail:
  fprintf(stderr, "sqlite native views failed at stage %d\n", stage);
  if (mutex != NULL && mutex_methods.free != NULL)
    mutex_methods.free(mutex_methods.context, mutex);
  if (vfs_handle != NULL)
    cpkt_sqlite_vfs_handle_close(vfs_handle);
  if (file_handle != NULL)
    cpkt_sqlite_file_handle_close(file_handle);
  if (database != NULL)
    database->close(database);
  if (sparse_file.methods != NULL)
    (void)sparse_file.methods->close(&sparse_file);
  if (vfs != NULL)
    vfs->close(vfs);
  if (page != NULL && cache != NULL)
    cache_methods.unpin(cache, page, 1);
  if (cache != NULL)
    cache_methods.destroy(cache);
  cpkt_sqlite_page_cache_methods_release(&cache_methods);
  cpkt_sqlite_page_cache_methods_release(&second_cache_methods);
  cpkt_sqlite_mutex_methods_release(&mutex_methods);
  (void)cpkt_sqlite_shutdown();
  (void)remove(argv[1]);
  (void)remove(argv[2]);
  return stage;
}
