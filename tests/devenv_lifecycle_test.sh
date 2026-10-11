#!/usr/bin/env bash
set -euo pipefail

repo_root=${1:?repository root is required}
fixture=$(mktemp -d "$repo_root/build/devenv-lifecycle.XXXXXX")
trap 'cmake -E remove_directory "$fixture"' EXIT
mkdir -p "$fixture/scripts" "$fixture/bin" "$fixture/pods"
cp -a "$repo_root/scripts" "$fixture/"
cp -a "$repo_root/cmake" "$fixture/"
cp "$repo_root/CMakePresets.json" "$fixture/"
cp "$repo_root/devenv.yaml.in" "$fixture/devenv.yaml.in"

cat > "$fixture/bin/podman" <<'SH'
#!/usr/bin/env bash
set -euo pipefail
printf '%s\n' "$*" >> "$CPKT_MOCK_PODMAN_LOG"
case "$1 $2" in
  'info --format') printf 'true\n' ;;
  'pod exists') test -f "$CPKT_MOCK_PODS/$3" ;;
  'pod inspect') printf 'Running\n' ;;
  'pod rm')
    [[ ${CPKT_MOCK_POD_RM_FAIL:-} != 1 ]] || exit 1
    cmake -E rm -f "$CPKT_MOCK_PODS/${@: -1}"
    ;;
  'kube play')
    while read -r pod; do
      touch "$CPKT_MOCK_PODS/$pod"
    done < <(awk '/^  name: cpkt-/{print $2}' "${@: -1}")
    ;;
  'kube down')
    [[ ${CPKT_MOCK_POD_RM_FAIL:-} != 1 ]] || exit 1
    had_pods=0
    while read -r pod; do
      if [[ -f $CPKT_MOCK_PODS/$pod ]]; then had_pods=1; fi
      cmake -E rm -f "$CPKT_MOCK_PODS/$pod"
    done < <(awk '/^  name: cpkt-/{print $2}' "${@: -1}")
    if [[ ${CPKT_MOCK_KUBE_DOWN_FAIL:-} == 1 && $had_pods == 1 ]]; then exit 1; fi
    ;;
  'exec '*) ;;
  *) printf 'unexpected mock Podman command: %s\n' "$*" >&2; exit 1 ;;
esac
SH
chmod +x "$fixture/bin/podman"
export PATH="$fixture/bin:$PATH"
export CPKT_MOCK_PODS="$fixture/pods"
export CPKT_MOCK_PODMAN_LOG="$fixture/podman.log"
unset CPKT_DEV_POSTGRES_PORT CPKT_DEV_COCKROACH_PORT
devenv="$fixture/scripts/devenv.sh"
manifest="$fixture/build/devenv/devenv.yaml"

# Every manifest/state redirect must fail without changing bytes, permissions,
# or starting Podman. Include dangling links and the common state ancestor.
for redirected in devenv devenv/devenv.yaml devenv/state devenv/state/postgres \
    devenv/state/cockroach devenv/tmp devenv/logs; do
  for dangling in no yes; do
    other="$fixture/sentinel"
    mkdir -p "$other"
    printf 'unrelated bytes\n' > "$other/devenv.yaml"
    chmod 640 "$other/devenv.yaml"
    target="$fixture/build/$redirected"
    mkdir -p "${target%/*}"
    if [[ $redirected == devenv/devenv.yaml ]]; then other="$other/devenv.yaml"; fi
    if [[ $dangling == yes ]]; then other="$fixture/absent"; fi
    ln -s "$other" "$target"
    for action in render up reset; do
      if bash "$devenv" "$action" >"$fixture/refusal.log" 2>&1; then
        printf 'devenv accepted redirected %s (%s)\n' "$redirected" "$action" >&2
        exit 14
      fi
      grep -q 'symlink ancestor' "$fixture/refusal.log"
      [[ ! -e $CPKT_MOCK_PODMAN_LOG && ! -e $fixture/absent ]]
      [[ $(cat "$fixture/sentinel/devenv.yaml") == 'unrelated bytes' ]]
      [[ $(stat -c %a "$fixture/sentinel/devenv.yaml") == 640 ]]
      [[ $(find "$fixture/sentinel" -mindepth 1 | wc -l) == 1 ]]
    done
    rm -- "$target"
    rm -rf -- "$fixture/build/devenv" "$fixture/sentinel"
  done
done

