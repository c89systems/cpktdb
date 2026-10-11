#!/usr/bin/env bash
set -euo pipefail
repo=${1:?repository required}
work=${2:?scratch required}/release-flow
rm -rf -- "$work"
root="$work/entry"
mkdir -p "$root/scripts"
(cd "$work"
  cmake -DCPKT_INFO=target -DCPKT_TARGET=cpkt_postgres_integration_test \
    -P "$repo/cmake/lifecycle-info.cmake"
)
if cmake -DCPKT_INFO=target -DCPKT_TARGET=not_an_owned_target \
    -P "$repo/cmake/lifecycle-info.cmake" > "$work/target-refusal.log" 2>&1; then
  printf 'Unknown release integration target was accepted\n' >&2
  exit 1
fi
grep -Fq 'Missing inventory ownership' "$work/target-refusal.log"
cp -- "$repo/scripts/release.sh" "$root/scripts/"
cp -- "$repo/scripts/source-reconstruct.sh" "$root/scripts/"
export CPKT_RELEASE_FLOW_LOG="$work/calls"
unset GROUP PRESET SCOPE CPKT_RELEASE_VERSION_OVERRIDE CPKT_LIFECYCLE_SCRIPTS_DIR
cat > "$root/scripts/lifecycle-common.sh" <<'EOF'
cpkt_scripts=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
cpkt_root=${cpkt_scripts%/scripts}
cpkt_provider=cpktdb
cpkt_default_preset() { printf 'debug\n'; }
cpkt_fail() { printf '%s\n' "$*" >&2; exit 2; }
EOF
cat > "$root/scripts/lifecycle.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/lifecycle-common.sh"
printf '%s\n' "$1" >> "$CPKT_RELEASE_FLOW_LOG"
[ "${CPKT_RELEASE_FLOW_FAILURE:-}" != "$1" ] || exit 42
case "$1" in
  clean) rm -rf -- "$cpkt_root/build" "$cpkt_root/dist" "$cpkt_root/.cache" ;;
  package-source)
    mkdir -p "$cpkt_root/dist"
    printf source > "$cpkt_root/dist/cpktdb-1.2.3.tar.gz" ;;
esac
EOF
printf 'printf "1.2.3\\n"\n' > "$root/scripts/release-version.sh"
cat > "$root/scripts/source-archive-verify.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
[ -f "$1" ] && [ "$2:$3" = 1.2.3:--release-build ]
printf 'source-release\n' >> "$CPKT_RELEASE_FLOW_LOG"
EOF
invoke() { bash "$root/scripts/release.sh" release; }
mkdir -p "$root/build" "$root/.cache" "$root/dist"
for directory in build .cache dist; do : > "$root/$directory/sentinel"; done
if CPKT_RELEASE_FLOW_FAILURE=lifecycle-version-contract invoke; then exit 1; fi
[ "$(cat "$CPKT_RELEASE_FLOW_LOG")" = lifecycle-version-contract ]
for directory in build .cache dist; do [ -f "$root/$directory/sentinel" ]; done
: > "$CPKT_RELEASE_FLOW_LOG"
invoke
[ "$(cat "$CPKT_RELEASE_FLOW_LOG")" = $'lifecycle-version-contract\nclean\nformat-check\npackage-source\nsource-release' ]
[ ! -e "$root/build" ] && [ ! -e "$root/.cache" ]
: > "$CPKT_RELEASE_FLOW_LOG"
if CPKT_RELEASE_FLOW_FAILURE=format-check invoke; then exit 1; fi
[ "$(cat "$CPKT_RELEASE_FLOW_LOG")" = $'lifecycle-version-contract\nclean\nformat-check' ]

# Extracted source owns the full pipeline and final checksum/byte checks.
cat > "$root/scripts/release.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
[ "$1" = release-pipeline ]
printf 'release-pipeline\n' >> "$CPKT_RELEASE_FLOW_LOG"
EOF
cat > "$root/scripts/package.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
[ "$2:$3:$4:$5" = --group:all:--scope:release ]
printf '%s\n' "$1" >> "$CPKT_RELEASE_FLOW_LOG"
EOF
printf source > "$work/source.tar.gz"
: > "$CPKT_RELEASE_FLOW_LOG"
bash "$root/scripts/source-reconstruct.sh" release "$work/source.tar.gz"
cmp "$work/source.tar.gz" "$root/dist/cpktdb-1.2.3.tar.gz"
[ "$(cat "$CPKT_RELEASE_FLOW_LOG")" = $'release-pipeline\npackage-checksums\npackage-verify' ]
mkdir -p "$root/.cache"
: > "$CPKT_RELEASE_FLOW_LOG"
if bash "$root/scripts/source-reconstruct.sh" release "$work/source.tar.gz" > "$work/refusal.log" 2>&1; then exit 1; fi
grep -Fq 'starts from clean compiled state' "$work/refusal.log"
[ ! -s "$CPKT_RELEASE_FLOW_LOG" ]

# A source child retains global archives and owns any checkout-local override.
source_child="$work/extracted source with spaces"
mkdir -p "$root/build/x86_64-linux-gnu/db/Debug"
check_source_cache() {
  local cache=$1 expected=$2 result
  printf 'CPKT_DEPENDENCY_BUILD_JOBS:STRING=4\nCPKT_DEPENDENCY_CACHE:PATH=%s\nCMAKE_GENERATOR:INTERNAL=Unix Makefiles\n' "$cache" \
    > "$root/build/x86_64-linux-gnu/db/Debug/CMakeCache.txt"
  result=$(env -u CPKT_DEPENDENCY_CACHE -u CPKT_SOURCE_GENERATOR \
    CPKT_PRESET=debug CPKT_DEPENDENCY_BUILD_JOBS=3 CMAKE_BUILD_PARALLEL_LEVEL=3 \
    bash -c 'source "$1/scripts/source-environment.sh" "$2" "$3"; printf "%s\n" "$CPKT_DEPENDENCY_CACHE" "$CPKT_DEPENDENCY_BUILD_JOBS" "$CPKT_SOURCE_GENERATOR"' \
    source-cache "$repo" "$root" "$source_child")
  [ "$result" = "$expected"$'\n3\nUnix Makefiles' ] || {
    printf 'Source reconstruction inherited the wrong cache or settings: %s\n' "$result" >&2
    exit 1
  }
}
global_cache="${XDG_CACHE_HOME:-$HOME/.cache}/cpkt/deps"
check_source_cache "$global_cache" "$global_cache"
check_source_cache "$root/build/local archives" "$source_child/build/local archives"
check_source_cache "$root/.cache/local archives" "$source_child/.cache/local archives"
