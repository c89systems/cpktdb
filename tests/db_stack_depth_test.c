#define _POSIX_C_SOURCE 200809L
#include <cpkt/postgres.h>
#include <cpkt/sqlite.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int check_scalar(cpkt_sqlite *db, const char *sql, int expected) {
  cpkt_sqlite_statement *statement = NULL;
  int ok;
  if (cpkt_sqlite_prepare(db, sql, -1, 0, &statement, NULL) != CPKT_SQLITE_OK) {
    fprintf(stderr, "prepare failed: %s\n", cpkt_sqlite_error(db));
    return 0;
  }
  ok = statement->step(statement) == CPKT_SQLITE_ROW &&
       statement->column_int(statement, 0) == expected;
  statement->finalize(statement);
  return ok;
}

static void mark_operand(cpkt_sqlite_context *context, int argc,
                         cpkt_sqlite_value *const *argv, void *data) {
  int *order = (int *)data;
  int value = cpkt_sqlite_value_int(argv[0]);
  (void)argc;
  *order = *order * 10 + value;
  cpkt_sqlite_context_result_int(context, value);
}

static int expression_depth(cpkt_sqlite *db) {
  int limit = cpkt_sqlite_limit(db, CPKT_SQLITE_LIMIT_EXPR_DEPTH, -1);
  char *sql;
  char *cursor;
  int i, status, ok = 1;
  if (limit < 2 || limit > 10000)
    return 0;
  sql = (char *)malloc(16U + 2U * (size_t)limit);
  if (sql == NULL)
    return 0;
  strcpy(sql, "SELECT 1");
  cursor = sql + strlen(sql);
  for (i = 1; i < limit; ++i) {
    memcpy(cursor, "+1", 2);
    cursor += 2;
  }
  *cursor = '\0';
  fprintf(stderr, "expression depth boundary: %d\n", limit);
  ok = check_scalar(db, sql, limit);
  memcpy(cursor, "+1", 3);
  status = cpkt_sqlite_exec(db, sql, NULL, NULL);
  if (status != CPKT_SQLITE_ERROR)
    ok = 0;
  free(sql);
  return ok;
}

/* Use nonconstant operands as well as literals: constant factoring and
 * expression-index lookup must retain their original register ownership. */
static int indexed_operand_results(cpkt_sqlite *db) {
  static const struct {
    const char *expression;
    int expected;
  } cases[] = {
      {"(x+2)*(x+3)", 12},         {"(x+3)*(x+2)", 12},
      {"(x+2)-(x+3)", -1},         {"(x+3)-(x+2)", 1},
      {"(x+2)/(x+1)", 1},          {"(x+4)/(x+2)", 1},
      {"(x+2)*((x+3)+(x+4))", 27}, {"(x+2)*(x+2)", 9},
      {"(x+2)+((x+2)*(x+3))", 15}, {"((x+2)*nullif(x,1)) IS NULL", 1},
  };
  static const char *access[] = {"INDEXED BY operand_expr", "NOT INDEXED"};
  char sql[256];
  size_t i, mode;
  for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    for (mode = 0; mode < sizeof(access) / sizeof(access[0]); ++mode) {
      sprintf(sql, "SELECT %s FROM operand %s", cases[i].expression,
              access[mode]);
      if (!check_scalar(db, sql, cases[i].expected)) {
        fprintf(stderr, "indexed operand result mismatch: %s\n", sql);
        return 0;
      }
    }
  }
  if (cpkt_sqlite_exec(db, "INSERT INTO operand VALUES(-3),(0),(5),(NULL)",
                       NULL, NULL) != CPKT_SQLITE_OK)
    return 0;
  for (mode = 0; mode < sizeof(access) / sizeof(access[0]); ++mode) {
    sprintf(sql, "SELECT sum((x+2)*(x+3)) FROM operand %s", access[mode]);
    if (!check_scalar(db, sql, 74))
      return 0;
  }
  return 1;
}

