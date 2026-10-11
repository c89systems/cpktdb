#include <cpkt/sqlite.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct probe_counts {
  int create_calls;
  int connect_calls;
  int table_cleanup;
  int open_calls;
  int cursor_cleanup;
  int table_live;
  int cursor_live;
  int fail_table_adapter;
  int fail_cursor_adapter;
  int refuse_connect;
  int refuse_open;
  int callback_failure_cleanup;
  int one_row;
  int find_calls;
  void *function_context;
  cpkt_sqlite_scalar_callback function;
} probe_counts;

static int fail_next_calloc;
static void *allocations[2048];
static int live_allocations;
void *__real_calloc(size_t count, size_t size);
void __real_free(void *pointer);
void *__wrap_calloc(size_t count, size_t size) {
  void *pointer;
  size_t index;
  if (fail_next_calloc) {
    fail_next_calloc = 0;
    return NULL;
  }
  pointer = __real_calloc(count, size);
  if (pointer != NULL) {
    for (index = 0; index < sizeof(allocations) / sizeof(allocations[0]);
         ++index)
      if (allocations[index] == NULL) {
        allocations[index] = pointer;
        ++live_allocations;
        return pointer;
      }
    abort();
  }
  return pointer;
}

void __wrap_free(void *pointer) {
  size_t index;
  if (pointer != NULL)
    for (index = 0; index < sizeof(allocations) / sizeof(allocations[0]);
         ++index)
      if (allocations[index] == pointer) {
        allocations[index] = NULL;
        --live_allocations;
        break;
      }
  __real_free(pointer);
}

static int table_connect(void *context, cpkt_sqlite *database,
                         int argument_count, const char *const *arguments,
                         cpkt_sqlite_virtual_table **out, char **error_out,
                         int create) {
  probe_counts *counts;
  cpkt_sqlite_virtual_table *table;
  void *state;
  (void)argument_count;
  (void)arguments;
  counts = (probe_counts *)context;
  if (create)
    ++counts->create_calls;
  else
    ++counts->connect_calls;
  *out = NULL;
  if (counts->refuse_connect) {
    state = malloc(1);
    if (state == NULL)
      return CPKT_SQLITE_NOMEM;
    table = cpkt_sqlite_virtual_table_new(state);
    if (table == NULL) {
      free(state);
      return CPKT_SQLITE_NOMEM;
    }
    free(table->state);
    free(table);
    ++counts->callback_failure_cleanup;
    *error_out = cpkt_sqlite_format("callback refusal");
    return CPKT_SQLITE_ERROR;
  }
  if (cpkt_sqlite_declare_virtual_table(
          database, "CREATE TABLE x(value INTEGER)") != CPKT_SQLITE_OK)
    return CPKT_SQLITE_ERROR;
  state = malloc(1);
  if (state == NULL)
    return CPKT_SQLITE_NOMEM;
  table = cpkt_sqlite_virtual_table_new(state);
  if (table == NULL) {
    free(state);
    return CPKT_SQLITE_NOMEM;
  }
  ++counts->table_live;
  *out = table;
  if (counts->fail_table_adapter)
    fail_next_calloc = 1;
  return CPKT_SQLITE_OK;
}

static int on_create(void *context, cpkt_sqlite *database, int argument_count,
                     const char *const *arguments,
                     cpkt_sqlite_virtual_table **out, char **error_out) {
  return table_connect(context, database, argument_count, arguments, out,
                       error_out, 1);
}

static int on_connect(void *context, cpkt_sqlite *database, int argument_count,
                      const char *const *arguments,
                      cpkt_sqlite_virtual_table **out, char **error_out) {
  return table_connect(context, database, argument_count, arguments, out,
                       error_out, 0);
}

static int on_table_cleanup(void *context, cpkt_sqlite_virtual_table *table) {
  probe_counts *counts;
  counts = (probe_counts *)context;
  ++counts->table_cleanup;
  --counts->table_live;
  free(table->state);
  table->state = NULL;
  return CPKT_SQLITE_OK;
}

static int on_best_index(void *context, cpkt_sqlite_virtual_table *table,
                         cpkt_sqlite_index_info *info) {
  (void)context;
  (void)table;
  info->estimated_cost = 1.0;
  return CPKT_SQLITE_OK;
}

