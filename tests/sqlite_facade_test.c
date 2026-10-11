#include <cpkt/sqlite.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct stream_state {
  const void *input;
  int input_size;
  int input_offset;
  int output_size;
} stream_state;

typedef struct fts5_state {
  int create_count;
  int tokenize_count;
  int destroy_count;
  int binding_destroy_count;
  int auxiliary_count;
  int phrase_callback_count;
} fts5_state;

typedef struct collation_needed_state {
  int count;
  int status;
} collation_needed_state;

typedef struct autovacuum_state {
  int callback_count;
  int destroy_count;
} autovacuum_state;

typedef struct trace_state {
  int statement_count;
  int row_count;
  int profile_count;
} trace_state;

typedef struct rtree_state {
  int callback_count;
  int query_count;
} rtree_state;

typedef struct unlock_notify_state {
  int callback_count;
  int context_count;
  int context_matches;
} unlock_notify_state;

typedef struct preupdate_blob_state {
  int call_count;
  int column;
} preupdate_blob_state;

typedef struct vfs_probe_state {
  int open_count;
  int close_count;
} vfs_probe_state;

typedef struct auto_extension_state {
  int call_count;
  int increment;
} auto_extension_state;

typedef struct log_state {
  int call_count;
  int error_code;
  char message[128];
} log_state;

typedef struct virtual_table_state {
  int best_index_count;
  int filter_count;
  int column_count;
  int disconnect_count;
  int module_destroy_count;
  int function_count;
  int update_count;
  int eof;
} virtual_table_state;

static int virtual_table_increment = 1;
static int virtual_table_shadow_name_count;

static int vfs_probe_close(cpkt_sqlite_file *file) {
  vfs_probe_state *state;
  state = (vfs_probe_state *)file->state;
  if (state != 0)
    state->close_count += 1;
  return CPKT_SQLITE_OK;
}

static int vfs_probe_open(cpkt_sqlite_vfs *vfs, const char *name,
                          cpkt_sqlite_file *file, int flags, int *flags_out) {
  static cpkt_sqlite_io_methods methods;
  vfs_probe_state *state;
  (void)name;
  (void)flags;
  (void)flags_out;
  state = (vfs_probe_state *)vfs->state;
  if (state != 0)
    state->open_count += 1;
  methods.version = 1;
  methods.close = vfs_probe_close;
  file->methods = &methods;
  file->state = state;
  return CPKT_SQLITE_CANTOPEN;
}

static int vfs_probe_delete(cpkt_sqlite_vfs *vfs, const char *name,
                            int sync_directory) {
  (void)vfs;
  (void)name;
  (void)sync_directory;
  return CPKT_SQLITE_OK;
}

static int vfs_probe_access(cpkt_sqlite_vfs *vfs, const char *name, int flags,
                            int *result_out) {
  (void)vfs;
  (void)name;
  (void)flags;
  if (result_out != 0)
    *result_out = 0;
  return CPKT_SQLITE_OK;
}

static int vfs_probe_full_path(cpkt_sqlite_vfs *vfs, const char *name,
                               int output_byte_count, char *output) {
  (void)vfs;
  if (output_byte_count <= 0 || output == 0 || name == 0)
    return CPKT_SQLITE_CANTOPEN;
  output[0] = '\0';
  return CPKT_SQLITE_OK;
}

static int vfs_probe_randomness(cpkt_sqlite_vfs *vfs, int byte_count,
                                char *output) {
  (void)vfs;
  if (byte_count > 0 && output != 0)
    memset(output, 0, (size_t)byte_count);
  return byte_count;
}

static int vfs_probe_sleep(cpkt_sqlite_vfs *vfs, int microseconds) {
  (void)vfs;
  return microseconds;
}

static int vfs_probe_current_time(cpkt_sqlite_vfs *vfs,
                                  double *julian_day_out) {
  (void)vfs;
  if (julian_day_out != 0)
    *julian_day_out = 0.0;
  return CPKT_SQLITE_OK;
}

typedef struct page_cache_test_page page_cache_test_page;

typedef struct page_cache_test_cache {
  cpkt_sqlite_page_cache cache;
  page_cache_test_page *pages;
  int page_count;
} page_cache_test_cache;

typedef struct page_cache_test_state {
  int initialize_count;
  int shutdown_count;
  int create_count;
  int fetch_count;
  int unpin_count;
  int destroy_count;
} page_cache_test_state;

struct page_cache_test_page {
  cpkt_sqlite_page page;
  unsigned long key;
  page_cache_test_page *next;
};

static page_cache_test_page *page_cache_test_find(page_cache_test_cache *cache,
                                                  unsigned long key) {
  page_cache_test_page *page;
  page = cache->pages;
  while (page != 0) {
    if (page->key == key)
      return page;
    page = page->next;
  }
  return 0;
}

static void page_cache_test_page_free(page_cache_test_page *page) {
  if (page == 0)
    return;
  free(page->page.buffer);
  free(page->page.extra);
  free(page);
}

static void page_cache_test_remove(page_cache_test_cache *cache,
                                   page_cache_test_page *page) {
  page_cache_test_page **link;
  if (cache == 0 || page == 0)
    return;
  link = &cache->pages;
  while (*link != 0 && *link != page)
    link = &(*link)->next;
  if (*link == page) {
    *link = page->next;
    cache->page_count -= 1;
    page_cache_test_page_free(page);
  }
}

static int page_cache_test_initialize(void *context) {
  page_cache_test_state *state;
  state = (page_cache_test_state *)context;
  if (state == 0)
    return CPKT_SQLITE_MISUSE;
  state->initialize_count += 1;
  return CPKT_SQLITE_OK;
}

static void page_cache_test_shutdown(void *context) {
  page_cache_test_state *state;
  state = (page_cache_test_state *)context;
  if (state != 0)
    state->shutdown_count += 1;
}

static cpkt_sqlite_page_cache *page_cache_test_create(void *context,
                                                      int page_byte_count,
                                                      int extra_byte_count,
                                                      int purgeable) {
  page_cache_test_state *state;
  page_cache_test_cache *cache;
  (void)purgeable;
  if (page_byte_count <= 0 || extra_byte_count < 0)
    return 0;
  state = (page_cache_test_state *)context;
  cache = (page_cache_test_cache *)calloc(1, sizeof(*cache));
  if (cache == 0)
    return 0;
  cache->cache.state = state;
  if (state != 0)
    state->create_count += 1;
  return &cache->cache;
}

static void page_cache_test_cache_size(cpkt_sqlite_page_cache *cache,
                                       int suggested_page_count) {
  (void)cache;
  (void)suggested_page_count;
}

static int page_cache_test_page_count(cpkt_sqlite_page_cache *cache) {
  page_cache_test_cache *test_cache;
  test_cache = (page_cache_test_cache *)cache;
  return test_cache == 0 ? 0 : test_cache->page_count;
}

static cpkt_sqlite_page *page_cache_test_fetch(cpkt_sqlite_page_cache *cache,
                                               unsigned long key,
                                               int create_flag) {
  page_cache_test_cache *test_cache;
  page_cache_test_page *page;
  page_cache_test_state *state;
  test_cache = (page_cache_test_cache *)cache;
  if (test_cache == 0 || key == 0)
    return 0;
  state = (page_cache_test_state *)test_cache->cache.state;
  if (state != 0)
    state->fetch_count += 1;
  page = page_cache_test_find(test_cache, key);
  if (page != 0 || create_flag == 0)
    return page == 0 ? 0 : &page->page;
  page = (page_cache_test_page *)calloc(1, sizeof(*page));
  if (page == 0)
    return 0;
  page->page.buffer = calloc(1, 65536U);
  page->page.extra = calloc(1, 256U);
  if (page->page.buffer == 0 || page->page.extra == 0) {
    page_cache_test_page_free(page);
    return 0;
  }
  page->key = key;
  page->next = test_cache->pages;
  test_cache->pages = page;
  test_cache->page_count += 1;
  return &page->page;
}

static void page_cache_test_unpin(cpkt_sqlite_page_cache *cache,
                                  cpkt_sqlite_page *page, int discard) {
  page_cache_test_cache *test_cache;
  page_cache_test_state *state;
  test_cache = (page_cache_test_cache *)cache;
  if (test_cache == 0 || page == 0)
    return;
  state = (page_cache_test_state *)test_cache->cache.state;
  if (state != 0)
    state->unpin_count += 1;
  if (discard)
    page_cache_test_remove(test_cache, (page_cache_test_page *)page);
}

static void page_cache_test_rekey(cpkt_sqlite_page_cache *cache,
                                  cpkt_sqlite_page *page, unsigned long old_key,
                                  unsigned long new_key) {
  page_cache_test_page *test_page;
  (void)cache;
  (void)old_key;
  test_page = (page_cache_test_page *)page;
  if (test_page != 0)
    test_page->key = new_key;
}

static void page_cache_test_truncate(cpkt_sqlite_page_cache *cache,
                                     unsigned long limit) {
  (void)cache;
  (void)limit;
}

static void page_cache_test_destroy(cpkt_sqlite_page_cache *cache) {
  page_cache_test_cache *test_cache;
  page_cache_test_state *state;
  page_cache_test_page *page;
  page_cache_test_page *next;
  test_cache = (page_cache_test_cache *)cache;
  if (test_cache == 0)
    return;
  state = (page_cache_test_state *)test_cache->cache.state;
  if (state != 0)
    state->destroy_count += 1;
  page = test_cache->pages;
  while (page != 0) {
    next = page->next;
    page_cache_test_page_free(page);
    page = next;
  }
  free(test_cache);
}

static void page_cache_test_shrink(cpkt_sqlite_page_cache *cache) {
  (void)cache;
}

static int stream_input(void *context, void *buffer, int *byte_count) {
  stream_state *state;
  int available;
  int count;
  state = (stream_state *)context;
  if (state == 0 || buffer == 0 || byte_count == 0 || *byte_count < 0) {
    return CPKT_SQLITE_MISUSE;
  }
  available = state->input_size - state->input_offset;
  count = *byte_count;
  if (count > available)
    count = available;
  if (count > 0)
    memcpy(buffer, (const char *)state->input + state->input_offset, count);
  state->input_offset += count;
  *byte_count = count;
  return CPKT_SQLITE_OK;
}

static int stream_output(void *context, const void *buffer, int byte_count) {
  stream_state *state;
  state = (stream_state *)context;
  if (state == 0 || buffer == 0 || byte_count < 0)
    return CPKT_SQLITE_MISUSE;
  state->output_size += byte_count;
  return CPKT_SQLITE_OK;
}

static int accept_changeset_iterator(void *context,
                                     cpkt_sqlite_changeset_iterator *iterator) {
  (void)context;
  return iterator == 0 ? 0 : 1;
}

static int callback_count(void *context, int column_count,
                          const char *const *values, const char *const *names) {
  int *count;
  if (column_count != 2 || values == 0 || names == 0 ||
      strcmp(names[0], "name") != 0 || strcmp(values[0], "alpha") != 0)
    return 1;
  count = (int *)context;
  *count += 1;
  return 0;
}

static int needed_collation_compare(void *context, int left_byte_count,
                                    const void *left, int right_byte_count,
                                    const void *right) {
  int compare;
  int byte_count;
  (void)context;
  byte_count =
      left_byte_count < right_byte_count ? left_byte_count : right_byte_count;
  compare = memcmp(left, right, (size_t)byte_count);
  if (compare != 0)
    return compare;
  if (left_byte_count < right_byte_count)
    return -1;
  if (left_byte_count > right_byte_count)
    return 1;
  return 0;
}