static int expression_variants(cpkt_sqlite *db) {
  static const char *operators[] = {"+", "-", "*",  "/",  "%",
                                    "&", "|", "<<", ">>", "||"};
  static const int results[] = {998, -998, 0, 0, 0, 0, 1, 0, 0, 0};
  char *sql = (char *)malloc(32768);
  char *cursor;
  size_t op;
  int i, order = 0, ok = 1;
  if (sql == NULL)
    return 0;
  if (cpkt_sqlite_exec(db,
                       "CREATE TABLE operand(x); INSERT INTO operand VALUES(1)",
                       NULL, NULL) != CPKT_SQLITE_OK) {
    free(sql);
    return 0;
  }
  for (op = 0; op < sizeof(operators) / sizeof(operators[0]); ++op) {
    strcpy(sql, "SELECT ");
    if (op == 9)
      strcat(sql, "length('");
    else
      strcat(sql, "0");
    if (op == 9)
      strcat(sql, "'");
    cursor = sql + strlen(sql);
    for (i = 0; i < 998; ++i) {
      strcpy(cursor, operators[op]);
      cursor += strlen(cursor);
      if (op == 9) {
        strcpy(cursor, "''");
        cursor += 2;
      } else {
        *cursor++ = 'x';
      }
    }
    strcpy(cursor, op == 9 ? ") FROM operand" : " FROM operand");
    fprintf(stderr, "deep operator: %s\n", operators[op]);
    if (!check_scalar(db, sql, results[op])) {
      fprintf(stderr, "operator failed: %s\n", operators[op]);
      ok = 0;
      break;
    }
  }
  /* A subquery flag propagates all the way up the left-hand arithmetic spine.
   */
  strcpy(sql, "SELECT (SELECT x FROM operand)");
  cursor = sql + strlen(sql);
  for (i = 0; i < 990; ++i) {
    memcpy(cursor, "+1", 2);
    cursor += 2;
  }
  *cursor = '\0';
  if (ok && !check_scalar(db, sql, 991))
    ok = 0;
  strcpy(sql, "SELECT ");
  cursor = sql + strlen(sql);
  for (i = 0; i < 990; ++i) {
    memcpy(cursor, "x-(", 3);
    cursor += 3;
  }
  *cursor++ = 'x';
  for (i = 0; i < 990; ++i)
    *cursor++ = ')';
  strcpy(cursor, " FROM operand");
  if (ok && cpkt_sqlite_exec(db, sql, NULL, NULL) != CPKT_SQLITE_ERROR)
    ok = 0;
  /* Parentheses consume the independent parser-depth budget too. */
  strcpy(sql, "SELECT ");
  cursor = sql + strlen(sql);
  for (i = 0; i < 600; ++i) {
    memcpy(cursor, "x-(", 3);
    cursor += 3;
  }
  *cursor++ = 'x';
  for (i = 0; i < 600; ++i)
    *cursor++ = ')';
  strcpy(cursor, " FROM operand");
  if (ok && !check_scalar(db, sql, 1))
    ok = 0;
  if (ok && (cpkt_sqlite_create_function(db, "mark_operand", 1,
                                         CPKT_SQLITE_UTF8, &order, mark_operand,
                                         NULL, NULL, NULL) != CPKT_SQLITE_OK ||
             !check_scalar(db, "SELECT mark_operand(1)-mark_operand(2)", -1) ||
             order != 12))
    ok = 0;
  order = 0;
  if (ok &&
      (!check_scalar(db, "SELECT (NULL+(SELECT mark_operand(1))) IS NULL", 1) ||
       !check_scalar(db, "SELECT ((SELECT mark_operand(2))+NULL) IS NULL", 1) ||
       order != 0))
    ok = 0;
  if (ok &&
      (!check_scalar(db, "SELECT (20-3)-2", 15) ||
       !check_scalar(db, "SELECT 20-(3-2)", 19) ||
       !check_scalar(db, "SELECT (NULL+(SELECT x FROM operand)) IS NULL", 1) ||
       !check_scalar(db, "SELECT ((SELECT x FROM operand)+NULL) IS NULL", 1) ||
       !check_scalar(db, "SELECT (1/0) IS NULL", 1) ||
       !check_scalar(db, "SELECT (-9223372036854775808+1)=-9223372036854775807",
                     1)))
    ok = 0;
  if (ok &&
      (cpkt_sqlite_exec(db, "CREATE INDEX operand_expr ON operand(x+2)", NULL,
                        NULL) != CPKT_SQLITE_OK ||
       !check_scalar(
           db,
           "SELECT (x+2)*3 FROM operand INDEXED BY operand_expr WHERE x+2=3",
           9)))
    ok = 0;
  if (ok && !indexed_operand_results(db))
    ok = 0;
  free(sql);
  return ok;
}

/* Keep each syntax family in its own CTest process so a guard fault identifies
 * the failing compiler path. All generated SQL is held on the heap. */
