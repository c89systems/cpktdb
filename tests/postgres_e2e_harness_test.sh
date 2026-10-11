#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
set +e
output=$(env -u CPKT_POSTGRES_E2E_CONNINFO -u CPKT_COCKROACH_E2E_CONNINFO \
  bash "$repo_root/scripts/e2e-postgres.sh" /bin/true 2>&1)
status=$?
set -e

if [[ $status -eq 0 ]]; then
  printf 'PostgreSQL e2e harness accepted missing connection strings\n' >&2
  exit 1
fi
if [[ $output != *CPKT_POSTGRES_E2E_CONNINFO* ]]; then
  printf 'PostgreSQL e2e harness did not identify the missing PostgreSQL input\n' >&2
  exit 1
fi

mkdir -p "$repo_root/build"
work_root=$(mktemp -d "$repo_root/build/postgres-e2e-harness.XXXXXX")
trap 'cmake -E remove_directory "$work_root"' EXIT

cat > "$work_root/integration-probe" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
if [[ $# -ne 1 ]]; then
  printf 'database connection string reached integration argv\n' >&2
  exit 1
fi
case $1 in
  postgresql) [[ ${CPKT_POSTGRES_E2E_CONNINFO:-} == *"password=$CPKT_TEST_SECRET"* ]] ;;
  cockroachdb) [[ ${CPKT_COCKROACH_E2E_CONNINFO:-} == *"password=$CPKT_TEST_SECRET"* ]] ;;
  *) exit 1 ;;
esac
printf '%s\n' "$1" >> "$CPKT_TEST_CALLS"
EOF
cat > "$work_root/valgrind" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
while [[ $# -gt 0 && $1 == --* ]]; do
  shift
done
"$@"
EOF
chmod +x "$work_root/integration-probe" "$work_root/valgrind"

test_secret='cpkt-e2e-argv-regression'
for memcheck in 0 1; do
  output=$(PATH="$work_root:$PATH" \
    CPKT_TEST_SECRET="$test_secret" CPKT_TEST_CALLS="$work_root/calls" \
    CPKT_POSTGRES_E2E_MEMCHECK="$memcheck" \
    CPKT_POSTGRES_E2E_CONNINFO="host=postgres password=$test_secret" \
    CPKT_COCKROACH_E2E_CONNINFO="host=cockroach password=$test_secret" \
    bash "$repo_root/scripts/e2e-postgres.sh" "$work_root/integration-probe") || {
      printf 'PostgreSQL e2e harness failed its argument privacy probe\n' >&2
      exit 1
    }
  if [[ $output == *"$test_secret"* ]]; then
    printf 'PostgreSQL e2e harness printed a connection secret\n' >&2
    exit 1
  fi
done
if [[ $(cat "$work_root/calls") != $'postgresql\ncockroachdb\npostgresql\ncockroachdb' ]]; then
  printf 'PostgreSQL e2e harness did not run both servers in both modes\n' >&2
  exit 1
fi
