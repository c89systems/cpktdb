#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  printf 'usage: %s <facade-source> <libpq-include-directories>\n' "$0" >&2
  exit 2
fi

facade_source=$1
native_header=''
IFS=';' read -r -a native_include_directories <<< "$2"
for native_include_directory in "${native_include_directories[@]}"; do
  candidate_header="$native_include_directory/libpq-fe.h"
  if [[ -f "$candidate_header" ]]; then
    native_header=$candidate_header
    break
  fi
done

if [[ -z "$native_header" ]]; then
  printf 'libpq-fe.h was not found in the declared libpq include directories\n' >&2
  exit 2
fi

native_apis=$(awk '
function emit() {
  if (match(decl, /PQ[A-Za-z0-9_]+[[:space:]]*\(/)) {
    name = substr(decl, RSTART, RLENGTH)
    sub(/[[:space:]]*\(.*/, "", name)
    print name
  }
  decl = ""
}
/^[[:space:]]*extern[[:space:]]/ {
  decl = $0
  if ($0 ~ /;/) emit()
  next
}
decl != "" {
  decl = decl " " $0
  if ($0 ~ /;/) emit()
}
' "$native_header" | sort -u)

missing=''
while IFS= read -r api; do
  [[ -n "$api" ]] || continue
  if ! grep -Fq "${api}(" "$facade_source" && ! grep -Fq "${api})" "$facade_source"; then
    missing+="${api}"$'\n'
  fi
done <<< "$native_apis"

if [[ -n "$missing" ]]; then
  printf 'PostgreSQL C89 facade lacks adapters for published libpq APIs:\n%s' "$missing" >&2
  exit 1
fi