static int expression_shape_level(cpkt_sqlite *db, const char *shape,
                                  int shallow) {
  char *sql = (char *)malloc(131072);
  char *cursor;
  const char *prefix = "", *suffix = "";
  int depth = 900, i, result = 1, status, ok;
  int where_only = strcmp(shape, "where") == 0;
  cpkt_sqlite_statement *statement = NULL;
  if (sql == NULL)
    return 0;
  if (strcmp(shape, "not") == 0)
    prefix = "NOT ";
  else if (strcmp(shape, "bitnot") == 0)
    prefix = "~";
  else if (strcmp(shape, "function") == 0) {
    prefix = "abs(";
    suffix = ")";
    depth = 400;
  } else if (strcmp(shape, "cast") == 0) {
    prefix = "CAST(";
    suffix = " AS INTEGER)";
    depth = 400;
  } else if (strcmp(shape, "case") == 0) {
    prefix = "CASE WHEN 1 THEN ";
    suffix = " ELSE 0 END";
    depth = 250;
  } else if (strcmp(shape, "coalesce") == 0) {
    prefix = "coalesce(";
    suffix = ",0)";
    depth = 400;
  } else if (strcmp(shape, "subquery") == 0) {
    prefix = "(SELECT ";
    suffix = ")";
    depth = 200;
  } else if (strcmp(shape, "planner") == 0) {
    prefix = "(x=1 OR (x>0 AND ";
    suffix = "))";
    depth = 200;
    where_only = 1;
  } else if (strcmp(shape, "comparison") != 0 && strcmp(shape, "and") != 0 &&
             strcmp(shape, "or") != 0 && strcmp(shape, "where") != 0) {
    free(sql);
    return 0;
  }
  if (cpkt_sqlite_exec(
          db,
          "CREATE TABLE IF NOT EXISTS shape_operand(x); DELETE FROM "
          "shape_operand; INSERT INTO shape_operand VALUES(1)",
          NULL, NULL) != CPKT_SQLITE_OK) {
    free(sql);
    return 0;
  }
  if (strcmp(shape, "planner") == 0 &&
      cpkt_sqlite_exec(
          db, "CREATE INDEX IF NOT EXISTS shape_index ON shape_operand(x)",
          NULL, NULL) != CPKT_SQLITE_OK) {
    free(sql);
    return 0;
  }
  if (shallow)
    depth = 8;
  strcpy(sql, where_only ? "SELECT x FROM shape_operand WHERE " : "SELECT ");
  cursor = sql + strlen(sql);
  for (i = 0; i < depth; ++i) {
    strcpy(cursor, prefix);
    cursor += strlen(prefix);
  }
  *cursor++ = 'x';
  for (i = 0; i < depth; ++i) {
    const char *tail = suffix;
    if (strcmp(shape, "comparison") == 0)
      tail = "=1";
    if (strcmp(shape, "and") == 0 || strcmp(shape, "where") == 0)
      tail = " AND x";
    if (strcmp(shape, "or") == 0)
      tail = " OR x";
    strcpy(cursor, tail);
    cursor += strlen(tail);
  }
  strcpy(cursor, where_only ? "" : " FROM shape_operand");
  fprintf(stderr, "expression shape %s, depth %d\n", shape, depth);
  status = cpkt_sqlite_prepare(db, sql, -1, 0, &statement, NULL);
  if (status == CPKT_SQLITE_OK) {
    ok = statement != NULL && statement->step(statement) == CPKT_SQLITE_ROW &&
         statement->column_int(statement, 0) == result;
    if (statement != NULL)
      statement->finalize(statement);
  } else {
    const char *error = cpkt_sqlite_error(db);
    fprintf(stderr, "prepare status %d: %s\n", status, error);
    /* SQLite's independent parser-depth limit may reject nested syntax before
     * reaching expression compilation. That remains an ordinary SQL error. */
    ok = !shallow && status == CPKT_SQLITE_ERROR && error != NULL &&
         (strstr(error, "Recursion limit") != NULL ||
          strstr(error, "Expression tree is too large") != NULL ||
          strstr(error, "native stack budget exhausted") != NULL);
  }
  free(sql);
  return ok && check_scalar(db, "SELECT 7", 7);
}

static int boolean_simplification(cpkt_sqlite *db) {
  static const struct {
    const char *expression;
    const char *expected;
  } cases[] = {
      {"(x AND true) OR (y AND false)", "x"},
      {"(x OR false) AND (y OR true)", "x"},
      {"(x AND false) OR (y AND true)", "y"},
      {"(x OR true) AND (y OR false)", "y"},
      {"(x AND y) OR true", "1"},
      {"(x OR y) AND false", "0"},
      {"(x AND false) OR (y AND false)", "0"},
      {"(x OR true) AND (y OR true)", "1"},
  };
  char sql[512];
  size_t i;
  for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    sprintf(sql,
            "WITH operand(x,y) AS (VALUES(1,1),(1,0),(1,NULL),(0,1),"
            "(0,0),(0,NULL),(NULL,1),(NULL,0),(NULL,NULL)) "
            "SELECT count(*) FROM operand WHERE (%s) IS NOT (%s)",
            cases[i].expression, cases[i].expected);
    if (!check_scalar(db, sql, 0)) {
      fprintf(stderr, "boolean simplification mismatch: %s\n", sql);
      return 0;
    }
  }
  return 1;
}

