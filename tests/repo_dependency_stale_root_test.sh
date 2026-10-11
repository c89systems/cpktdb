#!/usr/bin/env bash
set -euo pipefail
repo=${1:?repository required}
work=$(mktemp -d "${TMPDIR:?owned fixture scratch required}/dependency-roots.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
for key in CPKT_EXTERNAL_ROOT CPKT_DEPENDENCY_BUILD_ROOT CPKT_DEPENDENCY_CONTRACT_ROOT CPKT_DOWNLOAD_ROOT CPKT_DIST_DIR; do
  if cmake -S "$repo" -B "$work/$key" -G Ninja \
      -DCPKT_GROUP=db -DCPKT_TARGET_ARCH=x86_64 -DCPKT_TARGET_OS=linux -DCPKT_TARGET_LIBC=gnu \
      "-DCMAKE_TOOLCHAIN_FILE=$repo/cmake/CpktReadOnlyToolchain.cmake" \
      "-D$key=$work/borrowed" -DCPKT_DEPENDENCY_PRODUCER=ON -DCPKT_BUILD_DEPENDENCIES=ON \
      -DCPKT_ALLOW_DEPENDENCY_ROOT_OVERRIDE=ON -DCPKT_CALLER_OWNED_DEPENDENCY_ROOTS=ON \
      > "$work/diagnostic" 2>&1; then
    printf 'Borrowed dependency root accepted: %s\n' "$key" >&2
    exit 1
  fi
  if ! grep -Fq "$key must stay in this checkout" "$work/diagnostic"; then
    cat "$work/diagnostic" >&2
    exit 1
  fi
  [ ! -e "$work/borrowed" ]
done
