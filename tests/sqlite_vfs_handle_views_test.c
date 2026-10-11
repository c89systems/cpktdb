#include <cpkt/sqlite.h>

#include <stdio.h>
#include <string.h>

int cpkt_test_borrowed_file_view(cpkt_sqlite *database, int *close_calls);
int cpkt_test_vfs_provider_identity(cpkt_sqlite *database,
                                    cpkt_sqlite_vfs *original);
const char *cpkt_test_proxy_path(int mode);
void cpkt_test_proxy_path_changed(void);

static int owned_control_text(cpkt_sqlite *database) {
  const char *borrowed;
  char *owned;
  char expected[64];
  int mode;
  int status;
  for (mode = 0; mode < 5; ++mode) {
    borrowed = cpkt_test_proxy_path(mode);
    strcpy(expected, borrowed);
    owned = (char *)borrowed;
    status = cpkt_sqlite_file_control_text(
        database, "main", CPKT_SQLITE_FCNTL_GET_LOCKPROXYFILE, &owned);
    (void)cpkt_test_proxy_path(0); /* Clear allocation fault after the call. */
    if (mode == 3 || mode == 4) {
      if (status != (mode == 3 ? CPKT_SQLITE_IOERR : CPKT_SQLITE_NOMEM) ||
          owned != NULL)
        return 50 + mode;
    } else if (status != CPKT_SQLITE_OK ||
               (mode == 2 ? owned != NULL
                          : owned == NULL || owned == borrowed ||
                                strcmp(owned, expected) != 0)) {
      fprintf(stderr, "lock-proxy file control returned borrowed text\n");
      return 50 + mode;
    }
    if (mode == 1) {
      cpkt_test_proxy_path_changed();
      if (strcmp(owned, expected) != 0)
        return 55;
    }
    cpkt_sqlite_free(owned);
  }
  return 0;
}

int cpkt_test_borrowed_file_view(cpkt_sqlite *database, int *close_calls) {
  cpkt_sqlite_file_handle *first;
  cpkt_sqlite_file_handle *second;
  cpkt_sqlite_file *view;
  cpkt_sqlite_i64 size;
  int before;
  int status;
  first = NULL;
  second = NULL;
  if (database->tx(database, "CREATE TABLE borrowed(value INTEGER)", NULL,
                   NULL) != CPKT_SQLITE_OK)
    return 30;
  status = owned_control_text(database);
  if (status != 0)
    return status;
  if (cpkt_sqlite_file_control_file(database, "main",
                                    CPKT_SQLITE_FCNTL_FILE_POINTER,
                                    &first) != CPKT_SQLITE_OK ||
      cpkt_sqlite_file_control_file(database, "main",
                                    CPKT_SQLITE_FCNTL_FILE_POINTER,
                                    &second) != CPKT_SQLITE_OK)
    return 30;
  view = cpkt_sqlite_file_handle_view(first);
  before = *close_calls;
  if (view == NULL || view->methods->close(view) != CPKT_SQLITE_MISUSE ||
      *close_calls != before || view == cpkt_sqlite_file_handle_view(second)) {
    fprintf(stderr,
            "borrowed file view exposed an owning close or shared metadata\n");
    return 31;
  }
  if (view->methods->size(view, &size) != CPKT_SQLITE_OK ||
      cpkt_sqlite_file_handle_size(first, &size) != CPKT_SQLITE_OK)
    return 32;
  cpkt_sqlite_file_handle_close(first);
  view = cpkt_sqlite_file_handle_view(second);
  if (*close_calls != before ||
      view->methods->close(view) != CPKT_SQLITE_MISUSE ||
      view->methods->size(view, &size) != CPKT_SQLITE_OK)
    return 33;
  cpkt_sqlite_file_handle_close(second);
  if (*close_calls != before ||
      database->tx(database, "INSERT INTO borrowed VALUES(1)", NULL, NULL) !=
          CPKT_SQLITE_OK)
    return 34;
  if (database->tx(database, "BEGIN; INSERT INTO borrowed VALUES(2)", NULL,
                   NULL) != CPKT_SQLITE_OK ||
      cpkt_sqlite_file_control_file(database, "main",
                                    CPKT_SQLITE_FCNTL_JOURNAL_POINTER,
                                    &first) != CPKT_SQLITE_OK)
    return 35;
  before = *close_calls;
  view = cpkt_sqlite_file_handle_view(first);
  if (view == NULL || view->methods->close(view) != CPKT_SQLITE_MISUSE ||
      *close_calls != before ||
      view->methods->size(view, &size) != CPKT_SQLITE_OK)
    return 36;
  cpkt_sqlite_file_handle_close(first);
  if (*close_calls != before ||
      database->tx(database, "COMMIT", NULL, NULL) != CPKT_SQLITE_OK)
    return 37;
  return 0;
}

static int check_provider(cpkt_sqlite_vfs_handle *handle) {
  cpkt_sqlite_vfs *view;
  int direct;
  int through_view;
  direct = 0;
  through_view = 0;
  view = cpkt_sqlite_vfs_handle_view(handle);
  return view == NULL ||
         cpkt_sqlite_vfs_handle_access(handle, "cpkt-vfs-identity-probe", 0,
                                       &direct) != CPKT_SQLITE_OK ||
         view->methods.access(view, "cpkt-vfs-identity-probe", 0,
                              &through_view) != CPKT_SQLITE_OK ||
         direct != 11 || through_view != 11;
}

int cpkt_test_vfs_provider_identity(cpkt_sqlite *database,
                                    cpkt_sqlite_vfs *original) {
  cpkt_sqlite_vfs *replacement;
  cpkt_sqlite_vfs *found;
  cpkt_sqlite_vfs_handle *before;
  cpkt_sqlite_vfs_handle *after;
  cpkt_sqlite_vfs_handle *unregistered;
  int marker;
  marker = 22;
  before = NULL;
  after = NULL;
  unregistered = NULL;
  if (cpkt_sqlite_file_control_vfs(database, "main", &before) != CPKT_SQLITE_OK)
    return 40;
  replacement =
      cpkt_sqlite_vfs_new(original->name, original->maximum_pathname_bytes,
                          &marker, &original->methods);
  if (replacement == NULL ||
      replacement->register_vfs(replacement, 0) != CPKT_SQLITE_OK)
    return 41;
  found = cpkt_sqlite_vfs_find(original->name);
  if (found != replacement ||
      cpkt_sqlite_file_control_vfs(database, "main", &after) !=
          CPKT_SQLITE_OK ||
      check_provider(before) || check_provider(after)) {
    fprintf(stderr,
            "database handle view resolved a replacement provider by name\n");
    return 42;
  }
  if (original->unregister_vfs(original) != CPKT_SQLITE_OK ||
      replacement->unregister_vfs(replacement) != CPKT_SQLITE_OK ||
      cpkt_sqlite_vfs_find(original->name) != NULL ||
      cpkt_sqlite_file_control_vfs(database, "main", &unregistered) !=
          CPKT_SQLITE_OK ||
      check_provider(unregistered)) {
    fprintf(stderr, "database handle lost its unregistered provider\n");
    return 43;
  }
  cpkt_sqlite_vfs_handle_close(before);
  cpkt_sqlite_vfs_handle_close(after);
  if (check_provider(unregistered))
    return 44;
  cpkt_sqlite_vfs_handle_close(unregistered);
  replacement->close(replacement);
  return 0;
}