static int expression_shape(cpkt_sqlite *db, const char *shape) {
  if ((strcmp(shape, "and") == 0 || strcmp(shape, "or") == 0) &&
      !boolean_simplification(db))
    return 0;
  return expression_shape_level(db, shape, 1) &&
         expression_shape_level(db, shape, 0);
}

struct migrated_query {
  cpkt_sqlite *db;
  const char *sql;
  int ok;
};

static void *run_migrated_query(void *data) {
  struct migrated_query *query = (struct migrated_query *)data;
  query->ok = check_scalar(query->db, query->sql, 1);
  return NULL;
}

static int stack_migration(cpkt_sqlite *db) {
  struct migrated_query query;
  pthread_attr_t attributes;
  pthread_t worker;
  char *sql = (char *)malloc(8192);
  char *cursor;
  int i, round, status, ok = 1;
  if (sql == NULL)
    return 0;
  strcpy(sql, "SELECT ");
  cursor = sql + strlen(sql);
  for (i = 0; i < 400; ++i) {
    memcpy(cursor, "abs(", 4);
    cursor += 4;
  }
  *cursor++ = '1';
  for (i = 0; i < 400; ++i)
    *cursor++ = ')';
  *cursor = '\0';
  query.db = db;
  query.sql = sql;
  if (pthread_attr_init(&attributes) != 0) {
    free(sql);
    return 0;
  }
  status = pthread_attr_setstacksize(&attributes, 512UL * 1024);
  for (round = 0; status == 0 && round < 2; ++round) {
    /* An explicit larger worker establishes semantic success and primes its
     * own TLS cache. Reuse the same connection on the constrained worker. */
    query.ok = 0;
    status = pthread_create(&worker, &attributes, run_migrated_query, &query);
    if (status != 0)
      break;
    status = pthread_join(worker, NULL);
    if (!query.ok || status != 0) {
      ok = 0;
      break;
    }
    status = cpkt_sqlite_exec(db, sql, NULL, NULL);
    if (status != CPKT_SQLITE_ERROR ||
        strstr(cpkt_sqlite_error(db), "native stack budget exhausted") ==
            NULL ||
        !check_scalar(db, "SELECT 7", 7)) {
      fprintf(stderr, "migration small-worker result: %d %s\n", status,
              cpkt_sqlite_error(db));
      ok = 0;
      break;
    }
    status = 0;
  }
  pthread_attr_destroy(&attributes);
  free(sql);
  return ok && status == 0;
}

struct reentrant_query {
  cpkt_sqlite *db;
  int depth, peak, guarded;
};

static void reenter_sql(cpkt_sqlite_context *context, int argc,
                        cpkt_sqlite_value *const *argv, void *data) {
  struct reentrant_query *query = (struct reentrant_query *)data;
  int status;
  (void)argc;
  (void)argv;
  ++query->depth;
  if (query->depth > query->peak)
    query->peak = query->depth;
  if (query->depth == 512) {
    cpkt_sqlite_context_result_error(context, "test recursion ceiling", -1);
    --query->depth;
    return;
  }
  status = cpkt_sqlite_exec(query->db, "SELECT stack_reenter()", NULL, NULL);
  if (status == CPKT_SQLITE_OK)
    cpkt_sqlite_context_result_int(context, 1);
  else {
    if (strstr(cpkt_sqlite_error(query->db), "native stack budget exhausted") !=
        NULL)
      query->guarded = 1;
    cpkt_sqlite_context_result_error(context, "nested stack exhaustion", -1);
  }
  --query->depth;
}

static int stack_reentrancy(cpkt_sqlite *db) {
  struct reentrant_query query;
  int status, ok;
  memset(&query, 0, sizeof(query));
  query.db = db;
  if (cpkt_sqlite_create_function(db, "stack_reenter", 0, CPKT_SQLITE_UTF8,
                                  &query, reenter_sql, NULL, NULL,
                                  NULL) != CPKT_SQLITE_OK)
    return 0;
  status = cpkt_sqlite_exec(db, "SELECT stack_reenter()", NULL, NULL);
  fprintf(stderr, "recursive SQL callback peak: %d\n", query.peak);
  ok = status == CPKT_SQLITE_ERROR && query.peak > 1 && query.peak < 512 &&
       query.depth == 0 && query.guarded && check_scalar(db, "SELECT 7", 7);
  cpkt_sqlite_create_function(db, "stack_reenter", 0, CPKT_SQLITE_UTF8, NULL,
                              NULL, NULL, NULL, NULL);
  return ok;
}

