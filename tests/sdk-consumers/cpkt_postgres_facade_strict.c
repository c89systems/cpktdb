#include <cpkt/postgres.h>

int main(void) {
  cpkt_postgres *pg;

  if (cpkt_postgres_library_version() <= 0) {
    return 1;
  }
  pg = cpkt_postgres_new("host=/tmp/cpkt-postgres-no-socket connect_timeout=1");
  if (pg == 0 || pg->tx == 0 || pg->send == 0 || pg->receive == 0 ||
      pg->reset == 0 || pg->close == 0) {
    return 2;
  }
  if (pg->status(pg) != CPKT_POSTGRES_CONNECTION_BAD) {
    pg->close(pg);
    return 3;
  }
  pg->close(pg);
  return 0;
}