static int on_open(void *context, cpkt_sqlite_virtual_table *table,
                   cpkt_sqlite_virtual_cursor **out) {
  probe_counts *counts;
  cpkt_sqlite_virtual_cursor *cursor;
  void *state;
  counts = (probe_counts *)context;
  ++counts->open_calls;
  *out = NULL;
  if (counts->refuse_open) {
    state = malloc(1);
    if (state == NULL)
      return CPKT_SQLITE_NOMEM;
    cursor = cpkt_sqlite_virtual_cursor_new(table, state);
    if (cursor == NULL) {
      free(state);
      return CPKT_SQLITE_NOMEM;
    }
    free(cursor->state);
    free(cursor);
    ++counts->callback_failure_cleanup;
    return CPKT_SQLITE_ERROR;
  }
  state = malloc(1);
  if (state == NULL)
    return CPKT_SQLITE_NOMEM;
  cursor = cpkt_sqlite_virtual_cursor_new(table, state);
  if (cursor == NULL) {
    free(state);
    return CPKT_SQLITE_NOMEM;
  }
  ++counts->cursor_live;
  *out = cursor;
  if (counts->fail_cursor_adapter)
    fail_next_calloc = 1;
  return CPKT_SQLITE_OK;
}

static int on_close(void *context, cpkt_sqlite_virtual_cursor *cursor) {
  probe_counts *counts;
  counts = (probe_counts *)context;
  ++counts->cursor_cleanup;
  --counts->cursor_live;
  free(cursor->state);
  cursor->state = NULL;
  return CPKT_SQLITE_OK;
}

static int on_filter(void *context, cpkt_sqlite_virtual_cursor *cursor,
                     int index_number, const char *index_string,
                     int argument_count, cpkt_sqlite_value *const *arguments) {
  (void)context;
  (void)cursor;
  (void)index_number;
  (void)index_string;
  (void)argument_count;
  (void)arguments;
  *(char *)cursor->state = 0;
  return CPKT_SQLITE_OK;
}

static int on_next(void *context, cpkt_sqlite_virtual_cursor *cursor) {
  (void)context;
  (void)cursor;
  *(char *)cursor->state = 1;
  return CPKT_SQLITE_OK;
}

static int on_eof(void *context, cpkt_sqlite_virtual_cursor *cursor) {
  probe_counts *counts;
  counts = (probe_counts *)context;
  return !counts->one_row || *(char *)cursor->state;
}

static int on_column(void *context, cpkt_sqlite_virtual_cursor *cursor,
                     cpkt_sqlite_context *result, int column) {
  (void)context;
  (void)cursor;
  (void)column;
  cpkt_sqlite_context_result_int(result, 41);
  return CPKT_SQLITE_OK;
}

static int on_rowid(void *context, cpkt_sqlite_virtual_cursor *cursor,
                    cpkt_sqlite_i64 *rowid) {
  (void)context;
  (void)cursor;
  *rowid = cpkt_sqlite_i64_make(0UL, 1UL);
  return CPKT_SQLITE_OK;
}

static int run_case(int kind) {
  probe_counts counts;
  cpkt_sqlite_module_methods methods;
  cpkt_sqlite *database;
  const char *sql;
  int status;
  int failed;
  memset(&counts, 0, sizeof(counts));
  memset(&methods, 0, sizeof(methods));
  counts.fail_table_adapter = kind == 1 || kind == 2;
  counts.fail_cursor_adapter = kind == 3;
  counts.refuse_connect = kind == 4;
  counts.refuse_open = kind == 5;
  methods.context = &counts;
  /* Equal native xCreate/xConnect pointers enable eponymous lookup. */
  methods.create = kind == 1 ? on_create : on_connect;
  methods.connect = on_connect;
  methods.best_index = on_best_index;
  methods.disconnect = on_table_cleanup;
  methods.destroy_table = on_table_cleanup;
  methods.open = on_open;
  methods.close = on_close;
  methods.filter = on_filter;
  methods.next = on_next;
  methods.eof = on_eof;
  methods.column = on_column;
  methods.rowid = on_rowid;
  database = cpkt_sqlite_new(":memory:");
  if (database == NULL ||
      cpkt_sqlite_create_module(database, "probe", &methods) != CPKT_SQLITE_OK)
    return 0;
  sql =
      kind == 1 ? "CREATE VIRTUAL TABLE t USING probe" : "SELECT * FROM probe";
  status = database->tx(database, sql, NULL, NULL);
  fail_next_calloc = 0;
  /* Once the adapter moves before the callback, a one-shot libc fault may
   * instead strike a later SQLite allocation. Either status is valid then;
   * the successfully created table must still receive one cleanup. */
  failed =
      status != (kind == 4 || kind == 5 ? CPKT_SQLITE_ERROR : CPKT_SQLITE_OK) &&
      !(kind == 2 && status == CPKT_SQLITE_NOMEM);
  if (kind == 4 &&
      (database->error(database) == NULL ||
       strstr(database->error(database), "callback refusal") == NULL))
    failed = 1;
  if (kind == 1 && status == CPKT_SQLITE_OK &&
      database->tx(database, "DROP TABLE t", NULL, NULL) != CPKT_SQLITE_OK)
    failed = 1;
  database->close(database);
  if (counts.table_live != 0 || counts.cursor_live != 0 ||
      counts.callback_failure_cleanup != (kind == 4 || kind == 5 ? 1 : 0) ||
      counts.table_cleanup !=
          counts.create_calls + counts.connect_calls - (kind == 4 ? 1 : 0) ||
      counts.cursor_cleanup != counts.open_calls - (kind == 5 ? 1 : 0))
    failed = 1;
  if (failed)
    fprintf(stderr,
            "module case %d: status=%d create=%d connect=%d table_cleanup=%d "
            "open=%d cursor_cleanup=%d live=%d/%d\n",
            kind, status, counts.create_calls, counts.connect_calls,
            counts.table_cleanup, counts.open_calls, counts.cursor_cleanup,
            counts.table_live, counts.cursor_live);
  return !failed;
}