static int compound_select(cpkt_sqlite *db) {
  char *sql = (char *)malloc(16384);
  char *cursor;
  cpkt_sqlite_statement *statement = NULL;
  int i, status, rows = 0, ok;
  int limit = cpkt_sqlite_limit(db, CPKT_SQLITE_LIMIT_COMPOUND_SELECT, -1);
  if (sql == NULL || limit != 500) {
    free(sql);
    return 0;
  }
  strcpy(sql, "SELECT 1");
  cursor = sql + strlen(sql);
  for (i = 1; i < limit; ++i) {
    strcpy(cursor, " UNION ALL SELECT 1");
    cursor += strlen(cursor);
  }
  status = cpkt_sqlite_prepare(db, sql, -1, 0, &statement, NULL);
  if (status == CPKT_SQLITE_OK) {
    while ((status = statement->step(statement)) == CPKT_SQLITE_ROW)
      ++rows;
    ok = status == CPKT_SQLITE_DONE && rows == limit;
    statement->finalize(statement);
  } else
    ok = status == CPKT_SQLITE_ERROR &&
         strstr(cpkt_sqlite_error(db), "native stack budget exhausted") != NULL;
  strcpy(cursor, " UNION ALL SELECT 1");
  status = cpkt_sqlite_exec(db, sql, NULL, NULL);
  ok = ok && status == CPKT_SQLITE_ERROR && check_scalar(db, "SELECT 7", 7);
  free(sql);
  return ok;
}

static int fts_stack(cpkt_sqlite *db) {
  static const char *engines[] = {"fts3", "fts4", "fts5"};
  char *sql = (char *)malloc(32768);
  char *cursor;
  size_t engine;
  int i, mode, status, ok = 1;
  if (sql == NULL)
    return 0;
  for (engine = 0; engine < 3 && ok; ++engine) {
    sprintf(sql,
            "CREATE VIRTUAL TABLE search USING %s(text); INSERT INTO search "
            "VALUES('token')",
            engines[engine]);
    if (cpkt_sqlite_exec(db, sql, NULL, NULL) != CPKT_SQLITE_OK ||
        !check_scalar(
            db, "SELECT count(*) FROM search WHERE search MATCH 'token'", 1)) {
      ok = 0;
      break;
    }
    for (mode = 0; mode < 2 && ok; ++mode) {
      strcpy(sql, "SELECT count(*) FROM search WHERE search MATCH '");
      cursor = sql + strlen(sql);
      if (mode == 0) {
        for (i = 0; i < 400; ++i)
          *cursor++ = '(';
      }
      strcpy(cursor, "token");
      cursor += 5;
      for (i = 0; i < (mode == 0 ? 400 : 1000); ++i) {
        strcpy(cursor, mode == 0 ? ")" : " OR token");
        cursor += strlen(cursor);
      }
      strcpy(cursor, "'");
      fprintf(stderr, "%s query %s\n", engines[engine],
              mode == 0 ? "nested" : "wide");
      status = cpkt_sqlite_exec(db, sql, NULL, NULL);
      if (status != CPKT_SQLITE_OK &&
          (status != CPKT_SQLITE_ERROR ||
           (strstr(cpkt_sqlite_error(db), "too large") == NULL &&
            strstr(cpkt_sqlite_error(db), "depth") == NULL &&
            strstr(cpkt_sqlite_error(db), "native stack") == NULL &&
            strstr(cpkt_sqlite_error(db), "fts5: parser stack overflow") ==
                NULL))) {
        fprintf(stderr, "FTS error: %d %s\n", status, cpkt_sqlite_error(db));
        ok = 0;
      }
    }
    if (cpkt_sqlite_exec(db, "DROP TABLE search", NULL, NULL) != CPKT_SQLITE_OK)
      ok = 0;
  }
  free(sql);
  return ok && check_scalar(db, "SELECT 7", 7);
}

static cpkt_sqlite_memory_methods original_allocator;
static int allocation_count, allocation_failure = -1;
static int should_fail(void) {
  int position = allocation_count++;
  return allocation_failure >= 0 && position >= allocation_failure;
}
static void *fault_allocate(int bytes) {
  return should_fail() ? NULL : original_allocator.allocate(bytes);
}
static void *fault_reallocate(void *memory, int bytes) {
  return should_fail() ? NULL : original_allocator.reallocate(memory, bytes);
}

