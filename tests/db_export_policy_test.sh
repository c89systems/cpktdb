#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 6 ]]; then
  printf 'usage: %s provider shared allowlist elf|macho nm staged-root\n' "$0" >&2
  exit 2
fi
provider=$1
library=$2
allowlist=$3
format=$4
symbol_tool=$5
staged_root=$6

case "$format" in
  macho)
    actual=$("$symbol_tool" -gU "$library" | awk '{print $NF}' |
      sed 's/^_//' | sort -u)
    ;;
  elf)
    actual=$("$symbol_tool" -D --defined-only "$library" |
      awk '{print $NF}' | sed '/^_init$/d; /^_fini$/d' | sort -u)
    ;;
  *) exit 2 ;;
esac
expected=$(awk '/^cpkt_/ {print $1}' "$allowlist" | sort -u)
if [[ "$actual" != "$expected" ]]; then
  printf '%s shared export catalog mismatch\n' "$provider" >&2
  diff -u <(printf '%s\n' "$expected") <(printf '%s\n' "$actual") >&2 || true
  exit 1
fi
if [[ "$format" == elf ]]; then
  imports=$("$symbol_tool" -D --undefined-only "$library" |
    awk '{print $NF}' | sed 's/@.*$//' | sort -u)
  if printf '%s\n' "$imports" | rg -q '^(cpkt_|pslog_)'; then
    printf '%s shared facade imports private SDK symbol\n' "$provider" >&2
    exit 1
  fi
  if [[ "$provider" == postgres ]]; then
    dependency=postgresql
    native_library=libpq.so
    pattern='^(PQ|lo_|pg_)'
  else
    dependency=sqlite
    native_library=libsqlite3.so
    pattern='^(sqlite3|fts5)'
  fi
  provider_imports=$(printf '%s\n' "$imports" | rg "$pattern" || true)
  native_path="$staged_root/lib/$native_library"
  if [[ ! -f $native_path ]]; then native_path="$staged_root/$dependency/install/lib/$native_library"; fi
  definitions=$("$symbol_tool" -D --defined-only \
    "$native_path" |
    awk '{print $NF}' | sed 's/@.*$//' | sort -u)
  if [[ -n "$provider_imports" ]]; then
    unresolved=$(comm -23 <(printf '%s\n' "$provider_imports" | sort -u) \
      <(printf '%s\n' "$definitions" | sort -u))
    if [[ -n "$unresolved" ]]; then
      printf '%s unresolved provider imports: %s\n' "$provider" "$unresolved" >&2
      exit 1
    fi
  fi
fi
printf '%s exact exports and provider imports verified\n' "$provider"
