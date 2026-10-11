#!/usr/bin/env bash
case ${BASH_SOURCE[0]} in
  */*) source "${BASH_SOURCE[0]%/*}/require-host-bash.sh" ;;
  *) source ./require-host-bash.sh ;;
esac || exit $?
set -euo pipefail
[ "$#" -eq 2 ] || { printf 'usage: zip-archive.sh <prefix> <archive>\n' >&2; exit 2; }
prefix=$1
archive=$2
command -v zip >/dev/null || { printf 'Info-ZIP zip is required for deterministic smoke archives\n' >&2; exit 2; }
# The caller supplies owned, stable staging/output paths. Release member names
# have no newlines. Normalize staged times and strip ZIP UID/GID and time extras;
# CMake/libarchive retains changing access/creation times in local ZIP headers.
export LC_ALL=C TZ=UTC
mkdir -p "$(dirname -- "$archive")"
temporary="$archive.tmp"
trap 'rm -f -- "$temporary"' EXIT
rm -f -- "$temporary"
(cd "$(dirname -- "$prefix")"
  root=$(basename -- "$prefix")
  find "$root" -exec touch -t 198001010000.00 {} +
  find "$root" -print | sort | zip -X -q -@ "$temporary"
)
if [ -f "$archive" ] && cmp -s -- "$temporary" "$archive"; then
  rm -f -- "$temporary"
else
  mv -- "$temporary" "$archive"
fi