static int symlink_stack(int failure, int *observed, int links,
                         int expect_budget) {
  const char *scratch = getenv("TMPDIR");
  char *directory, *path, *resolved;
  char target[32];
  cpkt_sqlite_vfs *vfs;
  cpkt_sqlite *db = NULL;
  int i, ok = 0, made = 0;
  if (scratch == NULL || strlen(scratch) > 900)
    return 0;
  directory = (char *)malloc(1024);
  path = (char *)malloc(1024);
  resolved = (char *)malloc(1024);
  if (directory == NULL || path == NULL || resolved == NULL) {
    free(directory);
    free(path);
    free(resolved);
    return 0;
  }
  sprintf(directory, "%s/sqlite-path-XXXXXX", scratch);
  if (mkdtemp(directory) == NULL) {
    perror("mkdtemp");
    goto done;
  }
  for (i = 0; i < links; ++i) {
    sprintf(path, "%s/link%02d", directory, i);
    if (i == links - 1)
      strcpy(target, "final.db");
    else
      sprintf(target, "link%02d", i + 1);
    if (symlink(target, path) != 0) {
      perror("symlink");
      goto cleanup;
    }
    ++made;
  }
  sprintf(path, "%s/link00", directory);
  vfs = cpkt_sqlite_vfs_find(NULL);
  if (vfs == NULL)
    goto cleanup;
  allocation_count = 0;
  allocation_failure = failure;
  i = vfs->methods.full_path(vfs, path, 1024, resolved);
  allocation_failure = -1;
  if (observed != NULL)
    *observed = allocation_count;
  vfs->close(vfs);
  if (expect_budget && i == CPKT_SQLITE_CANTOPEN) {
    ok = 1;
    goto cleanup;
  }
  if (expect_budget) {
    fprintf(stderr, "expected path stack-budget error, got %d\n", i);
    goto cleanup;
  }
  if (failure >= 0 && i == CPKT_SQLITE_NOMEM) {
    ok = 1;
    goto cleanup;
  }
  if (i != CPKT_SQLITE_OK && i != CPKT_SQLITE_OK_SYMLINK) {
    fprintf(stderr, "full path result %d\n", i);
    goto cleanup;
  }
  db = cpkt_sqlite_new(resolved);
  if (db != NULL) {
    ok = cpkt_sqlite_exec(db, "CREATE TABLE t(x); INSERT INTO t VALUES(7)",
                          NULL, NULL) == CPKT_SQLITE_OK &&
         check_scalar(db, "SELECT x FROM t", 7);
    cpkt_sqlite_close(db);
  }
cleanup:
  for (i = 0; i < made; ++i) {
    sprintf(path, "%s/link%02d", directory, i);
    if (unlink(path) != 0)
      ok = 0;
  }
  sprintf(path, "%s/final.db", directory);
  if (db != NULL && unlink(path) != 0)
    ok = 0;
  if (rmdir(directory) != 0)
    ok = 0;
done:
  free(resolved);
  free(path);
  free(directory);
  return ok;
}

static void *run_small_path(void *data) {
  int *ok = (int *)data;
  *ok = symlink_stack(-1, NULL, 200, 1);
  return NULL;
}

static int symlink_budget(void) {
  pthread_attr_t attributes;
  pthread_t worker;
  int status, ok = 0;
  if (!symlink_stack(-1, NULL, 200, 0) || pthread_attr_init(&attributes) != 0)
    return 0;
  status = pthread_attr_setstacksize(&attributes, 64UL * 1024);
  if (status == 0) {
    status = pthread_create(&worker, &attributes, run_small_path, &ok);
    if (status == 0)
      status = pthread_join(worker, NULL);
  }
  pthread_attr_destroy(&attributes);
  return status == 0 && ok;
}

static int symlink_oom(void) {
  cpkt_sqlite_memory_methods allocator;
  int i, count = 0, ok;
  if (cpkt_sqlite_global_config_memory_methods_get(&original_allocator) !=
      CPKT_SQLITE_OK)
    return 0;
  allocator = original_allocator;
  allocator.allocate = fault_allocate;
  allocator.reallocate = fault_reallocate;
  if (cpkt_sqlite_global_config_memory_methods_set(&allocator) !=
      CPKT_SQLITE_OK)
    return 0;
  ok = symlink_stack(-1, &count, 40, 0) && count > 0;
  for (i = 0; i < count && ok; ++i)
    ok = symlink_stack(i, NULL, 40, 0);
  fprintf(stderr, "path allocation failure positions: %d\n", count);
  if (cpkt_sqlite_shutdown() != CPKT_SQLITE_OK ||
      cpkt_sqlite_global_config_memory_methods_set(&original_allocator) !=
          CPKT_SQLITE_OK)
    ok = 0;
  return ok;
}

