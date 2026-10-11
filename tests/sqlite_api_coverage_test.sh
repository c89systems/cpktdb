#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 5 ]]; then
  printf 'usage: %s <facade-source> <sqlite3.h> <sqlite3session.h> <sqlite3rtree.h> <fts5.h>\n' "$0" >&2
  exit 2
fi

facade_source=$1
shift

native_apis=$(awk '
function emit() {
  if (decl ~ /SQLITE_API/ && match(decl, /sqlite3(_[A-Za-z0-9_]*|session[A-Za-z0-9_]*|changeset[A-Za-z0-9_]*|changegroup[A-Za-z0-9_]*|rebaser[A-Za-z0-9_]*)[[:space:]]*\(/)) {
    name = substr(decl, RSTART, RLENGTH)
    sub(/[[:space:]]*\(.*/, "", name)
    print name
  }
  decl = ""
}
/SQLITE_API/ {
  decl = $0
  if ($0 ~ /;/) emit()
  next
}
decl != "" {
  decl = decl " " $0
  if ($0 ~ /;/) emit()
}
' "$@" | sort -u)

missing=''
while IFS= read -r api; do
  [[ -n "$api" ]] || continue
  case "$api" in
    sqlite3_activate_cerod|sqlite3_mutex_notheld|sqlite3_test_control|sqlite3_win32_set_directory|sqlite3_win32_set_directory8|sqlite3_win32_set_directory16)
      # Disabled profile or explicitly unstable, test-only APIs.
      continue
      ;;
    sqlite3_context_db_handle) needle='cpkt_sqlite_context_database' ;;
    sqlite3_create_collation) needle='sqlite3_create_collation_v2' ;;
    sqlite3_create_function) needle='sqlite3_create_function_v2' ;;
    sqlite3_create_function16) needle='sqlite3__cpkt_create_function16_v2' ;;
    sqlite3_create_module) needle='sqlite3_create_module_v2' ;;
    sqlite3_db_handle) needle='cpkt_sqlite_statement_database' ;;
    sqlite3_open) needle='sqlite3_open_v2' ;;
    sqlite3_log) needle='sqlite3__cpkt_log_v' ;;
    sqlite3_prepare|sqlite3_prepare16|sqlite3_prepare16_v2) needle='sqlite3_prepare16_v3' ;;
    sqlite3_snprintf) needle='sqlite3_vsnprintf' ;;
    sqlite3_soft_heap_limit) needle='sqlite3_soft_heap_limit64' ;;
    sqlite3_stmt_scanstatus) needle='sqlite3_stmt_scanstatus_v2' ;;
    sqlite3_str_appendf) needle='sqlite3_str_vappendf' ;;
    sqlite3_wal_checkpoint) needle='sqlite3_wal_checkpoint_v2' ;;
    sqlite3changeset_start) needle='sqlite3changeset_start_v2' ;;
    sqlite3changeset_start_strm) needle='sqlite3changeset_start_v2_strm' ;;
    *) needle=$api ;;
  esac
  if ! grep -Eq "${needle}[[:space:]]*\\(" "$facade_source"; then
    missing+="${api} (expected facade using ${needle})"$'\n'
  fi
done <<< "$native_apis"

if [[ -n "$missing" ]]; then
  printf 'SQLite C89 facade lacks published enabled APIs:\n%s' "$missing" >&2
  exit 1
fi