static void scalar_value(cpkt_sqlite_context *context, int argument_count,
                         cpkt_sqlite_value *const *arguments, void *user_data) {
  (void)argument_count;
  (void)arguments;
  cpkt_sqlite_context_result_int(context, *(int *)user_data);
}

static void scalar_shifted(cpkt_sqlite_context *context, int argument_count,
                           cpkt_sqlite_value *const *arguments,
                           void *user_data) {
  (void)argument_count;
  (void)arguments;
  cpkt_sqlite_context_result_int(context, *(int *)user_data + 100);
}

static int on_find_function(void *context, cpkt_sqlite_virtual_table *table,
                            int argument_count, const char *name,
                            cpkt_sqlite_scalar_callback *function,
                            void **user_data) {
  probe_counts *counts;
  (void)table;
  counts = (probe_counts *)context;
  if (argument_count != 1 || strcmp(name, "probe_value") != 0)
    return 0;
  ++counts->find_calls;
  *function = counts->function;
  *user_data = counts->function_context;
  return 1;
}

static int statement_value(cpkt_sqlite_statement *statement, int expected) {
  return statement != NULL && statement->step(statement) == CPKT_SQLITE_ROW &&
         statement->column_int(statement, 0) == expected;
}

static int query_value(cpkt_sqlite *database, const char *sql, int expected) {
  cpkt_sqlite_statement *statement;
  int ok;
  statement = NULL;
  if (database->prepare(database, sql, -1, 0, &statement, NULL) !=
      CPKT_SQLITE_OK)
    return 0;
  ok = statement_value(statement, expected);
  return statement->finalize(statement) == CPKT_SQLITE_OK && ok;
}

static int virtual_function_lifetime(void) {
  probe_counts counts;
  cpkt_sqlite_module_methods methods;
  cpkt_sqlite *database;
  cpkt_sqlite_statement *first_statement;
  cpkt_sqlite_statement *second_statement;
  const char *query;
  int first;
  int second;
  int baseline;
  int retained;
  int index;
  int ok;
  memset(&counts, 0, sizeof(counts));
  memset(&methods, 0, sizeof(methods));
  baseline = live_allocations;
  first = 11;
  second = 22;
  counts.one_row = 1;
  counts.function_context = &first;
  counts.function = scalar_value;
  methods.context = &counts;
  methods.create = on_connect;
  methods.connect = on_connect;
  methods.best_index = on_best_index;
  methods.disconnect = on_table_cleanup;
  methods.destroy_table = on_table_cleanup;
  methods.open = on_open;
  methods.close = on_close;
  methods.filter = on_filter;
  methods.next = on_next;
  methods.eof = on_eof;
  methods.column = on_column;
  methods.rowid = on_rowid;
  methods.find_function = on_find_function;
  query = "SELECT probe_value(value) FROM probe";
  database = cpkt_sqlite_new(":memory:");
  if (database == NULL)
    return 0;
  ok = cpkt_sqlite_create_module(database, "probe", &methods) ==
           CPKT_SQLITE_OK &&
       cpkt_sqlite_overload_function(database, "probe_value", 1) ==
           CPKT_SQLITE_OK &&
       query_value(database, query, first);
  retained = live_allocations;
  for (index = 0; ok && index < 100; ++index)
    ok = query_value(database, query, first) && live_allocations == retained;
  first_statement = NULL;
  second_statement = NULL;
  if (ok) {
    ok = database->prepare(database, query, -1, 0, &first_statement, NULL) ==
         CPKT_SQLITE_OK;
    counts.function_context = &second;
    ok = ok && database->prepare(database, query, -1, 0, &second_statement,
                                 NULL) == CPKT_SQLITE_OK;
    ok = ok && statement_value(first_statement, first) &&
         statement_value(second_statement, second);
  }
  if (first_statement != NULL)
    ok = first_statement->finalize(first_statement) == CPKT_SQLITE_OK && ok;
  if (second_statement != NULL)
    ok = second_statement->finalize(second_statement) == CPKT_SQLITE_OK && ok;
  counts.function = scalar_shifted;
  ok = ok && query_value(database, query, second + 100);
  retained = live_allocations;
  for (index = 0; ok && index < 100; ++index) {
    counts.function = index % 2 ? scalar_value : scalar_shifted;
    ok = query_value(database, query, second + (index % 2 ? 0 : 100)) &&
         live_allocations == retained;
  }
  ok = ok && counts.find_calls >= 203;
  database->close(database);
  ok = ok && live_allocations == baseline && counts.table_live == 0 &&
       counts.cursor_live == 0;
  if (!ok)
    fprintf(stderr, "virtual-function bindings grow across repeated queries or "
                    "lose callback context\n");
  return ok;
}

