#!/usr/bin/env bash
set -Eeuo pipefail

source "$(dirname -- "${BASH_SOURCE[0]}")/lifecycle-common.sh"
repo_root=$cpkt_root

case ${GROUP:-db} in db|all) ;; *) printf 'this operation requires GROUP=db|all\n' >&2; exit 2 ;; esac
state_root="$repo_root/build/devenv"
manifest="$state_root/devenv.yaml"
for path in "$manifest" "$state_root/state/postgres" "$state_root/state/cockroach" \
    "$state_root/tmp" "$state_root/logs"; do
  cpkt_owned_path "$path"
done
checkout_id=$(printf '%s' "$repo_root" | sha256sum | cut -c1-10)
postgres_pod="cpkt-pg-$checkout_id"
cockroach_pod="cpkt-crdb-$checkout_id"
postgres_port=${CPKT_DEV_POSTGRES_PORT:-55432}
cockroach_port=${CPKT_DEV_COCKROACH_PORT:-56257}

usage() {
  printf 'usage: %s {render|up|down|reset|ps|logs|is-up}\n' "$0" >&2
  exit 2
}

require_podman() {
  command -v podman >/dev/null || { printf 'rootless Podman is required\n' >&2; exit 1; }
  [[ $(podman info --format '{{.Host.Security.Rootless}}') == true ]] || {
    printf 'Podman must run rootlessly as the invoking user\n' >&2
    exit 1
  }
}

render_manifest() {
  local mode=$1 content existing key value
  IFS= read -r -d '' content < "$repo_root/devenv.yaml.in" || true
  for key in CPKT_POSTGRES_POD CPKT_COCKROACH_POD CPKT_POSTGRES_PORT CPKT_COCKROACH_PORT CPKT_POSTGRES_STATE CPKT_COCKROACH_STATE; do
    case "$key" in
      CPKT_POSTGRES_POD) value=$postgres_pod ;;
      CPKT_COCKROACH_POD) value=$cockroach_pod ;;
      CPKT_POSTGRES_PORT) value=$postgres_port ;;
      CPKT_COCKROACH_PORT) value=$cockroach_port ;;
      CPKT_POSTGRES_STATE) value=$state_root/state/postgres ;;
      CPKT_COCKROACH_STATE) value=$state_root/state/cockroach ;;
    esac
    content=${content//"$key"/"$value"}
  done
  if [ "$mode" = matches ]; then
    [ -f "$manifest" ] || return 1
    IFS= read -r -d '' existing < "$manifest" || true
    [ "$existing" = "$content" ]
  else
    (umask 077; printf '%s' "$content" > "$manifest")
    chmod 600 "$manifest"
  fi
}

render() {
  local port
  for port in "$postgres_port" "$cockroach_port"; do
    [[ $port =~ ^[0-9]+$ ]] && (( port >= 1024 && port <= 65535 )) || {
      printf 'local database ports must be numeric and between 1024 and 65535\n' >&2
      exit 2
    }
  done
  [[ $postgres_port != "$cockroach_port" ]] || { printf 'database ports must differ\n' >&2; exit 2; }
  mkdir -p "$state_root/state/postgres" "$state_root/state/cockroach" "$state_root/tmp" "$state_root/logs"
  render_manifest write
  printf '%s\n' "$manifest"
}

readiness() {
  local label=$1 container=$2
  shift 2
  for (( attempt=0; attempt<60; attempt++ )); do
    if podman exec "$container" "$@" >/dev/null 2>&1; then
      printf '[devenv] %s ready\n' "$label"
      return 0
    fi
    sleep 1
  done
  printf '[devenv] %s did not become ready; pod logs follow\n' "$label" >&2
  podman logs "$container" >&2 || true
  return 1
}

is_up() {
  render_manifest matches &&
      podman pod exists "$postgres_pod" && podman pod exists "$cockroach_pod" &&
      [[ $(podman pod inspect "$postgres_pod" --format '{{.State}}') == Running ]] &&
      [[ $(podman pod inspect "$cockroach_pod" --format '{{.State}}') == Running ]]
}

up() {
  require_podman
  if is_up; then
    printf '[devenv] existing pods %s and %s\n' "$postgres_pod" "$cockroach_pod"
  else
    down
    render
    podman kube play --network=pasta --userns=keep-id:uid=70,gid=70 "$manifest"
  fi
  readiness postgresql "$postgres_pod-postgres" pg_isready -h 127.0.0.1 -U postgres
  readiness cockroachdb "$cockroach_pod-cockroach" /cockroach/cockroach sql --insecure --host=127.0.0.1:26257 --execute='SELECT 1'
  printf '[devenv] PostgreSQL 127.0.0.1:%s; CockroachDB 127.0.0.1:%s; state %s\n' \
    "$postgres_port" "$cockroach_port" "$state_root"
}

down() {
  local pod status=0
  require_podman
  if [[ -f $manifest ]]; then
    podman kube down "$manifest" || status=$?
  fi
  for pod in "$postgres_pod" "$cockroach_pod"; do
    if podman pod exists "$pod"; then
      podman pod rm --force "$pod" || status=$?
    fi
  done
  if podman pod exists "$postgres_pod" || podman pod exists "$cockroach_pod"; then
    printf '[devenv] database pods remain after shutdown\n' >&2
    return 1
  fi
  return "$status"
}

case ${1:-} in
  render) render ;;
  up) up ;;
  down) down ;;
  reset) down; rm -rf -- "$state_root" ;;
  ps) require_podman; podman pod ps --filter "name=cpkt-.*-$checkout_id" ;;
  logs) require_podman; podman logs "$postgres_pod-postgres"; podman logs "$cockroach_pod-cockroach" ;;
  is-up) require_podman; is_up ;;
  *) usage ;;
esac