static int expression_oom(void) {
  cpkt_sqlite_memory_methods allocator;
  cpkt_sqlite *db;
  cpkt_sqlite_statement *statement;
  char sql[512];
  char *cursor;
  int i, failure, calls = 0, errors = 0, status, ok = 1;
  if (cpkt_sqlite_global_config_memory_methods_get(&original_allocator) !=
      CPKT_SQLITE_OK)
    return 0;
  allocator = original_allocator;
  allocator.allocate = fault_allocate;
  allocator.reallocate = fault_reallocate;
  if (cpkt_sqlite_global_config_memory_methods_set(&allocator) !=
          CPKT_SQLITE_OK ||
      cpkt_sqlite_global_config_two_int(CPKT_SQLITE_CONFIG_LOOKASIDE, 0, 0) !=
          CPKT_SQLITE_OK)
    return 0;
  strcpy(sql, "SELECT x");
  cursor = sql + strlen(sql);
  for (i = 0; i < 63; ++i) {
    memcpy(cursor, "+x", 2);
    cursor += 2;
  }
  strcpy(cursor, " FROM t");
  /* First measure allocations, then fail every prepare allocation in turn.
   * Persistent failure also covers SQLite's allocator retry paths. */
  for (failure = -1; failure < calls; ++failure) {
    db = cpkt_sqlite_new(":memory:");
    if (db == NULL ||
        cpkt_sqlite_exec(db, "CREATE TABLE t(x); INSERT INTO t VALUES(1)", NULL,
                         NULL) != CPKT_SQLITE_OK)
      return 0;
    allocation_count = 0;
    allocation_failure = failure;
    statement = NULL;
    status = cpkt_sqlite_prepare(db, sql, -1, 0, &statement, NULL);
    allocation_failure = -1;
    if (failure == -1)
      calls = allocation_count;
    if (status == CPKT_SQLITE_OK) {
      if (statement == NULL || statement->step(statement) != CPKT_SQLITE_ROW ||
          statement->column_int(statement, 0) != 64)
        ok = 0;
    } else if (status != CPKT_SQLITE_NOMEM || statement != NULL) {
      fprintf(stderr, "unexpected allocation failure result %d at %d\n", status,
              failure);
      ok = 0;
    } else {
      ++errors;
    }
    if (statement != NULL)
      statement->finalize(statement);
    /* The connection must remain usable after interrupted compilation. */
    if (!check_scalar(db, "SELECT 7", 7))
      ok = 0;
    cpkt_sqlite_close(db);
    if (!ok)
      break;
  }
  fprintf(stderr, "expression allocation failure points: %d\n", calls);
  if (calls < 64 || errors < 64)
    ok = 0;
  if (cpkt_sqlite_shutdown() != CPKT_SQLITE_OK ||
      cpkt_sqlite_global_config_memory_methods_set(&original_allocator) !=
          CPKT_SQLITE_OK)
    ok = 0;
  return ok;
}

static int result_string_ok;
static void string_result(cpkt_sqlite_context *context, int argc,
                          cpkt_sqlite_value *const *argv, void *data) {
  cpkt_sqlite_string *string;
  int flags = cpkt_sqlite_value_int(argv[0]);
  (void)data;
  (void)argc;
  string = cpkt_sqlite_string_new(NULL);
  if (string == NULL) {
    cpkt_sqlite_context_result_no_memory(context);
    return;
  }
  string->append_all(string, "hello");
  cpkt_sqlite_context_result_string(NULL, string, CPKT_SQLITE_FINISH);
  if (string->length(string) != 5)
    result_string_ok = 0;
  cpkt_sqlite_context_result_string(context, string, flags);
  if (flags != CPKT_SQLITE_FINISH) {
    if (string->length(string) != (flags == CPKT_SQLITE_COPY ? 5 : 0))
      result_string_ok = 0;
    string->append_all(string, "reuse");
    string->close(string);
  }
}

static int upgraded_api(cpkt_sqlite *db) {
  cpkt_sqlite_i64 detail;
  int flag;
  char sql[128];
  detail = cpkt_sqlite_incomplete("SELECT 1;");
  if (detail.high != 0 || detail.low != 0 ||
      strcmp(cpkt_sqlite_library_version(), CPKT_SQLITE_VERSION) != 0)
    return 0;
  detail = cpkt_sqlite_incomplete("SELECT 'unterminated");
  if (detail.high == 0 && detail.low == 0)
    return 0;
  if (cpkt_sqlite_limit(db, CPKT_SQLITE_LIMIT_SCHEMA, -1) <= 0 ||
      cpkt_sqlite_limit(db, CPKT_SQLITE_LIMIT_TRIGGER_STEPS, -1) <= 0 ||
      cpkt_sqlite_create_function(db, "string_result", 1, CPKT_SQLITE_UTF8,
                                  NULL, string_result, NULL, NULL,
                                  NULL) != CPKT_SQLITE_OK)
    return 0;
  result_string_ok = 1;
  for (flag = CPKT_SQLITE_COPY; flag <= CPKT_SQLITE_FINISH; ++flag) {
    sprintf(sql, "SELECT string_result(%d)='hello'", flag);
    if (!check_scalar(db, sql, 1))
      return 0;
  }
  return result_string_ok;
}

