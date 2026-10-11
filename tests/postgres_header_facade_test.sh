#!/usr/bin/env bash
set -euo pipefail

repo_root=${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}
header="$repo_root/include/cpkt/postgres.h"
include_dir="$repo_root/include"
cc=${CC:-cc}
work_root=$(mktemp -d "${TMPDIR:-/tmp}/cpkt-postgres-header.XXXXXX")
trap 'rm -rf "$work_root"' EXIT

for forbidden in \
  'libpq' \
  'postgres_ext' \
  'PGconn' \
  'PGresult' \
  'PGcancel' \
  'stdint.h' \
  'stdbool.h' \
  'uint64_t' \
  'int64_t' \
  'long long' \
  'extern "C"' \
  'inline'
do
  if grep -F -- "$forbidden" "$header" >/dev/null 2>&1; then
    printf 'PostgreSQL C89 facade header contains forbidden token: %s\n' "$forbidden" >&2
    exit 1
  fi
done

cat > "$work_root/postgres_header_c89.c" <<'EOF'
#include <cpkt/postgres.h>

int main(void) {
  cpkt_postgres *pg;
  cpkt_postgres_result *result;

  pg = cpkt_postgres_new("host=example.invalid connect_timeout=1");
  if (pg != 0) {
    result = pg->tx(pg, "select 1");
    if (result != 0) {
      cpkt_postgres_result_free(result);
    }
    pg->close(pg);
  }
  return 0;
}
EOF

"$cc" -std=c89 -Wall -Wextra -Wpedantic -Werror -I "$include_dir" \
  -c "$work_root/postgres_header_c89.c" -o "$work_root/postgres_header_c89.o"
