#!/usr/bin/env bash
set -euo pipefail

repo_root=${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}
header="$repo_root/include/cpkt/sqlite.h"
include_dir="$repo_root/include"
cc=${CC:-cc}
work_root=$(mktemp -d "${TMPDIR:-/tmp}/cpkt-sqlite-header.XXXXXX")
trap 'rm -rf "$work_root"' EXIT

for forbidden in 'sqlite3' 'stdint.h' 'stdbool.h' 'uint32_t' 'int32_t' 'long long' 'extern "C"' 'inline'; do
  if rg -F -- "$forbidden" "$header" >/dev/null 2>&1; then
    printf 'SQLite C89 facade header contains forbidden token: %s\n' "$forbidden" >&2
    exit 1
  fi
done

cat > "$work_root/sqlite_header_c89.c" <<'EOF'
#include <cpkt/sqlite.h>

int main(void) {
  cpkt_sqlite *db;
  db = cpkt_sqlite_new(":memory:");
  if (db != 0) db->close(db);
  return 0;
}
EOF

"$cc" -std=c89 -Wall -Wextra -Wpedantic -Werror -I "$include_dir" \
  -c "$work_root/sqlite_header_c89.c" -o "$work_root/sqlite_header_c89.o"