static void collation_needed(void *context, cpkt_sqlite *database,
                             unsigned long text_representation,
                             const char *name) {
  collation_needed_state *state;
  state = (collation_needed_state *)context;
  if (state == 0 || database == 0 || name == 0)
    return;
  state->count += 1;
  state->status = cpkt_sqlite_create_collation(
      database, name, text_representation, 0, needed_collation_compare, 0);
}

static unsigned long autovacuum_callback(void *context, const char *schema_name,
                                         unsigned long database_page_count,
                                         unsigned long free_page_count,
                                         unsigned long page_byte_count) {
  autovacuum_state *state;
  (void)schema_name;
  (void)database_page_count;
  (void)page_byte_count;
  state = (autovacuum_state *)context;
  if (state != 0)
    state->callback_count += 1;
  return free_page_count;
}

static void autovacuum_destroy(void *context) {
  autovacuum_state *state;
  state = (autovacuum_state *)context;
  if (state != 0)
    state->destroy_count += 1;
}

static void trace_callback(void *context, cpkt_sqlite *database,
                           unsigned long event, const char *sql,
                           cpkt_sqlite_u64 elapsed_nanoseconds) {
  trace_state *state;
  (void)database;
  (void)elapsed_nanoseconds;
  state = (trace_state *)context;
  if (state == 0)
    return;
  if (event == CPKT_SQLITE_TRACE_STATEMENT && sql != 0)
    state->statement_count += 1;
  if (event == CPKT_SQLITE_TRACE_ROW && sql != 0)
    state->row_count += 1;
  if (event == CPKT_SQLITE_TRACE_PROFILE && sql != 0)
    state->profile_count += 1;
}

static void legacy_trace_callback(void *context, const char *sql) {
  trace_state *state;
  state = (trace_state *)context;
  if (state != 0 && sql != 0)
    state->statement_count += 1;
}

static void legacy_profile_callback(void *context, const char *sql,
                                    cpkt_sqlite_u64 elapsed_nanoseconds) {
  trace_state *state;
  (void)elapsed_nanoseconds;
  state = (trace_state *)context;
  if (state != 0 && sql != 0)
    state->profile_count += 1;
}

static void unlock_notify_callback(void *context, int context_count,
                                   void *const *contexts) {
  unlock_notify_state *state;
  state = (unlock_notify_state *)context;
  if (state == 0)
    return;
  state->callback_count += 1;
  state->context_count = context_count;
  state->context_matches =
      context_count == 1 && contexts != 0 && contexts[0] == context;
}

static void virtual_table_module_destroy(void *context) {
  virtual_table_state *state;
  state = (virtual_table_state *)context;
  if (state != 0)
    state->module_destroy_count += 1;
}

static int virtual_table_connect(void *context, cpkt_sqlite *database,
                                 int argument_count,
                                 const char *const *arguments,
                                 cpkt_sqlite_virtual_table **out,
                                 char **error_out) {
  cpkt_sqlite_virtual_table *table;
  (void)argument_count;
  (void)arguments;
  (void)error_out;
  if (context == 0 || database == 0 || out == 0)
    return CPKT_SQLITE_MISUSE;
  table = cpkt_sqlite_virtual_table_new(context);
  if (table == 0)
    return CPKT_SQLITE_NOMEM;
  if (cpkt_sqlite_declare_virtual_table(
          database, "create table x(value integer)") != CPKT_SQLITE_OK ||
      cpkt_sqlite_virtual_table_config_int(
          database, CPKT_SQLITE_VTAB_CONSTRAINT_SUPPORT, 1) != CPKT_SQLITE_OK) {
    free(table);
    return CPKT_SQLITE_ERROR;
  }
  *out = table;
  return CPKT_SQLITE_OK;
}

static int virtual_table_best_index(void *context,
                                    cpkt_sqlite_virtual_table *table,
                                    cpkt_sqlite_index_info *index_info) {
  virtual_table_state *state;
  state = (virtual_table_state *)context;
  if (state == 0 || table == 0 || index_info == 0)
    return CPKT_SQLITE_MISUSE;
  state->best_index_count += 1;
  index_info->estimated_cost = 1.0;
  index_info->estimated_rows = cpkt_sqlite_i64_make(0, 1);
  return CPKT_SQLITE_OK;
}

static int virtual_table_open(void *context, cpkt_sqlite_virtual_table *table,
                              cpkt_sqlite_virtual_cursor **out) {
  if (context == 0 || table == 0 || out == 0)
    return CPKT_SQLITE_MISUSE;
  *out = cpkt_sqlite_virtual_cursor_new(table, context);
  return *out == 0 ? CPKT_SQLITE_NOMEM : CPKT_SQLITE_OK;
}

static int virtual_table_close(void *context,
                               cpkt_sqlite_virtual_cursor *cursor) {
  (void)context;
  return cursor == 0 ? CPKT_SQLITE_MISUSE : CPKT_SQLITE_OK;
}

static int virtual_table_filter(void *context,
                                cpkt_sqlite_virtual_cursor *cursor,
                                int index_number, const char *index_string,
                                int argument_count,
                                cpkt_sqlite_value *const *arguments) {
  virtual_table_state *state;
  (void)index_number;
  (void)index_string;
  (void)argument_count;
  (void)arguments;
  state = (virtual_table_state *)context;
  if (state == 0 || cursor == 0)
    return CPKT_SQLITE_MISUSE;
  state->filter_count += 1;
  state->eof = 0;
  return CPKT_SQLITE_OK;
}

static int virtual_table_next(void *context,
                              cpkt_sqlite_virtual_cursor *cursor) {
  virtual_table_state *state;
  state = (virtual_table_state *)context;
  if (state == 0 || cursor == 0)
    return CPKT_SQLITE_MISUSE;
  state->eof = 1;
  return CPKT_SQLITE_OK;
}

static int virtual_table_eof(void *context,
                             cpkt_sqlite_virtual_cursor *cursor) {
  virtual_table_state *state;
  state = (virtual_table_state *)context;
  if (state == 0 || cursor == 0)
    return 1;
  return state->eof;
}

static int virtual_table_column(void *context,
                                cpkt_sqlite_virtual_cursor *cursor,
                                cpkt_sqlite_context *result, int column) {
  virtual_table_state *state;
  state = (virtual_table_state *)context;
  if (state == 0 || cursor == 0 || result == 0 || column != 0) {
    return CPKT_SQLITE_MISUSE;
  }
  state->column_count += 1;
  cpkt_sqlite_context_result_int(result, 42);
  return CPKT_SQLITE_OK;
}

static int virtual_table_rowid(void *context,
                               cpkt_sqlite_virtual_cursor *cursor,
                               cpkt_sqlite_i64 *rowid_out) {
  if (context == 0 || cursor == 0 || rowid_out == 0)
    return CPKT_SQLITE_MISUSE;
  *rowid_out = cpkt_sqlite_i64_make(0, 1);
  return CPKT_SQLITE_OK;
}

static int virtual_table_update(void *context, cpkt_sqlite_virtual_table *table,
                                int argument_count,
                                cpkt_sqlite_value *const *arguments,
                                cpkt_sqlite_i64 *rowid_out) {
  virtual_table_state *state;
  state = (virtual_table_state *)context;
  if (state == 0 || table == 0 || argument_count < 3 || arguments == 0 ||
      rowid_out == 0)
    return CPKT_SQLITE_MISUSE;
  state->update_count += 1;
  *rowid_out = cpkt_sqlite_i64_make(0, 2);
  return CPKT_SQLITE_OK;
}

static int virtual_table_disconnect(void *context,
                                    cpkt_sqlite_virtual_table *table) {
  virtual_table_state *state;
  state = (virtual_table_state *)context;
  if (state == 0 || table == 0)
    return CPKT_SQLITE_MISUSE;
  state->disconnect_count += 1;
  return CPKT_SQLITE_OK;
}

static int rtree_contains_point(void *context,
                                cpkt_sqlite_rtree_geometry *geometry,
                                int coordinate_count, double *coordinates,
                                int *within_out) {
  rtree_state *state;
  state = (rtree_state *)context;
  if (state == 0 || geometry == 0 || coordinate_count != 4 ||
      geometry->parameter_count != 2 || geometry->parameters == 0 ||
      coordinates == 0 || within_out == 0)
    return CPKT_SQLITE_MISUSE;
  state->callback_count += 1;
  *within_out = coordinates[0] <= geometry->parameters[0] &&
                coordinates[1] >= geometry->parameters[0] &&
                coordinates[2] <= geometry->parameters[1] &&
                coordinates[3] >= geometry->parameters[1];
  return CPKT_SQLITE_OK;
}

static int rtree_score_point(void *context, cpkt_sqlite_rtree_query *query) {
  rtree_state *state;
  state = (rtree_state *)context;
  if (state == 0 || query == 0 || query->parameter_count != 2 ||
      query->parameters == 0 || query->coordinates == 0 ||
      query->coordinate_count != 4)
    return CPKT_SQLITE_MISUSE;
  state->query_count += 1;
  query->within = CPKT_SQLITE_RTREE_FULLY_WITHIN;
  query->score = 0.0;
  return CPKT_SQLITE_OK;
}

static void plus_one(cpkt_sqlite_context *context, int argument_count,
                     cpkt_sqlite_value *const *arguments, void *user_data) {
  int increment;
  if (argument_count != 1 || arguments == 0 || arguments[0] == 0 ||
      user_data == 0) {
    cpkt_sqlite_context_result_error_code(context, CPKT_SQLITE_MISUSE);
    return;
  }
  increment = *(int *)user_data;
  cpkt_sqlite_context_result_int(context, cpkt_sqlite_value_int(arguments[0]) +
                                              increment);
}

static int auto_extension_initialize(cpkt_sqlite *database, char **error_out,
                                     void *context) {
  auto_extension_state *state;
  (void)error_out;
  state = (auto_extension_state *)context;
  if (database == 0 || state == 0)
    return CPKT_SQLITE_MISUSE;
  state->call_count += 1;
  return cpkt_sqlite_create_function(database, "auto_plus_one", 1,
                                     CPKT_SQLITE_UTF8, &state->increment,
                                     plus_one, 0, 0, 0);
}

static void record_log(void *context, int error_code, const char *message) {
  log_state *state;
  state = (log_state *)context;
  if (state == 0 || message == 0)
    return;
  state->call_count += 1;
  state->error_code = error_code;
  strncpy(state->message, message, sizeof(state->message) - 1);
  state->message[sizeof(state->message) - 1] = '\0';
}

static void bound_argument(cpkt_sqlite_context *context, int argument_count,
                           cpkt_sqlite_value *const *arguments,
                           void *user_data) {
  int *was_bound;
  if (context == 0 || argument_count != 1 || arguments == 0 ||
      arguments[0] == 0 || user_data == 0 ||
      cpkt_sqlite_context_user_data(context) != user_data) {
    cpkt_sqlite_context_result_error_code(context, CPKT_SQLITE_MISUSE);
    return;
  }
  was_bound = (int *)user_data;
  *was_bound = cpkt_sqlite_value_from_bind(arguments[0]);
  cpkt_sqlite_context_result_int(context, *was_bound);
}

