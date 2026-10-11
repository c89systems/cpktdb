#!/usr/bin/env bash
set -Eeuo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

case ${GROUP:-db} in db|all) ;; *) printf 'this operation requires GROUP=db|all\n' >&2; exit 2 ;; esac
executable=${1:?pass the built PostgreSQL facade integration executable}
postgres_port=${CPKT_DEV_POSTGRES_PORT:-55432}
cockroach_port=${CPKT_DEV_COCKROACH_PORT:-56257}

cleanup() {
  local status=$? down_status=0
  trap - EXIT
  if (( status != 0 )); then
    printf '[test-e2e] failed; database pod diagnostics follow\n' >&2
    "$repo_root/scripts/devenv.sh" ps >&2 || true
    "$repo_root/scripts/devenv.sh" logs >&2 || true
  fi
  "$repo_root/scripts/devenv.sh" down || down_status=$?
  if (( down_status != 0 )); then
    printf '[test-e2e] failed to stop database pods\n' >&2
    if (( status == 0 )); then
      status=$down_status
    fi
  fi
  exit "$status"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

"$repo_root/scripts/devenv.sh" up
CPKT_POSTGRES_E2E_CONNINFO="host=127.0.0.1 port=$postgres_port user=postgres dbname=postgres sslmode=disable connect_timeout=3" \
CPKT_COCKROACH_E2E_CONNINFO="host=127.0.0.1 port=$cockroach_port user=root dbname=defaultdb sslmode=disable connect_timeout=3" \
  "$repo_root/scripts/e2e-postgres.sh" "$executable"
