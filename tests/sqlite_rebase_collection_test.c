#include <cpkt/sqlite.h>

#include <stdio.h>
#include <string.h>

enum { CPKT_REBASE_ROWS = 384, CPKT_REBASE_VALUE_SIZE = 512 };

typedef struct cpkt_rebase_stream {
  const char *data;
  int byte_count;
  int offset;
} cpkt_rebase_stream;

static int cpkt_rebase_input(void *context, void *buffer, int *byte_count) {
  cpkt_rebase_stream *stream;
  int available;
  stream = (cpkt_rebase_stream *)context;
  available = stream->byte_count - stream->offset;
  if (*byte_count > available)
    *byte_count = available;
  if (*byte_count > 0)
    memcpy(buffer, stream->data + stream->offset, (size_t)*byte_count);
  stream->offset += *byte_count;
  return CPKT_SQLITE_OK;
}

static int cpkt_rebase_omit(void *context, int kind,
                            cpkt_sqlite_changeset_iterator *iterator) {
  int *conflicts;
  (void)kind;
  (void)iterator;
  conflicts = (int *)context;
  ++*conflicts;
  return CPKT_SQLITE_CHANGESET_OMIT;
}

static cpkt_sqlite *cpkt_rebase_database(char value_byte) {
  cpkt_sqlite *database;
  char value[CPKT_REBASE_VALUE_SIZE + 1];
  char sql[CPKT_REBASE_VALUE_SIZE + 96];
  int index;
  database = cpkt_sqlite_new(":memory:");
  if (database == NULL)
    return NULL;
  memset(value, value_byte, CPKT_REBASE_VALUE_SIZE);
  value[CPKT_REBASE_VALUE_SIZE] = '\0';
  if (database->tx(database,
                   "create table item(id integer primary key, value text)",
                   NULL, NULL) != CPKT_SQLITE_OK ||
      database->tx(database, "begin", NULL, NULL) != CPKT_SQLITE_OK)
    return NULL;
  for (index = 0; index < CPKT_REBASE_ROWS; ++index) {
    sprintf(sql, "insert into item values(%d,'%s')", index, value);
    if (database->tx(database, sql, NULL, NULL) != CPKT_SQLITE_OK)
      return NULL;
  }
  if (database->tx(database, "commit", NULL, NULL) != CPKT_SQLITE_OK)
    return NULL;
  return database;
}

static int cpkt_rebase_apply(int mode, const cpkt_sqlite_changeset *changeset,
                             int collect, unsigned long *peak_out) {
  cpkt_sqlite *database;
  cpkt_sqlite_changeset *rebase;
  cpkt_rebase_stream stream;
  cpkt_sqlite_i64 baseline;
  cpkt_sqlite_i64 highwater;
  int conflicts;
  int status;
  database = cpkt_rebase_database('d');
  if (database == NULL)
    return 0;
  stream.data = (const char *)changeset->data;
  stream.byte_count = changeset->byte_count;
  stream.offset = 0;
  rebase = NULL;
  conflicts = 0;
  baseline = cpkt_sqlite_memory_used();
  (void)cpkt_sqlite_memory_highwater(1);
  switch (mode) {
  case 0:
    status = cpkt_sqlite_changeset_apply_ex(database, changeset, NULL,
                                            cpkt_rebase_omit, &conflicts, 0,
                                            collect ? &rebase : NULL);
    break;
  case 1:
    status = cpkt_sqlite_changeset_apply_ex_strm(
        database, cpkt_rebase_input, &stream, NULL, cpkt_rebase_omit,
        &conflicts, 0, collect ? &rebase : NULL);
    break;
  case 2:
    status = cpkt_sqlite_changeset_apply_v3(database, changeset, NULL,
                                            cpkt_rebase_omit, &conflicts, 0,
                                            collect ? &rebase : NULL);
    break;
  default:
    status = cpkt_sqlite_changeset_apply_v3_strm(
        database, cpkt_rebase_input, &stream, NULL, cpkt_rebase_omit,
        &conflicts, 0, collect ? &rebase : NULL);
    break;
  }
  highwater = cpkt_sqlite_memory_highwater(0);
  *peak_out = highwater.low - baseline.low;
  if (status != CPKT_SQLITE_OK || conflicts != CPKT_REBASE_ROWS ||
      ((mode == 1 || mode == 3) && stream.offset != stream.byte_count) ||
      (collect && (rebase == NULL || rebase->byte_count <= 0)) ||
      (!collect && rebase != NULL)) {
    database->close(database);
    return 0;
  }
  if (rebase != NULL)
    rebase->free(rebase);
  database->close(database);
  return 1;
}

int main(void) {
  cpkt_sqlite *source;
  cpkt_sqlite_session *session;
  cpkt_sqlite_changeset *changeset;
  unsigned long collected_peak;
  unsigned long discarded_peak;
  int mode;
  source = cpkt_sqlite_new(":memory:");
  if (source == NULL ||
      source->tx(source,
                 "create table item(id integer primary key, value text)", NULL,
                 NULL) != CPKT_SQLITE_OK ||
      cpkt_sqlite_session_new(source, "main", &session) != CPKT_SQLITE_OK ||
      session->attach(session, "item") != CPKT_SQLITE_OK)
    return 1;
  /* The source rows match the destination keys but differ in value. */
  {
    char value[CPKT_REBASE_VALUE_SIZE + 1];
    char sql[CPKT_REBASE_VALUE_SIZE + 96];
    int index;
    memset(value, 's', CPKT_REBASE_VALUE_SIZE);
    value[CPKT_REBASE_VALUE_SIZE] = '\0';
    if (source->tx(source, "begin", NULL, NULL) != CPKT_SQLITE_OK)
      return 2;
    for (index = 0; index < CPKT_REBASE_ROWS; ++index) {
      sprintf(sql, "insert into item values(%d,'%s')", index, value);
      if (source->tx(source, sql, NULL, NULL) != CPKT_SQLITE_OK)
        return 3;
    }
    if (source->tx(source, "commit", NULL, NULL) != CPKT_SQLITE_OK)
      return 4;
  }
  changeset = NULL;
  if (session->changeset(session, &changeset) != CPKT_SQLITE_OK ||
      changeset == NULL || changeset->byte_count <= 0)
    return 5;
  for (mode = 0; mode != 4; ++mode) {
    if (!cpkt_rebase_apply(mode, changeset, 1, &collected_peak) ||
        !cpkt_rebase_apply(mode, changeset, 0, &discarded_peak))
      return 6;
    if (collected_peak <= discarded_peak + 65536UL) {
      fprintf(stderr,
              "mode %d retained rebase memory: collected=%lu discarded=%lu\n",
              mode, collected_peak, discarded_peak);
      return 7;
    }
  }
  changeset->free(changeset);
  session->close(session);
  source->close(source);
  return 0;
}
