#include <cpkt/sqlite.h>

#include <string.h>

typedef struct cpkt_test_input {
  const unsigned char *data;
  int size;
  int offset;
} cpkt_test_input;

static int cpkt_test_stream_input(void *context, void *buffer, int *size) {
  cpkt_test_input *input;
  int remaining;
  input = (cpkt_test_input *)context;
  remaining = input->size - input->offset;
  if (*size > remaining)
    *size = remaining;
  if (*size > 0)
    memcpy(buffer, input->data + input->offset, (size_t)*size);
  input->offset += *size;
  return CPKT_SQLITE_OK;
}

static int cpkt_test_apply(int mode, cpkt_sqlite *database,
                           const cpkt_sqlite_changeset *changeset) {
  cpkt_test_input input;
  cpkt_sqlite_changeset *rebase;
  int status;
  input.data = (const unsigned char *)changeset->data;
  input.size = changeset->byte_count;
  input.offset = 0;
  rebase = NULL;
  switch (mode) {
  case 0:
    status = cpkt_sqlite_changeset_apply(database, changeset, NULL, NULL, NULL);
    break;
  case 1:
    status = cpkt_sqlite_changeset_apply_strm(database, cpkt_test_stream_input,
                                              &input, NULL, NULL, NULL);
    break;
  case 2:
    status = cpkt_sqlite_changeset_apply_ex(database, changeset, NULL, NULL,
                                            NULL, 0, &rebase);
    break;
  case 3:
    status = cpkt_sqlite_changeset_apply_ex_strm(
        database, cpkt_test_stream_input, &input, NULL, NULL, NULL, 0, &rebase);
    break;
  case 4:
    status = cpkt_sqlite_changeset_apply_v3(database, changeset, NULL, NULL,
                                            NULL, 0, &rebase);
    break;
  default:
    status = cpkt_sqlite_changeset_apply_v3_strm(
        database, cpkt_test_stream_input, &input, NULL, NULL, NULL, 0, &rebase);
    break;
  }
  if (rebase != NULL) {
    rebase->free(rebase);
    return 0;
  }
  return status == CPKT_SQLITE_ABORT;
}

int main(void) {
  cpkt_sqlite *source;
  cpkt_sqlite *destination;
  cpkt_sqlite_session *session;
  cpkt_sqlite_changeset *changeset;
  cpkt_sqlite_statement *statement;
  const unsigned char *value;
  int mode;
  source = cpkt_sqlite_new(":memory:");
  if (source == NULL ||
      source->tx(source,
                 "create table item(id integer primary key, value text)", NULL,
                 NULL) != CPKT_SQLITE_OK)
    return 1;
  session = NULL;
  changeset = NULL;
  if (cpkt_sqlite_session_new(source, "main", &session) != CPKT_SQLITE_OK ||
      session == NULL || session->attach(session, "item") != CPKT_SQLITE_OK ||
      source->tx(source, "insert into item values(1,'source')", NULL, NULL) !=
          CPKT_SQLITE_OK ||
      session->changeset(session, &changeset) != CPKT_SQLITE_OK ||
      changeset == NULL || changeset->byte_count <= 0)
    return 2;
  for (mode = 0; mode < 6; ++mode) {
    destination = cpkt_sqlite_new(":memory:");
    if (destination == NULL ||
        destination->tx(destination,
                        "create table item(id integer primary key, value text)",
                        NULL, NULL) != CPKT_SQLITE_OK ||
        destination->tx(destination, "insert into item values(1,'existing')",
                        NULL, NULL) != CPKT_SQLITE_OK)
      return 3;
    if (!cpkt_test_apply(mode, destination, changeset))
      return 4 + mode;
    statement = NULL;
    if (destination->prepare(destination, "select value from item where id=1",
                             -1, 0, &statement, NULL) != CPKT_SQLITE_OK ||
        statement == NULL || statement->step(statement) != CPKT_SQLITE_ROW)
      return 10 + mode;
    value = statement->column_text(statement, 0);
    if (value == NULL || strcmp((const char *)value, "existing") != 0 ||
        statement->finalize(statement) != CPKT_SQLITE_OK)
      return 16 + mode;
    destination->close(destination);
  }
  changeset->free(changeset);
  session->close(session);
  source->close(source);
  return 0;
}