static int json_depth(cpkt_sqlite *db) {
  char *sql;
  char *cursor;
  int depth, i, ok = 1;
  sql = (char *)malloc(4096);
  if (sql == NULL)
    return 0;
  for (depth = 1000; depth <= 1001; ++depth) {
    strcpy(sql, "SELECT json_valid('");
    cursor = sql + strlen(sql);
    for (i = 0; i < depth; ++i)
      *cursor++ = '[';
    *cursor++ = '0';
    for (i = 0; i < depth; ++i)
      *cursor++ = ']';
    strcpy(cursor, "')");
    fprintf(stderr, "JSON nesting boundary: %d\n", depth);
    if (!check_scalar(db, sql, depth == 1000))
      ok = 0;
  }
  free(sql);
  return ok;
}

static int trigger_depth(cpkt_sqlite *db) {
  int status;
  if (!check_scalar(db,
                    "WITH RECURSIVE t(x) AS (VALUES(1) UNION ALL "
                    "SELECT x+1 FROM t WHERE x<10000) SELECT max(x) FROM t",
                    10000))
    return 0;
  status = cpkt_sqlite_exec(db,
                            "PRAGMA recursive_triggers=ON;"
                            "CREATE TABLE t(x);"
                            "CREATE TRIGGER tr AFTER INSERT ON t BEGIN "
                            "INSERT INTO t VALUES(new.x+1); END;"
                            "INSERT INTO t VALUES(0);",
                            NULL, NULL);
  return status == CPKT_SQLITE_ERROR;
}

static int postgres_options(void) {
  char *options;
  char *cursor;
  cpkt_postgres_connection_option *parsed;
  cpkt_postgres_connection_option *option;
  char *error = NULL;
  int i, ok;
  const char *item = "application_name=stack-test ";
  size_t length = strlen(item);
  options = (char *)malloc(length * 10000U + 64U);
  if (options == NULL)
    return 0;
  strcpy(options, "host=127.0.0.1 port=1 connect_timeout=1 ");
  cursor = options + strlen(options);
  for (i = 0; i < 10000; ++i) {
    memcpy(cursor, item, length);
    cursor += length;
  }
  *cursor = '\0';
  parsed = cpkt_postgres_connection_parse(options, &error);
  ok = 0;
  if (parsed != NULL) {
    for (option = parsed; option->keyword != NULL; ++option)
      if (strcmp(option->keyword, "application_name") == 0 &&
          option->value != NULL && strcmp(option->value, "stack-test") == 0)
        ok = 1;
    cpkt_postgres_connection_options_free(parsed);
  }
  cpkt_postgres_text_free(error);
  free(options);
  return ok;
}

int main(int argc, char **argv) {
  cpkt_sqlite *db;
  int ok;
  if (argc != 2)
    return 2;
  if (strcmp(argv[1], "postgres-options") == 0)
    return postgres_options() ? 0 : 1;
  if (strcmp(argv[1], "symlink-path") == 0)
    return symlink_stack(-1, NULL, 40, 0) ? 0 : 1;
  if (strcmp(argv[1], "symlink-budget") == 0)
    return symlink_budget() ? 0 : 1;
  if (strcmp(argv[1], "symlink-oom") == 0)
    return symlink_oom() ? 0 : 1;
  if (strcmp(argv[1], "expression-oom") == 0)
    return expression_oom() ? 0 : 1;
  db = cpkt_sqlite_new(":memory:");
  if (db == NULL)
    return 1;
  if (strcmp(argv[1], "expression") == 0)
    ok = expression_depth(db);
  else if (strncmp(argv[1], "expr-", 5) == 0)
    ok = expression_shape(db, argv[1] + 5);
  else if (strcmp(argv[1], "expression-variants") == 0)
    ok = expression_variants(db);
  else if (strcmp(argv[1], "stack-migration") == 0)
    ok = stack_migration(db);
  else if (strcmp(argv[1], "stack-reentrancy") == 0)
    ok = stack_reentrancy(db);
  else if (strcmp(argv[1], "compound") == 0)
    ok = compound_select(db);
  else if (strcmp(argv[1], "fts") == 0)
    ok = fts_stack(db);
  else if (strcmp(argv[1], "upgraded-api") == 0)
    ok = upgraded_api(db);
  else if (strcmp(argv[1], "json") == 0)
    ok = json_depth(db);
  else if (strcmp(argv[1], "trigger") == 0)
    ok = trigger_depth(db);
  else
    ok = 0;
  cpkt_sqlite_close(db);
  return ok ? 0 : 1;
}