static void auxiliary_data(cpkt_sqlite_context *context, int argument_count,
                           cpkt_sqlite_value *const *arguments,
                           void *user_data) {
  int *count;
  if (context == 0 || argument_count != 1 || arguments == 0 ||
      arguments[0] == 0 || user_data == 0) {
    cpkt_sqlite_context_result_error_code(context, CPKT_SQLITE_MISUSE);
    return;
  }
  count = (int *)cpkt_sqlite_context_auxdata(context, 0);
  if (count == 0) {
    count = (int *)user_data;
    if (cpkt_sqlite_context_set_auxdata(context, 0, count, 0) !=
        CPKT_SQLITE_OK) {
      cpkt_sqlite_context_result_error_code(context, CPKT_SQLITE_NOMEM);
      return;
    }
  }
  *count += 1;
  cpkt_sqlite_context_result_int(context, *count);
}

static void aggregate_counter_step(cpkt_sqlite_context *context,
                                   int argument_count,
                                   cpkt_sqlite_value *const *arguments,
                                   void *user_data) {
  int *last_count;
  (void)arguments;
  if (context == 0 || argument_count != 1 || user_data == 0)
    return;
  last_count = (int *)user_data;
  *last_count = cpkt_sqlite_context_aggregate_count(context);
}

static void aggregate_counter_final(cpkt_sqlite_context *context,
                                    int argument_count,
                                    cpkt_sqlite_value *const *arguments,
                                    void *user_data) {
  (void)arguments;
  (void)user_data;
  if (context == 0 || argument_count != 0)
    return;
  cpkt_sqlite_context_result_int(context,
                                 cpkt_sqlite_context_aggregate_count(context));
}

static void utf16_error(cpkt_sqlite_context *context, int argument_count,
                        cpkt_sqlite_value *const *arguments, void *user_data) {
  if (context == 0 || argument_count != 0 || arguments != 0 || user_data == 0) {
    cpkt_sqlite_context_result_error_code(context, CPKT_SQLITE_MISUSE);
    return;
  }
  cpkt_sqlite_context_result_error16(context, user_data, -1);
}

static void utf16le_text(cpkt_sqlite_context *context, int argument_count,
                         cpkt_sqlite_value *const *arguments, void *user_data) {
  if (context == 0 || argument_count != 0 || arguments != 0 || user_data == 0) {
    cpkt_sqlite_context_result_error_code(context, CPKT_SQLITE_MISUSE);
    return;
  }
  cpkt_sqlite_context_result_text16le(context, user_data, -1);
}

static void utf16be_text(cpkt_sqlite_context *context, int argument_count,
                         cpkt_sqlite_value *const *arguments, void *user_data) {
  if (context == 0 || argument_count != 0 || arguments != 0 || user_data == 0) {
    cpkt_sqlite_context_result_error_code(context, CPKT_SQLITE_MISUSE);
    return;
  }
  cpkt_sqlite_context_result_text16be(context, user_data, -1);
}

static int
virtual_table_find_function(void *context, cpkt_sqlite_virtual_table *table,
                            int argument_count, const char *name,
                            cpkt_sqlite_scalar_callback *function_out,
                            void **user_data_out) {
  virtual_table_state *state;
  state = (virtual_table_state *)context;
  if (state == 0 || table == 0 || function_out == 0 || user_data_out == 0 ||
      name == 0 || argument_count != 1 ||
      strcmp(name, "cpkt_vtab_plus_one") != 0) {
    return 0;
  }
  state->function_count += 1;
  *function_out = plus_one;
  *user_data_out = &virtual_table_increment;
  return 1;
}

static int virtual_table_shadow_name(const char *name) {
  if (name != 0 && strcmp(name, "shadow") == 0) {
    virtual_table_shadow_name_count += 1;
    return 1;
  }
  return 0;
}

static void update_count(void *context, int operation,
                         const char *database_name, const char *table_name,
                         cpkt_sqlite_i64 row_id) {
  int *count;
  (void)database_name;
  (void)row_id;
  count = (int *)context;
  if (operation == 18 && table_name != 0 && strcmp(table_name, "item") == 0)
    *count += 1;
}

static void preupdate_noop(void *context, cpkt_sqlite *database, int operation,
                           const char *database_name, const char *table_name,
                           cpkt_sqlite_i64 old_row_id,
                           cpkt_sqlite_i64 new_row_id) {
  (void)context;
  (void)database;
  (void)operation;
  (void)database_name;
  (void)table_name;
  (void)old_row_id;
  (void)new_row_id;
}

static void preupdate_blob_write(void *context, cpkt_sqlite *database,
                                 int operation, const char *database_name,
                                 const char *table_name,
                                 cpkt_sqlite_i64 old_row_id,
                                 cpkt_sqlite_i64 new_row_id) {
  preupdate_blob_state *state;
  (void)operation;
  (void)database_name;
  (void)table_name;
  (void)old_row_id;
  (void)new_row_id;
  state = (preupdate_blob_state *)context;
  if (state == 0 || database == 0)
    return;
  state->call_count += 1;
  state->column = database->preupdate_blob_write(database);
}

static int fts5_tokenizer_create(void *context, const char *const *arguments,
                                 int argument_count,
                                 cpkt_sqlite_fts5_tokenizer **out) {
  fts5_state *state;
  (void)arguments;
  (void)argument_count;
  state = (fts5_state *)context;
  if (state == 0 || out == 0)
    return CPKT_SQLITE_MISUSE;
  *out = cpkt_sqlite_fts5_tokenizer_new(state);
  if (*out == 0)
    return CPKT_SQLITE_NOMEM;
  state->create_count += 1;
  return CPKT_SQLITE_OK;
}

static void fts5_tokenizer_destroy(cpkt_sqlite_fts5_tokenizer *tokenizer,
                                   void *context) {
  fts5_state *state;
  state = (fts5_state *)context;
  if (state != 0 && cpkt_sqlite_fts5_tokenizer_state(tokenizer) == state) {
    state->destroy_count += 1;
  }
}

static int fts5_tokenizer_tokenize(cpkt_sqlite_fts5_tokenizer *tokenizer,
                                   void *context, int flags, const char *text,
                                   int text_byte_count, const char *locale,
                                   int locale_byte_count,
                                   cpkt_sqlite_fts5_token_callback token) {
  fts5_state *state;
  (void)flags;
  (void)locale;
  (void)locale_byte_count;
  state = (fts5_state *)cpkt_sqlite_fts5_tokenizer_state(tokenizer);
  if (state == 0 || context == 0 || text == 0 || text_byte_count < 0 ||
      token == 0) {
    return CPKT_SQLITE_MISUSE;
  }
  state->tokenize_count += 1;
  return token(context, 0, text, text_byte_count, 0, text_byte_count);
}

static void fts5_binding_destroy(void *context) {
  fts5_state *state;
  state = (fts5_state *)context;
  if (state != 0)
    state->binding_destroy_count += 1;
}

static int fts5_phrase_callback(cpkt_sqlite_fts5_context *context,
                                void *user_data) {
  fts5_state *state;
  state = (fts5_state *)user_data;
  if (state == NULL || context->user_data(context) != state)
    return CPKT_SQLITE_MISUSE;
  ++state->phrase_callback_count;
  return CPKT_SQLITE_OK;
}

static void fts5_auxiliary(cpkt_sqlite_fts5_context *fts_context,
                           cpkt_sqlite_context *sql_context, int argument_count,
                           cpkt_sqlite_value *const *arguments,
                           void *user_data) {
  fts5_state *state;
  cpkt_sqlite_i64 row_count;
  const char *text;
  int text_byte_count;
  int instance_count;
  int phrase;
  int column;
  int offset;
  state = (fts5_state *)user_data;
  if (state == 0 || fts_context == 0 || sql_context == 0 ||
      argument_count != 0 || arguments != 0 ||
      fts_context->user_data(fts_context) != state ||
      cpkt_sqlite_context_user_data(sql_context) != state ||
      fts_context->row_count(fts_context, &row_count) != CPKT_SQLITE_OK ||
      row_count.high != 0 || row_count.low != 1 ||
      fts_context->phrase_count(fts_context) != 1 ||
      fts_context->phrase_size(fts_context, 0) != 1 ||
      fts_context->instance_count(fts_context, &instance_count) !=
          CPKT_SQLITE_OK ||
      instance_count != 1 ||
      fts_context->instance(fts_context, 0, &phrase, &column, &offset) !=
          CPKT_SQLITE_OK ||
      phrase != 0 || column != 0 || offset != 0 ||
      fts_context->column_text(fts_context, 0, &text, &text_byte_count) !=
          CPKT_SQLITE_OK ||
      text == 0 || text_byte_count != 5 || memcmp(text, "alpha", 5) != 0 ||
      fts_context->query_token(fts_context, 0, 0, &text, &text_byte_count) !=
          CPKT_SQLITE_OK ||
      text == 0 || text_byte_count != 5 || memcmp(text, "alpha", 5) != 0 ||
      fts_context->query_phrase(fts_context, 0, state, fts5_phrase_callback) !=
          CPKT_SQLITE_OK ||
      state->phrase_callback_count != 1) {
    cpkt_sqlite_context_result_error_code(sql_context, CPKT_SQLITE_MISUSE);
    return;
  }
  state->auxiliary_count += 1;
  cpkt_sqlite_context_result_int(sql_context, instance_count);
}

