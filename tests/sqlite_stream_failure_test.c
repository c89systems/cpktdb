#include <cpkt/sqlite.h>

#include <stdio.h>
#include <string.h>

typedef struct stream_case {
  const unsigned char *data;
  int length;
  int offset;
  int calls;
  int fail_after;
  int bytes_seen;
} stream_case;

static int tiny_input(void *context, void *buffer, int *byte_count) {
  stream_case *stream;
  int remaining;
  stream = (stream_case *)context;
  ++stream->calls;
  if (stream->fail_after > 0 && stream->calls >= stream->fail_after)
    return CPKT_SQLITE_IOERR;
  remaining = stream->length - stream->offset;
  if (*byte_count > 3)
    *byte_count = 3;
  if (*byte_count > remaining)
    *byte_count = remaining;
  if (*byte_count > 0)
    memcpy(buffer, stream->data + stream->offset, (size_t)*byte_count);
  stream->offset += *byte_count;
  return CPKT_SQLITE_OK;
}

static int failing_output(void *context, const void *buffer, int byte_count) {
  stream_case *stream;
  stream = (stream_case *)context;
  if (buffer == NULL || byte_count < 0)
    return CPKT_SQLITE_MISUSE;
  ++stream->calls;
  if (stream->calls >= stream->fail_after)
    return CPKT_SQLITE_IOERR;
  stream->bytes_seen += byte_count;
  return CPKT_SQLITE_OK;
}

int main(int argc, char **argv) {
  cpkt_sqlite *source;
  cpkt_sqlite *replica;
  cpkt_sqlite_session *session;
  cpkt_sqlite_changeset *changeset;
  cpkt_sqlite_statement *statement;
  stream_case stream;
  char text[513];
  char sql[600];
  int index;
  int stage;
  if (argc != 3)
    return 2;
  (void)remove(argv[1]);
  (void)remove(argv[2]);
  source = NULL;
  replica = NULL;
  session = NULL;
  changeset = NULL;
  statement = NULL;
  stage = 1;
  source = cpkt_sqlite_new(argv[1]);
  replica = cpkt_sqlite_new(argv[2]);
  if (source == NULL || replica == NULL ||
      source->tx(source,
                 "CREATE TABLE item(id INTEGER PRIMARY KEY, value TEXT)", NULL,
                 NULL) != CPKT_SQLITE_OK ||
      replica->tx(replica,
                  "CREATE TABLE item(id INTEGER PRIMARY KEY, value TEXT)", NULL,
                  NULL) != CPKT_SQLITE_OK ||
      cpkt_sqlite_session_new(source, "main", &session) != CPKT_SQLITE_OK ||
      session == NULL || session->attach(session, "item") != CPKT_SQLITE_OK)
    goto fail;
  memset(text, 'z', 512);
  text[512] = '\0';
  if (source->tx(source, "BEGIN", NULL, NULL) != CPKT_SQLITE_OK)
    goto fail;
  for (index = 0; index < 256; ++index) {
    sprintf(sql, "INSERT INTO item VALUES(%d,'%s')", index, text);
    if (source->tx(source, sql, NULL, NULL) != CPKT_SQLITE_OK)
      goto fail;
  }
  if (source->tx(source, "COMMIT", NULL, NULL) != CPKT_SQLITE_OK)
    goto fail;
  stage = 2;
  memset(&stream, 0, sizeof(stream));
  stream.fail_after = 2;
  if (session->changeset_strm(session, failing_output, &stream) !=
          CPKT_SQLITE_IOERR ||
      stream.calls < 2 || stream.bytes_seen == 0 ||
      session->changeset(session, &changeset) != CPKT_SQLITE_OK ||
      changeset == NULL || changeset->byte_count < 65536)
    goto fail;
  stage = 3;
  memset(&stream, 0, sizeof(stream));
  stream.data = (const unsigned char *)changeset->data;
  stream.length = changeset->byte_count;
  stream.fail_after = 5;
  if (cpkt_sqlite_changeset_apply_strm(replica, tiny_input, &stream, NULL, NULL,
                                       NULL) != CPKT_SQLITE_IOERR ||
      stream.calls < 5 || stream.offset <= 0)
    goto fail;
  stage = 4;
  stream.offset = stream.calls = stream.fail_after = 0;
  if (cpkt_sqlite_changeset_apply_strm(replica, tiny_input, &stream, NULL, NULL,
                                       NULL) != CPKT_SQLITE_OK ||
      stream.offset != stream.length || stream.calls <= 1 ||
      replica->prepare(replica, "SELECT count(*) FROM item", -1, 0, &statement,
                       NULL) != CPKT_SQLITE_OK ||
      statement == NULL || statement->step(statement) != CPKT_SQLITE_ROW ||
      statement->column_int(statement, 0) != 256)
    goto fail;
  (void)statement->finalize(statement);
  changeset->free(changeset);
  session->close(session);
  replica->close(replica);
  source->close(source);
  (void)remove(argv[1]);
  (void)remove(argv[2]);
  return 0;
fail:
  fprintf(stderr, "sqlite stream failure regression failed at stage %d\n",
          stage);
  if (statement != NULL)
    (void)statement->finalize(statement);
  if (changeset != NULL)
    changeset->free(changeset);
  if (session != NULL)
    session->close(session);
  if (replica != NULL)
    replica->close(replica);
  if (source != NULL)
    source->close(source);
  (void)remove(argv[1]);
  (void)remove(argv[2]);
  return stage;
}
