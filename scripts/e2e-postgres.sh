#!/usr/bin/env bash
set -euo pipefail
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

case ${GROUP:-db} in db|all) ;; *) printf 'this operation requires GROUP=db|all\n' >&2; exit 2 ;; esac

if [[ $# -ne 1 ]]; then
  printf 'usage: %s POSTGRES-INTEGRATION-EXECUTABLE\n' "$0" >&2
  exit 2
fi

: "${CPKT_POSTGRES_E2E_CONNINFO:?set a PostgreSQL libpq connection string}"
: "${CPKT_COCKROACH_E2E_CONNINFO:?set a CockroachDB libpq connection string}"

executable=$1
runner=("$executable")
if [[ ${CPKT_POSTGRES_E2E_MEMCHECK:-0} == 1 ]]; then
  runner=(valgrind --error-exitcode=1 --leak-check=full --track-origins=yes
    --show-leak-kinds=definite,indirect
    "--suppressions=$repo_root/tests/valgrind.supp" "$executable")
fi
timeout --signal=TERM 300s "${runner[@]}" postgresql
timeout --signal=TERM 300s "${runner[@]}" cockroachdb
printf '[e2e-postgres] PostgreSQL and CockroachDB passed\n'