bash "$devenv" render >/dev/null
[[ $(stat -c %a "$manifest") == 600 ]]
bash "$devenv" up >/dev/null
bash "$devenv" is-up
if ! grep -q '^kube play --network=pasta ' "$CPKT_MOCK_PODMAN_LOG"; then
  printf 'devenv did not select per-pod rootless networking\n' >&2
  exit 11
fi
if CPKT_DEV_POSTGRES_PORT=5543 bash "$devenv" is-up; then
  printf 'devenv accepted a partial port match\n' >&2
  exit 1
fi
if CPKT_DEV_POSTGRES_PORT=56257 CPKT_DEV_COCKROACH_PORT=55432 \
    bash "$devenv" is-up; then
  printf 'devenv accepted swapped service ports\n' >&2
  exit 2
fi
CPKT_DEV_POSTGRES_PORT=56257 CPKT_DEV_COCKROACH_PORT=55432 \
  bash "$devenv" up >/dev/null
awk 'BEGIN { pod=1 } /^---/ { pod++ } /hostPort: 56257/ { if(pod!=1) exit 1; first++ } /hostPort: 55432/ { if(pod!=2) exit 1; second++ } END { if(first!=1 || second!=1) exit 1 }' "$manifest"
if [[ $(grep -c '^kube play ' "$CPKT_MOCK_PODMAN_LOG") != 2 ]]; then
  printf 'devenv did not replace pods after changing ports\n' >&2
  exit 3
fi

cmake -E rm -f "$manifest"
bash "$devenv" down
if find "$CPKT_MOCK_PODS" -type f | grep -q .; then
  printf 'devenv left pods running after the manifest was removed\n' >&2
  exit 4
fi
if [[ $(grep -c '^pod rm --force ' "$CPKT_MOCK_PODMAN_LOG") != 2 ]]; then
  printf 'devenv did not remove both checkout-owned pods by name\n' >&2
  exit 5
fi

bash "$devenv" up >/dev/null
cmake -E rm -f "$manifest"
bash "$devenv" up >/dev/null
if [[ $(grep -c '^kube play ' "$CPKT_MOCK_PODMAN_LOG") != 4 ]]; then
  printf 'devenv did not replace stale pods when the manifest was missing\n' >&2
  exit 6
fi
bash "$devenv" reset
if [[ -e $fixture/build/devenv ]] || find "$CPKT_MOCK_PODS" -type f | grep -q .; then
  printf 'devenv reset left state or pods behind\n' >&2
  exit 7
fi

# Even when Podman removes both pods, a teardown error must fail the public
# e2e command rather than disappear behind a successful integration result.
if CPKT_MOCK_KUBE_DOWN_FAIL=1 bash "$fixture/scripts/test-e2e.sh" /bin/true \
    >"$fixture/e2e.log" 2>&1; then
  printf 'e2e succeeded after Podman reported a teardown error\n' >&2
  exit 12
fi
if find "$CPKT_MOCK_PODS" -type f | grep -q .; then
  printf 'e2e did not finish removing pods after a teardown error\n' >&2
  exit 13
fi
grep -q 'failed to stop database pods' "$fixture/e2e.log"
grep -q 'PostgreSQL and CockroachDB passed' "$fixture/e2e.log"

bash "$devenv" up >/dev/null
mkdir -p "$fixture/.cache" "$fixture/dist"
: > "$fixture/.cache/keep-on-teardown-failure"
: > "$fixture/dist/keep-on-teardown-failure"
clean_fixture() (
  export GROUP=all PRESET=debug
  bash "$fixture/scripts/clean.sh" clean --group all
)
if CPKT_MOCK_POD_RM_FAIL=1 clean_fixture >/dev/null 2>&1; then
  printf 'clean succeeded while database pods remained running\n' >&2
  exit 8
fi
if [[ ! -d $fixture/build/devenv || ! -f $fixture/.cache/keep-on-teardown-failure ||
      ! -f $fixture/dist/keep-on-teardown-failure ]] ||
    ! find "$CPKT_MOCK_PODS" -type f | grep -q .; then
  printf 'clean deleted generated state after pod teardown failed\n' >&2
  exit 9
fi
clean_fixture
if [[ -e $fixture/build || -e $fixture/.cache || -e $fixture/dist ]] ||
    find "$CPKT_MOCK_PODS" -type f | grep -q .; then
  printf 'clean left database pods or generated state behind\n' >&2
  exit 10
fi