static int utf16_function_lifetime(void) {
  unsigned short name[] = {'p', 0x00e9, 0};
  const char *query;
  cpkt_sqlite *database;
  cpkt_sqlite_statement *statement;
  int first;
  int second;
  int baseline;
  int registered;
  int active;
  int index;
  int ok;
  first = 11;
  second = 22;
  query = "SELECT p\303\251()";
  baseline = live_allocations;
  database = cpkt_sqlite_new(":memory:");
  if (database == NULL)
    return 0;
  registered = live_allocations;
  ok =
      cpkt_sqlite_create_function16(database, name, 0, CPKT_SQLITE_ANY, &first,
                                    scalar_value, NULL, NULL) == CPKT_SQLITE_OK;
  registered += 1;
  for (index = 0; ok && index < 100; ++index)
    ok = cpkt_sqlite_create_function16(database, name, 0, CPKT_SQLITE_ANY,
                                       &second, scalar_value, NULL,
                                       NULL) == CPKT_SQLITE_OK &&
         query_value(database, query, second) && live_allocations == registered;
  ok = ok &&
       cpkt_sqlite_create_function16(database, name, -2, CPKT_SQLITE_ANY,
                                     &first, scalar_value, NULL,
                                     NULL) == CPKT_SQLITE_MISUSE &&
       live_allocations == registered;
  fail_next_calloc = 1;
  if (ok)
    ok = cpkt_sqlite_create_function16(database, name, 0, CPKT_SQLITE_ANY,
                                       &first, scalar_value, NULL,
                                       NULL) == CPKT_SQLITE_NOMEM;
  fail_next_calloc = 0;
  statement = NULL;
  if (ok) {
    ok = database->prepare(database, query, -1, 0, &statement, NULL) ==
             CPKT_SQLITE_OK &&
         statement_value(statement, second);
    active = live_allocations;
    ok = ok &&
         cpkt_sqlite_create_function16(database, name, 0, CPKT_SQLITE_ANY,
                                       &first, scalar_value, NULL,
                                       NULL) == CPKT_SQLITE_BUSY &&
         live_allocations == active;
  }
  if (statement != NULL)
    ok = statement->finalize(statement) == CPKT_SQLITE_OK && ok;
  ok = ok &&
       cpkt_sqlite_create_function16(database, name, 0, CPKT_SQLITE_ANY, NULL,
                                     NULL, NULL, NULL) == CPKT_SQLITE_OK &&
       live_allocations == registered - 1;
  statement = NULL;
  if (ok)
    ok = database->prepare(database, query, -1, 0, &statement, NULL) ==
             CPKT_SQLITE_ERROR &&
         statement == NULL;
  database->close(database);
  ok = ok && live_allocations == baseline;
  if (!ok)
    fprintf(stderr, "UTF-16 function bindings survive replacement, failure or "
                    "unregistration\n");
  return ok;
}

int main(void) {
  int kind;
  int failed;
  failed = 0;
  for (kind = 1; kind <= 6; ++kind)
    if (!run_case(kind))
      failed = 1;
  if (!virtual_function_lifetime())
    failed = 1;
  if (!utf16_function_lifetime())
    failed = 1;
  return failed;
}
