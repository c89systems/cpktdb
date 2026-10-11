#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d "$root/build/source-failure.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
mkdir -p "$work/payload/cpktdb-0.0.0"
printf '0.0.0\n' > "$work/payload/cpktdb-0.0.0/VERSION"
printf 'VERSION\nRELEASE_MANIFEST\n' > "$work/payload/cpktdb-0.0.0/RELEASE_MANIFEST"
bash "$root/scripts/archive.sh" "$work/payload/cpktdb-0.0.0" "$work/source.tar.gz" >/dev/null
if cmake "-DCPKT_SOURCE=$root" "-DCPKT_ARCHIVE=$work/source.tar.gz" \
    "-DCPKT_PREFIX=$work/extracted" -DCPKT_VERSION=0.0.0 \
    -P "$root/tests/source/archive_test.cmake" > "$work/diagnostic" 2>&1; then
  printf 'Incomplete source reconstruction input was accepted\n' >&2
  exit 1
fi
grep -Fq 'Missing reconstruction input' "$work/diagnostic"
