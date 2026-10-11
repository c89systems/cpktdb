#include <cpkt/sqlite.h>

#include <stdio.h>

static int omit_conflict(void *context, int kind,
                         cpkt_sqlite_changeset_iterator *iterator) {
  (void)context;
  (void)kind;
  (void)iterator;
  return CPKT_SQLITE_CHANGESET_OMIT;
}

int main(void) {
  cpkt_sqlite *database;
  cpkt_sqlite *replica;
  cpkt_sqlite_session *session;
  cpkt_sqlite_changeset *empty;
  cpkt_sqlite_changeset *nonempty;
  cpkt_sqlite_changeset *output;
  cpkt_sqlite_changeset *rebase_data;
  cpkt_sqlite_rebaser *rebaser;
  int stage;
  database = NULL;
  replica = NULL;
  session = NULL;
  empty = NULL;
  nonempty = NULL;
  output = NULL;
  rebase_data = NULL;
  rebaser = NULL;
  stage = 1;
  database = cpkt_sqlite_new(":memory:");
  if (database == NULL ||
      database->tx(database, "CREATE TABLE item(id INTEGER PRIMARY KEY)", NULL,
                   NULL) != CPKT_SQLITE_OK ||
      cpkt_sqlite_session_new(database, "main", &session) != CPKT_SQLITE_OK ||
      session == NULL || session->attach(session, "item") != CPKT_SQLITE_OK ||
      session->changeset(session, &empty) != CPKT_SQLITE_OK || empty == NULL ||
      empty->data != NULL || empty->byte_count != 0 ||
      cpkt_sqlite_rebaser_new(&rebaser) != CPKT_SQLITE_OK || rebaser == NULL)
    goto fail;
  stage = 2;
  if (rebaser->configure(rebaser, empty->data, empty->byte_count) !=
      CPKT_SQLITE_OK)
    goto fail;
  stage = 3;
  if (rebaser->rebase(rebaser, empty->data, empty->byte_count, &output) !=
          CPKT_SQLITE_OK ||
      output == NULL || output->data != NULL || output->byte_count != 0)
    goto fail;
  output->free(output);
  output = NULL;
  stage = 4;
  if (rebaser->configure(rebaser, NULL, 1) != CPKT_SQLITE_MISUSE)
    goto fail;
  output = (cpkt_sqlite_changeset *)rebaser;
  if (rebaser->rebase(rebaser, NULL, 1, &output) != CPKT_SQLITE_MISUSE ||
      output != NULL)
    goto fail;
  stage = 5;
  if (database->tx(database, "INSERT INTO item VALUES(1)", NULL, NULL) !=
          CPKT_SQLITE_OK ||
      session->changeset(session, &nonempty) != CPKT_SQLITE_OK ||
      nonempty == NULL || nonempty->data == NULL || nonempty->byte_count <= 0 ||
      rebaser->rebase(rebaser, nonempty->data, nonempty->byte_count, &output) !=
          CPKT_SQLITE_OK ||
      output == NULL || output->byte_count <= 0 || output->data == NULL)
    goto fail;
  output->free(output);
  output = NULL;
  stage = 6;
  replica = cpkt_sqlite_new(":memory:");
  if (replica == NULL ||
      replica->tx(replica,
                  "CREATE TABLE item(id INTEGER PRIMARY KEY); "
                  "INSERT INTO item VALUES(1)",
                  NULL, NULL) != CPKT_SQLITE_OK ||
      cpkt_sqlite_changeset_apply_ex(replica, nonempty, NULL, omit_conflict,
                                     NULL, 0, &rebase_data) != CPKT_SQLITE_OK ||
      rebase_data == NULL || rebase_data->data == NULL ||
      rebase_data->byte_count <= 0 ||
      rebaser->configure(rebaser, rebase_data->data, rebase_data->byte_count) !=
          CPKT_SQLITE_OK ||
      rebaser->rebase(rebaser, nonempty->data, nonempty->byte_count, &output) !=
          CPKT_SQLITE_OK ||
      output == NULL)
    goto fail;
  output->free(output);
  rebase_data->free(rebase_data);
  nonempty->free(nonempty);
  empty->free(empty);
  rebaser->close(rebaser);
  session->close(session);
  replica->close(replica);
  database->close(database);
  return 0;
fail:
  fprintf(stderr, "sqlite empty rebaser failed at stage %d\n", stage);
  if (output != NULL && output != (cpkt_sqlite_changeset *)rebaser)
    output->free(output);
  if (nonempty != NULL)
    nonempty->free(nonempty);
  if (rebase_data != NULL)
    rebase_data->free(rebase_data);
  if (empty != NULL)
    empty->free(empty);
  if (rebaser != NULL)
    rebaser->close(rebaser);
  if (session != NULL)
    session->close(session);
  if (replica != NULL)
    replica->close(replica);
  if (database != NULL)
    database->close(database);
  return stage;
}