int main(void) {
  cpkt_sqlite *db;
  cpkt_sqlite *page_cache_database;
  cpkt_sqlite *fts_database;
  cpkt_sqlite *utf16_database;
  cpkt_sqlite *replica;
  cpkt_sqlite *unlock_database;
  cpkt_sqlite *unlock_blocked;
  cpkt_sqlite *deferred_close_database;
  cpkt_sqlite *backup_source;
  cpkt_sqlite *backup_destination;
  cpkt_sqlite *strict_close_database;
  cpkt_sqlite_fts5_api *fts_api;
  cpkt_sqlite_filename *filename;
  cpkt_sqlite_table *table;
  cpkt_sqlite_statement *statement;
  cpkt_sqlite_statement *other_statement;
  cpkt_sqlite_i64 expected;
  cpkt_sqlite_i64 actual;
  cpkt_sqlite_blob *blob;
  cpkt_sqlite_backup *deferred_backup;
  cpkt_sqlite_session *session;
  cpkt_sqlite_changeset *changeset;
  cpkt_sqlite_changeset *inverted;
  cpkt_sqlite_changeset *combined;
  cpkt_sqlite_changeset *grouped;
  cpkt_sqlite_changeset *rebase;
  cpkt_sqlite_changeset_iterator *iterator;
  cpkt_sqlite_changegroup *changegroup;
  cpkt_sqlite_string *string;
  const char *table_name;
  const char *filename_parameters[2];
  const unsigned char *primary_key;
  int column_count;
  int operation;
  int indirect;
  int increment;
  int size_config;
  int primary_key_count;
  int status;
  int carray_values[2];
  cpkt_sqlite_i64 carray_i64_values[2];
  unsigned short utf16_text[3];
  unsigned short utf16_filename[9];
  unsigned short utf16_sql[9];
  unsigned short utf16_collation_name[11];
  unsigned short utf16_function_name[15];
  unsigned short utf16_error_text[11];
  unsigned short utf16le_result[3];
  unsigned char utf16be_result[6];
  char blob_buffer[3];
  char format_buffer[16];
  char wide_format_buffer[32];
  char *formatted;
  int count;
  int configuration_result;
  int was_bound;
  int auxdata_count;
  int aggregate_count;
  cpkt_sqlite_memory_methods memory_methods;
  cpkt_sqlite_memory_methods optional_shutdown_methods;
  cpkt_sqlite_memory_methods configured_memory_methods;
  void *allocator_probe;
  cpkt_sqlite_column_metadata column_metadata;
  collation_needed_state collation_state;
  autovacuum_state autovacuum;
  trace_state trace;
  trace_state profile_trace;
  rtree_state rtree;
  unlock_notify_state unlock_notify;
  preupdate_blob_state preupdate_blob;
  virtual_table_state virtual_table;
  cpkt_sqlite_module_methods virtual_methods;
  cpkt_sqlite_page_cache_methods page_cache_methods;
  cpkt_sqlite_page_cache_methods configured_page_cache_methods;
  cpkt_sqlite_mutex *mutex;
  page_cache_test_state page_cache_state;
  stream_state stream;
  stream_state left_stream;
  stream_state right_stream;
  stream_state result_stream;
  fts5_state fts_state;
  vfs_probe_state vfs_probe;
  cpkt_sqlite_vfs_methods vfs_methods;
  cpkt_sqlite_vfs *vfs;
  cpkt_sqlite_auto_extension *auto_extension;
  auto_extension_state auto_extension_registration;
  log_state log;

  if (cpkt_sqlite_shutdown() != CPKT_SQLITE_OK ||
      cpkt_sqlite_global_config_memory_methods_get(&memory_methods) !=
          CPKT_SQLITE_OK ||
      memory_methods.allocate == 0 || memory_methods.free == 0 ||
      memory_methods.reallocate == 0 || memory_methods.size == 0 ||
      memory_methods.roundup == 0 || memory_methods.initialize == 0 ||
      memory_methods.shutdown == 0)
    return 36;
  optional_shutdown_methods = memory_methods;
  optional_shutdown_methods.shutdown = NULL;
  if (cpkt_sqlite_global_config_memory_methods_set(NULL) != CPKT_SQLITE_MISUSE)
    return 90;
  optional_shutdown_methods.initialize = NULL;
  if (cpkt_sqlite_global_config_memory_methods_set(
          &optional_shutdown_methods) != CPKT_SQLITE_MISUSE)
    return 91;
  optional_shutdown_methods.initialize = memory_methods.initialize;
  if (cpkt_sqlite_global_config_memory_methods_set(
          &optional_shutdown_methods) != CPKT_SQLITE_OK ||
      cpkt_sqlite_global_config_memory_methods_get(
          &configured_memory_methods) != CPKT_SQLITE_OK ||
      configured_memory_methods.shutdown != NULL ||
      cpkt_sqlite_initialize() != CPKT_SQLITE_OK)
    return 92;
  allocator_probe = cpkt_sqlite_allocate(32);
  if (allocator_probe == NULL)
    return 93;
  cpkt_sqlite_free(allocator_probe);
  if (cpkt_sqlite_shutdown() != CPKT_SQLITE_OK ||
      cpkt_sqlite_global_config_memory_methods_set(&memory_methods) !=
          CPKT_SQLITE_OK)
    return 94;
  if (cpkt_sqlite_memory_alarm(0, 0, cpkt_sqlite_i64_make(0, 0)) !=
      CPKT_SQLITE_OK)
    return 81;
  log.call_count = 0;
  log.error_code = 0;
  log.message[0] = '\0';
  if (cpkt_sqlite_global_config_log(record_log, &log) != CPKT_SQLITE_OK)
    return 82;
  cpkt_sqlite_log(CPKT_SQLITE_NOTICE, "facade log %d", 7);
  if (log.call_count != 1 || log.error_code != CPKT_SQLITE_NOTICE ||
      strcmp(log.message, "facade log 7") != 0)
    return 83;
  cpkt_sqlite_log_i64(CPKT_SQLITE_NOTICE, "signed %lld",
                      cpkt_sqlite_i64_make(0xffffffffUL, 0xffffffd6UL));
  if (log.call_count != 2 || strcmp(log.message, "signed -42") != 0)
    return 88;
  cpkt_sqlite_log_u64(CPKT_SQLITE_NOTICE, "unsigned %llu",
                      cpkt_sqlite_u64_make(0xffffffffUL, 0xffffffffUL));
  if (log.call_count != 3 ||
      strcmp(log.message, "unsigned 18446744073709551615") != 0)
    return 89;
  page_cache_state.initialize_count = 0;
  page_cache_state.shutdown_count = 0;
  page_cache_state.create_count = 0;
  page_cache_state.fetch_count = 0;
  page_cache_state.unpin_count = 0;
  page_cache_state.destroy_count = 0;
  page_cache_methods.context = &page_cache_state;
  page_cache_methods.initialize = page_cache_test_initialize;
  page_cache_methods.shutdown = page_cache_test_shutdown;
  page_cache_methods.create = page_cache_test_create;
  page_cache_methods.cache_size = page_cache_test_cache_size;
  page_cache_methods.page_count = page_cache_test_page_count;
  page_cache_methods.fetch = page_cache_test_fetch;
  page_cache_methods.unpin = page_cache_test_unpin;
  page_cache_methods.rekey = page_cache_test_rekey;
  page_cache_methods.truncate = page_cache_test_truncate;
  page_cache_methods.destroy = page_cache_test_destroy;
  page_cache_methods.shrink = page_cache_test_shrink;
  if (cpkt_sqlite_global_config_page_cache_methods_set(&page_cache_methods) !=
          CPKT_SQLITE_OK ||
      cpkt_sqlite_global_config_page_cache_methods_get(
          &configured_page_cache_methods) != CPKT_SQLITE_OK ||
      configured_page_cache_methods.context != &page_cache_state ||
      configured_page_cache_methods.create != page_cache_test_create)
    return 39;
  (void)remove("cpkt-sqlite-page-cache-test.db");
  page_cache_database = cpkt_sqlite_new("cpkt-sqlite-page-cache-test.db");
  if (page_cache_database == 0 ||
      page_cache_database->tx(
          page_cache_database,
          "create table cache_test (id integer primary key, value text);"
          "insert into cache_test values(1, 'page cache');",
          0, 0) != CPKT_SQLITE_OK) {
    if (page_cache_database != 0)
      page_cache_database->close(page_cache_database);
    return 40;
  }
  page_cache_database->close(page_cache_database);
  (void)remove("cpkt-sqlite-page-cache-test.db");
  if (page_cache_state.initialize_count != 1 ||
      page_cache_state.create_count == 0 || page_cache_state.fetch_count == 0 ||
      page_cache_state.unpin_count == 0 || page_cache_state.destroy_count == 0)
    return 41;
  mutex = cpkt_sqlite_mutex_new(CPKT_SQLITE_MUTEX_FAST);
  if (mutex == 0 || mutex->try_enter(mutex) != CPKT_SQLITE_OK ||
      mutex->held(mutex) == 0)
    return 37;
  mutex->leave(mutex);
  if (mutex->not_held(mutex) == 0)
    return 38;
  mutex->close(mutex);
  formatted = cpkt_sqlite_format("%q:%d", "a'b", 7);
  if (formatted == 0 || strcmp(formatted, "a''b:7") != 0 ||
      cpkt_sqlite_format_into((int)sizeof(format_buffer), format_buffer, "%s",
                              "format") != format_buffer ||
      strcmp(format_buffer, "format") != 0)
    return 45;
  cpkt_sqlite_free(formatted);
  formatted = cpkt_sqlite_format_i64("%lld%%", cpkt_sqlite_i64_make(0, 42));
  if (formatted == 0 || strcmp(formatted, "42%") != 0)
    return 90;
  cpkt_sqlite_free(formatted);
  formatted = cpkt_sqlite_format_u64(
      "%llu", cpkt_sqlite_u64_make(0xffffffffUL, 0xffffffffUL));
  if (formatted == 0 || strcmp(formatted, "18446744073709551615") != 0)
    return 91;
  cpkt_sqlite_free(formatted);
  if (cpkt_sqlite_format_into_i64(
          (int)sizeof(format_buffer), format_buffer, "%lld",
          cpkt_sqlite_i64_make(0xffffffffUL, 0xffffffd6UL)) != format_buffer ||
      strcmp(format_buffer, "-42") != 0 ||
      cpkt_sqlite_format_into_u64(
          (int)sizeof(wide_format_buffer), wide_format_buffer, "%016llx",
          cpkt_sqlite_u64_make(0xffffffffUL, 0xffffffffUL)) !=
          wide_format_buffer ||
      strcmp(wide_format_buffer, "ffffffffffffffff") != 0)
    return 92;
  vfs_probe.open_count = 0;
  vfs_probe.close_count = 0;
  memset(&vfs_methods, 0, sizeof(vfs_methods));
  vfs_methods.version = 1;
  vfs_methods.open = vfs_probe_open;
  vfs_methods.delete_file = vfs_probe_delete;
  vfs_methods.access = vfs_probe_access;
  vfs_methods.full_path = vfs_probe_full_path;
  vfs_methods.randomness = vfs_probe_randomness;
  vfs_methods.sleep = vfs_probe_sleep;
  vfs_methods.current_time = vfs_probe_current_time;
  vfs = cpkt_sqlite_vfs_new("cpkt-vfs-probe", 1024, &vfs_probe, &vfs_methods);
  if (vfs == 0 || vfs->register_vfs(vfs, 0) != CPKT_SQLITE_OK)
    return 74;
  if (cpkt_sqlite_vfs_find("cpkt-vfs-probe") != vfs)
    return 85;
  replica = cpkt_sqlite_open(
      "cpkt-vfs-probe.db", CPKT_SQLITE_OPEN_READWRITE | CPKT_SQLITE_OPEN_CREATE,
      "cpkt-vfs-probe");
  if (replica == 0 || replica->error_code(replica) != CPKT_SQLITE_CANTOPEN ||
      vfs_probe.open_count != 1 || vfs_probe.close_count != 1)
    return 75;
  replica->close(replica);
  if (vfs->unregister_vfs(vfs) != CPKT_SQLITE_OK)
    return 76;
  vfs->close(vfs);
  auto_extension_registration.call_count = 0;
  auto_extension_registration.increment = 1;
  auto_extension = cpkt_sqlite_auto_extension_new(auto_extension_initialize,
                                                  &auto_extension_registration);
  if (auto_extension == 0 ||
      auto_extension->register_extension(auto_extension) != CPKT_SQLITE_OK)
    return 79;
  db = cpkt_sqlite_open(":memory:",
                        CPKT_SQLITE_OPEN_READWRITE | CPKT_SQLITE_OPEN_CREATE |
                            CPKT_SQLITE_OPEN_FULLMUTEX,
                        0);
  if (db == 0 || db->error_code(db) != CPKT_SQLITE_OK ||
      cpkt_sqlite_error16(db) == 0)
    return 1;
  string = cpkt_sqlite_string_new(db);
  if (string == 0)
    return 86;
  string->append_format(string, "%s:%d", "string", 7);
  if (string->value(string) == 0 ||
      strcmp(string->value(string), "string:7") != 0 ||
      string->error_code(string) != CPKT_SQLITE_OK)
    return 87;
  cpkt_sqlite_string_append_format_i64(
      string, ":%lld", cpkt_sqlite_i64_make(0xffffffffUL, 0xffffffd6UL));
  cpkt_sqlite_string_append_format_u64(
      string, ":%llx", cpkt_sqlite_u64_make(0xffffffffUL, 0xffffffffUL));
  if (strcmp(string->value(string), "string:7:-42:ffffffffffffffff") != 0)
    return 93;
  string->close(string);
  statement = 0;
  if (auto_extension_registration.call_count != 1 ||
      db->prepare(db, "select auto_plus_one(4)", -1, 0, &statement, 0) !=
          CPKT_SQLITE_OK ||
      statement == 0 || statement->step(statement) != CPKT_SQLITE_ROW ||
      statement->column_int(statement, 0) != 5 ||
      statement->finalize(statement) != CPKT_SQLITE_OK ||
      auto_extension->cancel(auto_extension) != 1)
    return 80;
  auto_extension->close(auto_extension);
  mutex = cpkt_sqlite_database_mutex(db);
  if (mutex == 0 || mutex->try_enter(mutex) != CPKT_SQLITE_OK ||
      mutex->held(mutex) == 0)
    return 63;
  mutex->leave(mutex);
  mutex->close(mutex);
  utf16_collation_name[0] = 'u';
  utf16_collation_name[1] = 't';
  utf16_collation_name[2] = 'f';
  utf16_collation_name[3] = '1';
  utf16_collation_name[4] = '6';
  utf16_collation_name[5] = '_';
  utf16_collation_name[6] = 'c';
  utf16_collation_name[7] = 'm';
  utf16_collation_name[8] = 'p';
  utf16_collation_name[9] = 0;
  if (cpkt_sqlite_create_collation16(
          db, utf16_collation_name, CPKT_SQLITE_UTF16, 0,
          needed_collation_compare) != CPKT_SQLITE_OK ||
      db->tx(db, "select 'a' union all select 'b' order by 1 collate utf16_cmp",
             0, 0) != CPKT_SQLITE_OK)
    return 71;
  utf16_function_name[0] = 'u';
  utf16_function_name[1] = 't';
  utf16_function_name[2] = 'f';
  utf16_function_name[3] = '1';
  utf16_function_name[4] = '6';
  utf16_function_name[5] = '_';
  utf16_function_name[6] = 'f';
  utf16_function_name[7] = 'u';
  utf16_function_name[8] = 'n';
  utf16_function_name[9] = 'c';
  utf16_function_name[10] = 't';
  utf16_function_name[11] = 'i';
  utf16_function_name[12] = 'o';
  utf16_function_name[13] = 'n';
  utf16_function_name[14] = 0;
  increment = 1;
  statement = 0;
  if (cpkt_sqlite_create_function16(db, utf16_function_name, 1,
                                    CPKT_SQLITE_UTF16, &increment, plus_one, 0,
                                    0) != CPKT_SQLITE_OK ||
      db->prepare(db, "select utf16_function(4)", -1, 0, &statement, 0) !=
          CPKT_SQLITE_OK ||
      statement == 0 || statement->step(statement) != CPKT_SQLITE_ROW ||
      statement->column_int(statement, 0) != 5 ||
      statement->finalize(statement) != CPKT_SQLITE_OK)
    return 78;
  utf16_error_text[0] = 'w';
  utf16_error_text[1] = 'i';
  utf16_error_text[2] = 'd';
  utf16_error_text[3] = 'e';
  utf16_error_text[4] = ' ';
  utf16_error_text[5] = 'e';
  utf16_error_text[6] = 'r';
  utf16_error_text[7] = 'r';
  utf16_error_text[8] = 'o';
  utf16_error_text[9] = 'r';
  utf16_error_text[10] = 0;
  statement = 0;
  if (cpkt_sqlite_create_function(db, "utf16_error", 0, CPKT_SQLITE_UTF8,
                                  utf16_error_text, utf16_error, 0, 0,
                                  0) != CPKT_SQLITE_OK ||
      db->prepare(db, "select utf16_error()", -1, 0, &statement, 0) !=
          CPKT_SQLITE_OK ||
      statement == 0 || statement->step(statement) != CPKT_SQLITE_ERROR ||
      strcmp(db->error(db), "wide error") != 0 ||
      statement->finalize(statement) != CPKT_SQLITE_ERROR)
    return 76;
  utf16le_result[0] = 'l';
  utf16le_result[1] = 'e';
  utf16le_result[2] = 0;
  utf16be_result[0] = 0;
  utf16be_result[1] = 'b';
  utf16be_result[2] = 0;
  utf16be_result[3] = 'e';
  utf16be_result[4] = 0;
  utf16be_result[5] = 0;
  statement = 0;
  if (cpkt_sqlite_create_function(db, "utf16le_text", 0, CPKT_SQLITE_UTF8,
                                  utf16le_result, utf16le_text, 0, 0,
                                  0) != CPKT_SQLITE_OK ||
      cpkt_sqlite_create_function(db, "utf16be_text", 0, CPKT_SQLITE_UTF8,
                                  utf16be_result, utf16be_text, 0, 0,
                                  0) != CPKT_SQLITE_OK ||
      db->prepare(db, "select utf16le_text(), utf16be_text()", -1, 0,
                  &statement, 0) != CPKT_SQLITE_OK ||
      statement == 0 || statement->step(statement) != CPKT_SQLITE_ROW ||
      strcmp((const char *)statement->column_text(statement, 0), "le") != 0 ||
      strcmp((const char *)statement->column_text(statement, 1), "be") != 0 ||
      statement->finalize(statement) != CPKT_SQLITE_OK)
    return 77;
  if (db->set_error(db, CPKT_SQLITE_ERROR, "facade error") != CPKT_SQLITE_OK ||
      db->error_code(db) != CPKT_SQLITE_ERROR ||
      strcmp(db->error(db), "facade error") != 0)
    return 72;
  was_bound = 0;
  statement = 0;
  if (cpkt_sqlite_create_function(db, "bound_argument", 1, CPKT_SQLITE_UTF8,
                                  &was_bound, bound_argument, 0, 0,
                                  0) != CPKT_SQLITE_OK ||
      db->prepare(db, "select bound_argument(?1)", -1, 0, &statement, 0) !=
          CPKT_SQLITE_OK ||
      statement == 0 ||
      statement->bind_int(statement, 1, 3) != CPKT_SQLITE_OK ||
      statement->step(statement) != CPKT_SQLITE_ROW ||
      statement->column_int(statement, 0) != 1 || was_bound != 1 ||
      statement->finalize(statement) != CPKT_SQLITE_OK)
    return 75;
  auxdata_count = 0;
  statement = 0;
  if (cpkt_sqlite_create_function(db, "auxiliary_data", 1, CPKT_SQLITE_UTF8,
                                  &auxdata_count, auxiliary_data, 0, 0,
                                  0) != CPKT_SQLITE_OK ||
      db->prepare(
          db, "select auxiliary_data(?1) from (select 1 union all select 2)",
          -1, 0, &statement, 0) != CPKT_SQLITE_OK ||
      statement == 0 ||
      statement->bind_int(statement, 1, 9) != CPKT_SQLITE_OK ||
      statement->step(statement) != CPKT_SQLITE_ROW ||
      statement->column_int(statement, 0) != 1 ||
      statement->step(statement) != CPKT_SQLITE_ROW ||
      statement->column_int(statement, 0) != 2 || auxdata_count != 2 ||
      statement->finalize(statement) != CPKT_SQLITE_OK)
    return 78;
  aggregate_count = 0;
  statement = 0;
  if (cpkt_sqlite_create_function(db, "aggregate_counter", 1, CPKT_SQLITE_UTF8,
                                  &aggregate_count, 0, aggregate_counter_step,
                                  aggregate_counter_final,
                                  0) != CPKT_SQLITE_OK ||
      db->prepare(db,
                  "select aggregate_counter(value) from (select 1 as value "
                  "union all select 2)",
                  -1, 0, &statement, 0) != CPKT_SQLITE_OK ||
      statement == 0 || statement->step(statement) != CPKT_SQLITE_ROW ||
      statement->column_int(statement, 0) != 2 || aggregate_count != 2 ||
      statement->finalize(statement) != CPKT_SQLITE_OK)
    return 79;
  deferred_close_database = cpkt_sqlite_new(":memory:");
  increment = 41;
  statement = 0;
  if (deferred_close_database == 0 ||
      cpkt_sqlite_create_function(deferred_close_database, "plus_one", 1,
                                  CPKT_SQLITE_UTF8, &increment, plus_one, 0, 0,
                                  0) != CPKT_SQLITE_OK ||
      deferred_close_database->prepare(deferred_close_database,
                                       "select plus_one(1)", -1, 0, &statement,
                                       0) != CPKT_SQLITE_OK ||
      statement == 0 ||
      statement->database(statement) != deferred_close_database) {
    return 64;
  }
  deferred_close_database->close(deferred_close_database);
  if (statement->database(statement) != deferred_close_database ||
      statement->step(statement) != CPKT_SQLITE_ROW ||
      statement->column_int(statement, 0) != 42 ||
      statement->finalize(statement) != CPKT_SQLITE_OK)
    return 65;
  deferred_close_database = cpkt_sqlite_new(":memory:");
  blob = 0;
  if (deferred_close_database == 0 ||
      deferred_close_database->tx(
          deferred_close_database,
          "create table deferred_blob(value blob);"
          "insert into deferred_blob values(x'78797a');",
          0, 0) != CPKT_SQLITE_OK ||
      cpkt_sqlite_open_blob(deferred_close_database, "main", "deferred_blob",
                            "value", cpkt_sqlite_i64_make(0, 1), 0,
                            &blob) != CPKT_SQLITE_OK ||
      blob == 0 || blob->database != deferred_close_database)
    return 66;
  deferred_close_database->close(deferred_close_database);
  if (blob->read(blob, blob_buffer, 3, 0) != CPKT_SQLITE_OK ||
      memcmp(blob_buffer, "xyz", 3) != 0 ||
      blob->close(blob) != CPKT_SQLITE_OK) {
    return 67;
  }
  backup_source = cpkt_sqlite_new(":memory:");
  backup_destination = cpkt_sqlite_new(":memory:");
  deferred_backup = 0;
  if (backup_source == 0 || backup_destination == 0 ||
      backup_source->tx(backup_source,
                        "create table deferred_backup(value integer);"
                        "insert into deferred_backup values(7);",
                        0, 0) != CPKT_SQLITE_OK ||
      cpkt_sqlite_backup_start(backup_destination, "main", backup_source,
                               "main", &deferred_backup) != CPKT_SQLITE_OK ||
      deferred_backup == 0 ||
      deferred_backup->destination != backup_destination ||
      deferred_backup->source != backup_source)
    return 68;
  backup_source->close(backup_source);
  backup_destination->close(backup_destination);
  if (deferred_backup->close(deferred_backup) != CPKT_SQLITE_OK)
    return 69;
  strict_close_database = cpkt_sqlite_new(":memory:");
  statement = 0;
  if (strict_close_database == 0 ||
      strict_close_database->prepare(strict_close_database, "select 8", -1, 0,
                                     &statement, 0) != CPKT_SQLITE_OK ||
      statement == 0 ||
      cpkt_sqlite_close_strict(strict_close_database) != CPKT_SQLITE_BUSY ||
      statement->step(statement) != CPKT_SQLITE_ROW ||
      statement->column_int(statement, 0) != 8 ||
      statement->finalize(statement) != CPKT_SQLITE_OK ||
      cpkt_sqlite_close_strict(strict_close_database) != CPKT_SQLITE_OK)
    return 74;
  statement = 0;
  other_statement = 0;
  if (db->prepare(db, "select 6", -1, 0, &statement, 0) != CPKT_SQLITE_OK ||
      statement == 0 ||
      cpkt_sqlite_next_statement(db, 0, &other_statement) != CPKT_SQLITE_OK ||
      other_statement == 0 || other_statement == statement ||
      other_statement->database(other_statement) != db ||
      other_statement->finalize(other_statement) != CPKT_SQLITE_OK ||
      statement->step(statement) != CPKT_SQLITE_ROW ||
      statement->column_int(statement, 0) != 6 ||
      statement->finalize(statement) != CPKT_SQLITE_OK)
    return 70;
  configuration_result = 0;
  if (cpkt_sqlite_global_config_int_out(
          CPKT_SQLITE_GLOBAL_CONFIG_PCACHE_HEADER_SIZE,
          &configuration_result) != CPKT_SQLITE_OK ||
      configuration_result <= 0)
    return 35;
  configuration_result = 0;
  if (cpkt_sqlite_database_config_int(
          db, CPKT_SQLITE_DATABASE_CONFIG_ENABLE_FOREIGN_KEYS, 1,
          &configuration_result) != CPKT_SQLITE_OK ||
      configuration_result != 1)
    return 34;
  if (cpkt_sqlite_set_lock_timeout(db, 10, 0) != CPKT_SQLITE_OK)
    return 42;
  collation_state.count = 0;
  collation_state.status = CPKT_SQLITE_OK;
  if (cpkt_sqlite_set_collation_needed16(db, 0, 0) != CPKT_SQLITE_OK ||
      cpkt_sqlite_set_collation_needed(db, collation_needed,
                                       &collation_state) != CPKT_SQLITE_OK ||
      db->tx(db,
             "select 'b' as value union all select 'a' order by value collate "
             "cpkt_needed",
             0, 0) != CPKT_SQLITE_OK ||
      collation_state.count != 1 || collation_state.status != CPKT_SQLITE_OK)
    return 46;
  autovacuum.callback_count = 0;
  autovacuum.destroy_count = 0;
  if (cpkt_sqlite_set_autovacuum_callback(db, autovacuum_callback, &autovacuum,
                                          autovacuum_destroy) !=
          CPKT_SQLITE_OK ||
      cpkt_sqlite_set_autovacuum_callback(db, 0, 0, 0) != CPKT_SQLITE_OK ||
      autovacuum.destroy_count != 1)
    return 47;
  autovacuum.destroy_count = 0;
  if (cpkt_sqlite_set_client_data(db, "cpkt.client", &autovacuum,
                                  autovacuum_destroy) != CPKT_SQLITE_OK ||
      cpkt_sqlite_client_data(db, "cpkt.client") != &autovacuum ||
      cpkt_sqlite_set_client_data(db, "cpkt.client", 0, 0) != CPKT_SQLITE_OK ||
      autovacuum.destroy_count != 1)
    return 54;
  trace.statement_count = 0;
  trace.row_count = 0;
  trace.profile_count = 0;
  if (cpkt_sqlite_set_trace(db,
                            CPKT_SQLITE_TRACE_STATEMENT |
                                CPKT_SQLITE_TRACE_ROW |
                                CPKT_SQLITE_TRACE_PROFILE,
                            trace_callback, &trace) != CPKT_SQLITE_OK ||
      db->tx(db, "select 'trace'", 0, 0) != CPKT_SQLITE_OK ||
      trace.statement_count == 0 || trace.row_count == 0 ||
      trace.profile_count == 0 ||
      cpkt_sqlite_set_trace(db, 0, 0, 0) != CPKT_SQLITE_OK)
    return 48;
  trace.statement_count = 0;
  memset(&profile_trace, 0, sizeof(profile_trace));
  if (cpkt_sqlite_set_trace(db, CPKT_SQLITE_TRACE_STATEMENT, trace_callback,
                            &trace) != CPKT_SQLITE_OK ||
      cpkt_sqlite_set_legacy_profile(db, legacy_profile_callback,
                                     &profile_trace) != 0 ||
      db->tx(db, "select 'trace with profile'", 0, 0) != CPKT_SQLITE_OK ||
      trace.statement_count == 0 || profile_trace.profile_count == 0 ||
      cpkt_sqlite_set_legacy_profile(db, 0, 0) != &profile_trace ||
      db->tx(db, "select 'trace after profile'", 0, 0) != CPKT_SQLITE_OK ||
      trace.statement_count < 2 ||
      cpkt_sqlite_set_trace(db, 0, 0, 0) != CPKT_SQLITE_OK)
    return 85;
  trace.statement_count = 0;
  trace.profile_count = 0;
  if (cpkt_sqlite_set_legacy_trace(db, legacy_trace_callback, &trace) != 0 ||
      db->tx(db, "select 'legacy trace'", 0, 0) != CPKT_SQLITE_OK ||
      trace.statement_count == 0 ||
      cpkt_sqlite_set_legacy_profile(db, legacy_profile_callback, &trace) !=
          0 ||
      db->tx(db, "select 'legacy profile'", 0, 0) != CPKT_SQLITE_OK ||
      trace.profile_count == 0 ||
      cpkt_sqlite_set_legacy_profile(db, 0, 0) != &trace) {
    return 84;
  }
  memset(&virtual_table, 0, sizeof(virtual_table));
  virtual_table_shadow_name_count = 0;
  memset(&virtual_methods, 0, sizeof(virtual_methods));
  virtual_methods.context = &virtual_table;
  virtual_methods.destroy = virtual_table_module_destroy;
  virtual_methods.create = virtual_table_connect;
  virtual_methods.connect = virtual_table_connect;
  virtual_methods.best_index = virtual_table_best_index;
  virtual_methods.disconnect = virtual_table_disconnect;
  virtual_methods.destroy_table = virtual_table_disconnect;
  virtual_methods.open = virtual_table_open;
  virtual_methods.close = virtual_table_close;
  virtual_methods.filter = virtual_table_filter;
  virtual_methods.next = virtual_table_next;
  virtual_methods.eof = virtual_table_eof;
  virtual_methods.column = virtual_table_column;
  virtual_methods.rowid = virtual_table_rowid;
  virtual_methods.update = virtual_table_update;
  virtual_methods.find_function = virtual_table_find_function;
  virtual_methods.shadow_name = virtual_table_shadow_name;
  if (cpkt_sqlite_create_module(db, "cpkt_test_vtab", &virtual_methods) !=
          CPKT_SQLITE_OK ||
      db->prepare(db, "select value from cpkt_test_vtab", -1, 0, &statement,
                  0) != CPKT_SQLITE_OK ||
      statement == 0 || statement->step(statement) != CPKT_SQLITE_ROW ||
      statement->column_int(statement, 0) != 42 ||
      statement->finalize(statement) != CPKT_SQLITE_OK ||
      db->tx(db,
             "create table cpkt_virtual_shadow(value integer);"
             "create virtual table cpkt_virtual using cpkt_test_vtab;",
             0, 0) != CPKT_SQLITE_OK ||
      db->prepare(db, "select value from cpkt_virtual", -1, 0, &statement, 0) !=
          CPKT_SQLITE_OK ||
      statement == 0 || statement->step(statement) != CPKT_SQLITE_ROW ||
      statement->column_int(statement, 0) != 42 ||
      statement->step(statement) != CPKT_SQLITE_DONE ||
      statement->finalize(statement) != CPKT_SQLITE_OK ||
      db->tx(db, "insert into cpkt_virtual values (9);", 0, 0) !=
          CPKT_SQLITE_OK ||
      db->overload_function(db, "cpkt_vtab_plus_one", 1) != CPKT_SQLITE_OK ||
      db->prepare(db, "select cpkt_vtab_plus_one(value) from cpkt_virtual", -1,
                  0, &statement, 0) != CPKT_SQLITE_OK ||
      statement == 0 || statement->step(statement) != CPKT_SQLITE_ROW ||
      statement->column_int(statement, 0) != 43 ||
      statement->finalize(statement) != CPKT_SQLITE_OK ||
      db->tx(db, "drop table cpkt_virtual;", 0, 0) != CPKT_SQLITE_OK ||
      virtual_table.best_index_count == 0 || virtual_table.filter_count == 0 ||
      virtual_table.column_count == 0 || virtual_table.function_count == 0 ||
      virtual_table.update_count != 1 || virtual_table.disconnect_count != 1 ||
      virtual_table_shadow_name_count == 0)
    return 61;
  rtree.callback_count = 0;
  rtree.query_count = 0;
  if (cpkt_sqlite_register_rtree_geometry(db, "cpkt_contains_point",
                                          rtree_contains_point,
                                          &rtree) != CPKT_SQLITE_OK ||
      db->tx(
          db,
          "create virtual table boxes using rtree(id, min_x, max_x, min_y, "
          "max_y);"
          "insert into boxes values(1, 0, 10, 0, 10);"
          "select id from boxes where min_x match cpkt_contains_point(5, 5);",
          0, 0) != CPKT_SQLITE_OK ||
      rtree.callback_count == 0)
    return 49;
  if (cpkt_sqlite_register_rtree_query(db, "cpkt_score_point",
                                       rtree_score_point, &rtree,
                                       0) != CPKT_SQLITE_OK ||
      db->tx(db,
             "select id from boxes where min_x match cpkt_score_point(5, 5);",
             0, 0) != CPKT_SQLITE_OK ||
      rtree.query_count == 0)
    return 50;
  filename_parameters[0] = "cache";
  filename_parameters[1] = "shared";
  filename = cpkt_sqlite_filename_new("main.db", "main.db-journal",
                                      "main.db-wal", 1, filename_parameters);
  if (filename == 0 || filename->database(filename) == 0 ||
      filename->journal(filename) == 0 || filename->wal(filename) == 0 ||
      filename->uri_parameter(filename, "cache") == 0 ||
      strcmp(filename->database(filename), "main.db") != 0 ||
      strcmp(filename->uri_parameter(filename, "cache"), "shared") != 0)
    return 2;
  filename->close(filename);
  fts_state.create_count = 0;
  fts_state.tokenize_count = 0;
  fts_state.destroy_count = 0;
  fts_state.binding_destroy_count = 0;
  fts_state.auxiliary_count = 0;
  fts_state.phrase_callback_count = 0;
  fts_database = cpkt_sqlite_new(":memory:");
  fts_api = 0;
  if (fts_database == 0 ||
      cpkt_sqlite_fts5_api_open(fts_database, &fts_api) != CPKT_SQLITE_OK ||
      fts_api == 0 || fts_api->version(fts_api) < 3 ||
      fts_api->create_tokenizer(fts_api, "cpkt_test", &fts_state,
                                fts5_tokenizer_create, fts5_tokenizer_destroy,
                                fts5_tokenizer_tokenize,
                                fts5_binding_destroy) != CPKT_SQLITE_OK ||
      fts_api->create_auxiliary(fts_api, "cpkt_fts_probe", &fts_state,
                                fts5_auxiliary, 0) != CPKT_SQLITE_OK ||
      fts_database->tx(
          fts_database,
          "create virtual table fts using fts5(content, tokenize='cpkt_test');"
          "insert into fts values('alpha');",
          0, 0) != CPKT_SQLITE_OK ||
      fts_database->prepare(fts_database,
                            "select count(*) from fts where fts match 'alpha'",
                            -1, 0, &statement, 0) != CPKT_SQLITE_OK ||
      statement == 0 || statement->step(statement) != CPKT_SQLITE_ROW ||
      statement->column_int(statement, 0) != 1 ||
      statement->finalize(statement) != CPKT_SQLITE_OK ||
      fts_database->prepare(
          fts_database,
          "select cpkt_fts_probe(fts) from fts where fts match 'alpha'", -1, 0,
          &statement, 0) != CPKT_SQLITE_OK ||
      statement == 0 || statement->step(statement) != CPKT_SQLITE_ROW ||
      statement->column_int(statement, 0) != 1 ||
      statement->finalize(statement) != CPKT_SQLITE_OK ||
      fts_state.auxiliary_count != 1 || fts_state.create_count == 0 ||
      fts_state.tokenize_count == 0)
    return 32;
  fts_api->close(fts_api);
  fts_database->close(fts_database);
  if (fts_state.destroy_count == 0 || fts_state.binding_destroy_count != 1)
    return 33;
  utf16_filename[0] = ':';
  utf16_filename[1] = 'm';
  utf16_filename[2] = 'e';
  utf16_filename[3] = 'm';
  utf16_filename[4] = 'o';
  utf16_filename[5] = 'r';
  utf16_filename[6] = 'y';
  utf16_filename[7] = ':';
  utf16_filename[8] = 0;
  utf16_database = cpkt_sqlite_open16(utf16_filename);
  if (utf16_database == 0 ||
      utf16_database->tx(utf16_database,
                         "create table utf16_open_test (value integer);", 0,
                         0) != CPKT_SQLITE_OK) {
    if (utf16_database != 0)
      utf16_database->close(utf16_database);
    return 57;
  }
  utf16_sql[0] = 's';
  utf16_sql[1] = 'e';
  utf16_sql[2] = 'l';
  utf16_sql[3] = 'e';
  utf16_sql[4] = 'c';
  utf16_sql[5] = 't';
  utf16_sql[6] = ' ';
  utf16_sql[7] = '9';
  utf16_sql[8] = 0;
  statement = 0;
  if (utf16_database->prepare16(utf16_database, utf16_sql, -1, 0, &statement,
                                0) != CPKT_SQLITE_OK ||
      statement == 0 || statement->step(statement) != CPKT_SQLITE_ROW ||
      statement->column_int(statement, 0) != 9 ||
      statement->finalize(statement) != CPKT_SQLITE_OK ||
      cpkt_sqlite_drop_modules(utf16_database, 0, 0) != CPKT_SQLITE_OK)
    return 58;
  utf16_database->close(utf16_database);
  (void)remove("cpkt-sqlite-unlock-notify-test.db");
  if (cpkt_sqlite_enable_shared_cache(1) != CPKT_SQLITE_OK)
    return 55;
  unlock_database =
      cpkt_sqlite_open("cpkt-sqlite-unlock-notify-test.db",
                       CPKT_SQLITE_OPEN_READWRITE | CPKT_SQLITE_OPEN_CREATE |
                           CPKT_SQLITE_OPEN_SHAREDCACHE,
                       0);
  unlock_blocked = cpkt_sqlite_open(
      "cpkt-sqlite-unlock-notify-test.db",
      CPKT_SQLITE_OPEN_READWRITE | CPKT_SQLITE_OPEN_SHAREDCACHE, 0);
  unlock_notify.callback_count = 0;
  unlock_notify.context_count = 0;
  unlock_notify.context_matches = 0;
  if (unlock_database == 0 || unlock_blocked == 0 ||
      unlock_database->tx(
          unlock_database,
          "begin immediate; create table unlock_test (value integer);", 0,
          0) != CPKT_SQLITE_OK ||
      unlock_blocked->tx(unlock_blocked, "begin immediate;", 0, 0) !=
          CPKT_SQLITE_LOCKED ||
      unlock_blocked->unlock_notify(unlock_blocked, unlock_notify_callback,
                                    &unlock_notify) != CPKT_SQLITE_OK ||
      unlock_database->tx(unlock_database, "commit;", 0, 0) != CPKT_SQLITE_OK ||
      unlock_notify.callback_count != 1 || unlock_notify.context_count != 1 ||
      unlock_notify.context_matches == 0)
    return 56;
  unlock_blocked->close(unlock_blocked);
  unlock_database->close(unlock_database);
  (void)cpkt_sqlite_enable_shared_cache(0);
  (void)remove("cpkt-sqlite-unlock-notify-test.db");
  if (db->tx(db, "create table item (name text, value integer, payload blob)",
             0, 0) != CPKT_SQLITE_OK)
    return 3;
  if (db->overload_function(db, "cpkt_overload_probe", 1) != CPKT_SQLITE_OK)
    return 59;
  if (cpkt_sqlite_table_column_metadata(db, "main", "item", "name",
                                        &column_metadata) != CPKT_SQLITE_OK ||
      column_metadata.declared_type == 0 || column_metadata.collation == 0 ||
      strcmp(column_metadata.declared_type, "TEXT") != 0 ||
      strcmp(column_metadata.collation, "BINARY") != 0 ||
      column_metadata.not_null != 0 || column_metadata.primary_key != 0 ||
      column_metadata.auto_increment != 0)
    return 43;
  table = 0;
  if (cpkt_sqlite_get_table(db, "select 'alpha' as name, 1 as value", &table,
                            0) != CPKT_SQLITE_OK ||
      table == 0 || table->row_count(table) != 1 ||
      table->column_count(table) != 2 ||
      strcmp(table->column_name(table, 0), "name") != 0 ||
      strcmp(table->column_name(table, 1), "value") != 0 ||
      strcmp(table->value(table, 0, 0), "alpha") != 0 ||
      strcmp(table->value(table, 0, 1), "1") != 0)
    return 44;
  table->close(table);
  statement = 0;
  if (db->prepare(db, "insert into item values (?1, ?2, ?3)", -1,
                  CPKT_SQLITE_PREPARE_PERSISTENT, &statement,
                  0) != CPKT_SQLITE_OK ||
      statement == 0)
    return 4;
  expected = cpkt_sqlite_i64_make(0x12345678UL, 0x9abcdef0UL);
  if (statement->bind_text(statement, 1, "alpha", -1) != CPKT_SQLITE_OK ||
      statement->bind_i64(statement, 2, expected) != CPKT_SQLITE_OK ||
      statement->bind_blob(statement, 3, "xyz", 3) != CPKT_SQLITE_OK ||
      statement->step(statement) != CPKT_SQLITE_DONE ||
      statement->finalize(statement) != CPKT_SQLITE_OK)
    return 5;
  statement = 0;
  if (db->prepare(db, "select ?1", -1, 0, &statement, 0) != CPKT_SQLITE_OK ||
      statement == 0)
    return 51;
  other_statement = 0;
  if (db->prepare(db, "select ?1", -1, 0, &other_statement, 0) !=
          CPKT_SQLITE_OK ||
      other_statement == 0)
    return 52;
  if (statement->bind_text(statement, 1, "transfer", -1) != CPKT_SQLITE_OK ||
      cpkt_sqlite_statement_transfer_bindings(statement, other_statement) !=
          CPKT_SQLITE_OK ||
      other_statement->step(other_statement) != CPKT_SQLITE_ROW ||
      strcmp((const char *)other_statement->column_text(other_statement, 0),
             "transfer") != 0 ||
      statement->finalize(statement) != CPKT_SQLITE_OK ||
      other_statement->finalize(other_statement) != CPKT_SQLITE_OK)
    return 53;
  if (db->changes(db) != 1)
    return 6;
  statement = 0;
  if (db->prepare(db, "select value, payload from item", -1, 0, &statement,
                  0) != CPKT_SQLITE_OK ||
      statement == 0 || statement->step(statement) != CPKT_SQLITE_ROW)
    return 7;
  actual = statement->column_i64(statement, 0);
  if (actual.high != expected.high || actual.low != expected.low ||
      statement->column_name16(statement, 0) == 0 ||
      statement->column_database_name(statement, 0) == 0 ||
      statement->column_database_name16(statement, 0) == 0 ||
      statement->column_table_name(statement, 0) == 0 ||
      statement->column_table_name16(statement, 0) == 0 ||
      statement->column_origin_name(statement, 0) == 0 ||
      statement->column_origin_name16(statement, 0) == 0 ||
      statement->column_declared_type(statement, 0) == 0 ||
      statement->column_declared_type16(statement, 0) == 0 ||
      strcmp(statement->column_database_name(statement, 0), "main") != 0 ||
      strcmp(statement->column_table_name(statement, 0), "item") != 0 ||
      statement->column_bytes(statement, 1) != 3 ||
      memcmp(statement->column_blob(statement, 1), "xyz", 3) != 0 ||
      statement->expired(statement) != 0 ||
      statement->is_explain(statement) != 0 ||
      statement->status(statement, CPKT_SQLITE_STATEMENT_STATUS_VM_STEP, 0) <=
          0 ||
      statement->scan_status_i64(statement, 0,
                                 CPKT_SQLITE_SCAN_STATUS_LOOP_COUNT, 0,
                                 &actual) != CPKT_SQLITE_OK ||
      cpkt_sqlite_i64_compare(actual, cpkt_sqlite_i64_make(0, 1)) < 0 ||
      statement->finalize(statement) != CPKT_SQLITE_OK)
    return 8;
  count = 0;
  if (db->tx(db, "select name, value from item", callback_count, &count) !=
          CPKT_SQLITE_OK ||
      count != 1)
    return 9;
  carray_values[0] = 7;
  carray_values[1] = 8;
  statement = 0;
  if (db->prepare(db, "select value from carray(?1)", -1, 0, &statement, 0) !=
          CPKT_SQLITE_OK ||
      statement == 0 ||
      statement->bind_carray(statement, 1, carray_values, 2,
                             CPKT_SQLITE_CARRAY_INT32, 0) != CPKT_SQLITE_OK ||
      statement->step(statement) != CPKT_SQLITE_ROW ||
      statement->column_int(statement, 0) != 7 ||
      statement->step(statement) != CPKT_SQLITE_ROW ||
      statement->column_int(statement, 0) != 8 ||
      statement->step(statement) != CPKT_SQLITE_DONE ||
      statement->finalize(statement) != CPKT_SQLITE_OK)
    return 10;
  carray_i64_values[0] = cpkt_sqlite_i64_make(0, 7);
  carray_i64_values[1] = cpkt_sqlite_i64_make(0, 8);
  statement = 0;
  if (db->prepare(db, "select value from carray(?1)", -1, 0, &statement, 0) !=
          CPKT_SQLITE_OK ||
      statement == 0 ||
      statement->bind_carray_with_context(statement, 1, carray_i64_values, 2,
                                          CPKT_SQLITE_CARRAY_I64, 0,
                                          0) != CPKT_SQLITE_OK ||
      statement->step(statement) != CPKT_SQLITE_ROW ||
      cpkt_sqlite_i64_compare(statement->column_i64(statement, 0),
                              cpkt_sqlite_i64_make(0, 7)) != 0 ||
      statement->step(statement) != CPKT_SQLITE_ROW ||
      cpkt_sqlite_i64_compare(statement->column_i64(statement, 0),
                              cpkt_sqlite_i64_make(0, 8)) != 0 ||
      statement->step(statement) != CPKT_SQLITE_DONE ||
      statement->finalize(statement) != CPKT_SQLITE_OK)
    return 110;
  utf16_text[0] = (unsigned short)'o';
  utf16_text[1] = (unsigned short)'k';
  utf16_text[2] = 0;
  statement = 0;
  if (db->prepare(db, "select ?1", -1, 0, &statement, 0) != CPKT_SQLITE_OK ||
      statement == 0 ||
      statement->bind_text16(statement, 1, utf16_text, 4) != CPKT_SQLITE_OK ||
      statement->step(statement) != CPKT_SQLITE_ROW ||
      statement->column_bytes16(statement, 0) != 4 ||
      statement->column_text16(statement, 0) == 0 ||
      statement->finalize(statement) != CPKT_SQLITE_OK)
    return 11;
  blob = 0;
  if (cpkt_sqlite_open_blob(db, "main", "item", "payload",
                            cpkt_sqlite_i64_make(0, 1), 0,
                            &blob) != CPKT_SQLITE_OK ||
      blob == 0 || blob->bytes(blob) != 3 ||
      blob->read(blob, blob_buffer, 3, 0) != CPKT_SQLITE_OK ||
      memcmp(blob_buffer, "xyz", 3) != 0 || blob->close(blob) != CPKT_SQLITE_OK)
    return 12;
  increment = 1;
  if (cpkt_sqlite_create_function(db, "plus_one", 1, CPKT_SQLITE_UTF8,
                                  &increment, plus_one, 0, 0,
                                  0) != CPKT_SQLITE_OK)
    return 13;
  statement = 0;
  if (db->prepare(db, "select plus_one(4)", -1, 0, &statement, 0) !=
          CPKT_SQLITE_OK ||
      statement == 0 || statement->step(statement) != CPKT_SQLITE_ROW ||
      statement->column_int(statement, 0) != 5 ||
      statement->finalize(statement) != CPKT_SQLITE_OK)
    return 14;
  if (db->tx(db,
             "create table session_item (id integer primary key, value text)",
             0, 0) != CPKT_SQLITE_OK)
    return 15;
  session = 0;
  if (cpkt_sqlite_set_preupdate_hook(db, preupdate_noop, 0) != CPKT_SQLITE_OK ||
      cpkt_sqlite_session_new(db, "main", &session) != CPKT_SQLITE_MISUSE ||
      session != 0 ||
      cpkt_sqlite_set_preupdate_hook(db, 0, 0) != CPKT_SQLITE_OK)
    return 16;
  if (cpkt_sqlite_session_new(db, "main", &session) != CPKT_SQLITE_OK ||
      session == 0 ||
      (size_config = 1, session->object_config(session, 1, &size_config)) !=
          CPKT_SQLITE_OK ||
      session->attach(session, "session_item") != CPKT_SQLITE_OK)
    return 17;
  changeset = 0;
  iterator = 0;
  inverted = 0;
  combined = 0;
  changegroup = 0;
  rebase = 0;
  if (session->changeset(session, &changeset) != CPKT_SQLITE_OK ||
      changeset == 0 || changeset->byte_count != 0 || changeset->data != 0 ||
      changeset->iterator(changeset, &iterator) != CPKT_SQLITE_OK ||
      iterator == 0 || iterator->next(iterator) != CPKT_SQLITE_DONE ||
      iterator->close(iterator) != CPKT_SQLITE_OK ||
      cpkt_sqlite_changeset_invert(changeset, &inverted) != CPKT_SQLITE_OK ||
      inverted == 0 || inverted->byte_count != 0 ||
      cpkt_sqlite_changeset_concat(changeset, inverted, &combined) !=
          CPKT_SQLITE_OK ||
      combined == 0 || combined->byte_count != 0 ||
      cpkt_sqlite_changeset_apply(db, changeset, 0, 0, 0) != CPKT_SQLITE_OK ||
      cpkt_sqlite_changeset_apply_ex(db, changeset, 0, 0, 0, 0, &rebase) !=
          CPKT_SQLITE_OK ||
      rebase != 0 ||
      cpkt_sqlite_changeset_apply_v3(db, changeset, 0, 0, 0, 0, &rebase) !=
          CPKT_SQLITE_OK ||
      rebase != 0 ||
      cpkt_sqlite_changegroup_new(&changegroup) != CPKT_SQLITE_OK ||
      changegroup == 0 ||
      changegroup->add(changegroup, changeset) != CPKT_SQLITE_OK)
    return 101;
  changegroup->close(changegroup);
  combined->free(combined);
  inverted->free(inverted);
  changeset->free(changeset);
  if (db->tx(db, "insert into session_item values (1, 'session')", 0, 0) !=
      CPKT_SQLITE_OK)
    return 102;
  changeset = 0;
  if (session->changeset(session, &changeset) != CPKT_SQLITE_OK ||
      changeset == 0 || changeset->byte_count <= 0)
    return 18;
  iterator = 0;
  if (changeset->iterator(changeset, &iterator) != CPKT_SQLITE_OK ||
      iterator == 0 || iterator->next(iterator) != CPKT_SQLITE_ROW)
    return 19;
  table_name = 0;
  column_count = 0;
  operation = 0;
  indirect = 0;
  if (iterator->operation(iterator, &table_name, &column_count, &operation,
                          &indirect) != CPKT_SQLITE_OK ||
      table_name == 0 || strcmp(table_name, "session_item") != 0 ||
      column_count != 2 || operation != 18 || indirect != 0 ||
      (primary_key = 0, primary_key_count = 0,
       iterator->primary_key(iterator, &primary_key, &primary_key_count)) !=
          CPKT_SQLITE_OK ||
      primary_key == 0 || primary_key_count != 2 || primary_key[0] != 1 ||
      iterator->close(iterator) != CPKT_SQLITE_OK)
    return 20;
  if (session->changeset_size(session).high != 0 ||
      session->changeset_size(session).low == 0 ||
      session->memory_used(session).high != 0 ||
      session->memory_used(session).low == 0)
    return 21;
  stream.input = 0;
  stream.input_size = 0;
  stream.input_offset = 0;
  stream.output_size = 0;
  if (session->changeset_strm(session, stream_output, &stream) !=
          CPKT_SQLITE_OK ||
      stream.output_size != changeset->byte_count)
    return 22;
  replica = cpkt_sqlite_new(":memory:");
  if (replica == 0 ||
      replica->tx(
          replica,
          "create table session_item (id integer primary key, value text)", 0,
          0) != CPKT_SQLITE_OK)
    return 23;
  rebase = 0;
  if (cpkt_sqlite_changeset_apply_ex(replica, changeset, 0, 0, 0, 0, &rebase) !=
          CPKT_SQLITE_OK ||
      rebase != 0 ||
      replica->prepare(replica, "select count(*) from session_item", -1, 0,
                       &statement, 0) != CPKT_SQLITE_OK ||
      statement == 0 || statement->step(statement) != CPKT_SQLITE_ROW ||
      statement->column_int(statement, 0) != 1 ||
      statement->finalize(statement) != CPKT_SQLITE_OK)
    return 24;
  replica->close(replica);
  replica = cpkt_sqlite_new(":memory:");
  if (replica == 0 ||
      replica->tx(
          replica,
          "create table session_item (id integer primary key, value text)", 0,
          0) != CPKT_SQLITE_OK ||
      cpkt_sqlite_changeset_apply_v3(replica, changeset,
                                     accept_changeset_iterator, 0, 0, 0,
                                     &rebase) != CPKT_SQLITE_OK ||
      rebase != 0)
    return 25;
  replica->close(replica);
  stream.input = changeset->data;
  stream.input_size = changeset->byte_count;
  stream.input_offset = 0;
  stream.output_size = 0;
  iterator = 0;
  if (cpkt_sqlite_changeset_start_strm(stream_input, &stream, 0, &iterator) !=
          CPKT_SQLITE_OK ||
      iterator == 0 || iterator->next(iterator) != CPKT_SQLITE_ROW ||
      iterator->close(iterator) != CPKT_SQLITE_OK ||
      stream.input_offset != stream.input_size)
    return 26;
  inverted = 0;
  combined = 0;
  if (cpkt_sqlite_changeset_invert(changeset, &inverted) != CPKT_SQLITE_OK ||
      inverted == 0) {
    return 27;
  }
  status = cpkt_sqlite_changeset_concat(changeset, inverted, &combined);
  if (status != CPKT_SQLITE_OK)
    return 40 + status;
  if (combined == 0)
    return 28;
  left_stream.input = changeset->data;
  left_stream.input_size = changeset->byte_count;
  left_stream.input_offset = 0;
  left_stream.output_size = 0;
  right_stream.input = inverted->data;
  right_stream.input_size = inverted->byte_count;
  right_stream.input_offset = 0;
  right_stream.output_size = 0;
  result_stream.input = NULL;
  result_stream.input_size = 0;
  result_stream.input_offset = 0;
  result_stream.output_size = 0;
  if (cpkt_sqlite_changeset_concat_strm(
          stream_input, &left_stream, stream_input, &right_stream,
          stream_output, &result_stream) != CPKT_SQLITE_OK ||
      left_stream.input_offset != left_stream.input_size ||
      right_stream.input_offset != right_stream.input_size ||
      result_stream.output_size != combined->byte_count)
    return 90;
  left_stream.input_offset = 0;
  result_stream.output_size = 0;
  if (cpkt_sqlite_changeset_invert_strm(stream_input, &left_stream,
                                        stream_output,
                                        &result_stream) != CPKT_SQLITE_OK ||
      left_stream.input_offset != left_stream.input_size ||
      result_stream.output_size != inverted->byte_count)
    return 91;
  changegroup = 0;
  grouped = 0;
  if (cpkt_sqlite_changegroup_new(&changegroup) != CPKT_SQLITE_OK ||
      changegroup == 0 ||
      changegroup->add(changegroup, changeset) != CPKT_SQLITE_OK ||
      changegroup->output(changegroup, &grouped) != CPKT_SQLITE_OK ||
      grouped == 0 || grouped->byte_count <= 0)
    return 29;
  grouped->free(grouped);
  changegroup->close(changegroup);
  combined->free(combined);
  inverted->free(inverted);
  changeset->free(changeset);
  if (cpkt_sqlite_set_preupdate_hook(db, preupdate_noop, 0) !=
      CPKT_SQLITE_MISUSE)
    return 30;
  session->close(session);
  preupdate_blob.call_count = 0;
  preupdate_blob.column = -1;
  blob = 0;
  if (cpkt_sqlite_set_preupdate_hook(db, preupdate_blob_write,
                                     &preupdate_blob) != CPKT_SQLITE_OK ||
      cpkt_sqlite_open_blob(db, "main", "item", "payload",
                            cpkt_sqlite_i64_make(0, 1), 1,
                            &blob) != CPKT_SQLITE_OK ||
      blob == 0 || blob->write(blob, "Q", 1, 0) != CPKT_SQLITE_OK ||
      blob->close(blob) != CPKT_SQLITE_OK || preupdate_blob.call_count != 1 ||
      preupdate_blob.column != 2 ||
      cpkt_sqlite_set_preupdate_hook(db, 0, 0) != CPKT_SQLITE_OK)
    return 73;
  count = 0;
  cpkt_sqlite_set_update_hook(db, update_count, &count);
  if (db->tx(db, "insert into item values ('beta', 2, x'00')", 0, 0) !=
          CPKT_SQLITE_OK ||
      count != 1)
    return 31;
  cpkt_sqlite_set_update_hook(db, 0, 0);
  db->close(db);
  if (virtual_table.module_destroy_count != 1)
    return 62;
  if (cpkt_sqlite_global_recover() != CPKT_SQLITE_OK ||
      cpkt_sqlite_sleep(0) < 0)
    return 60;
  cpkt_sqlite_thread_cleanup();
  return 0;
}
