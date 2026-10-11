#include <cpkt/sqlite.h>

typedef struct notify_state {
  int calls;
  int count;
  int contexts_match;
} notify_state;

static notify_state first;
static notify_state second;
static notify_state third;

static void grouped_callback(void *context, int count, void *const *contexts) {
  notify_state *state;
  state = (notify_state *)context;
  ++state->calls;
  state->count = count;
  state->contexts_match = count == 2 && contexts != NULL &&
                          ((contexts[0] == &first && contexts[1] == &third) ||
                           (contexts[0] == &third && contexts[1] == &first));
}

static void separate_callback(void *context, int count, void *const *contexts) {
  notify_state *state;
  state = (notify_state *)context;
  ++state->calls;
  state->count = count;
  state->contexts_match =
      count == 1 && contexts != NULL && contexts[0] == &second;
}

int main(void) {
  cpkt_sqlite *blocker;
  cpkt_sqlite *blocked_first;
  cpkt_sqlite *blocked_second;
  cpkt_sqlite *blocked_third;
  int flags;
  flags = CPKT_SQLITE_OPEN_READWRITE | CPKT_SQLITE_OPEN_CREATE |
          CPKT_SQLITE_OPEN_URI;
  blocker = cpkt_sqlite_open("file:cpkt-notify-groups?mode=memory&cache=shared",
                             flags, NULL);
  blocked_first = cpkt_sqlite_open(
      "file:cpkt-notify-groups?mode=memory&cache=shared", flags, NULL);
  blocked_second = cpkt_sqlite_open(
      "file:cpkt-notify-groups?mode=memory&cache=shared", flags, NULL);
  blocked_third = cpkt_sqlite_open(
      "file:cpkt-notify-groups?mode=memory&cache=shared", flags, NULL);
  if (blocker == NULL || blocked_first == NULL || blocked_second == NULL ||
      blocked_third == NULL)
    return 1;
  if (blocker->tx(blocker, "begin immediate; create table item(value);", NULL,
                  NULL) != CPKT_SQLITE_OK ||
      blocked_first->tx(blocked_first, "begin immediate", NULL, NULL) !=
          CPKT_SQLITE_LOCKED ||
      blocked_second->tx(blocked_second, "begin immediate", NULL, NULL) !=
          CPKT_SQLITE_LOCKED ||
      blocked_third->tx(blocked_third, "begin immediate", NULL, NULL) !=
          CPKT_SQLITE_LOCKED)
    return 2;
  if (blocked_first->unlock_notify(blocked_first, grouped_callback, &first) !=
          CPKT_SQLITE_OK ||
      blocked_second->unlock_notify(blocked_second, separate_callback,
                                    &second) != CPKT_SQLITE_OK ||
      blocked_third->unlock_notify(blocked_third, grouped_callback, &third) !=
          CPKT_SQLITE_OK ||
      blocker->tx(blocker, "commit", NULL, NULL) != CPKT_SQLITE_OK)
    return 3;
  if (first.calls != 1 || first.count != 2 || !first.contexts_match ||
      second.calls != 1 || second.count != 1 || !second.contexts_match ||
      third.calls != 1 || third.count != 2 || !third.contexts_match)
    return 4;
  blocked_first->close(blocked_first);
  blocked_second->close(blocked_second);
  blocked_third->close(blocked_third);
  blocker->close(blocker);
  return 0;
}
