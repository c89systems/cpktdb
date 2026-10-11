#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/lifecycle-common.sh"
action=${1:?release action required}
[ "${GROUP:-all}" = all ] || cpkt_fail 'release workflows require GROUP=all'
export GROUP=all
run() {
  bash "$cpkt_scripts/lifecycle.sh" "$1" --group all --preset "$(cpkt_default_preset)" \
    --preset-explicit no --scope '' --scope-explicit no --dependency ''
}
case "$action" in
  prerelease|prerelease-hardening) exec bash "$0" release-pipeline ;;
  release-final-matrix) exec bash "$0" release ;;
  release-matrix)
    while IFS= read -r target; do bash "$cpkt_scripts/core-dependency.sh" "$target"; done < <(cpkt_info targets)
    bash "$cpkt_scripts/package.sh" package --group all --scope binary
    bash "$cpkt_scripts/package.sh" package-checksums --group all --scope binary
    bash "$cpkt_scripts/package.sh" package-verify --group all --scope binary
    exit 0 ;;
  release|release-pipeline|test-all) ;;
  *) cpkt_fail "unsupported release action: $action" ;;
esac
if [ "$action" = release ]; then
  # Validate final identity before cleanup, acquisition or any producer starts.
  run lifecycle-version-contract
  run clean
  export CPKT_RELEASE_PRODUCTION=1
  run format-check
  run package-source
  version=$(bash "$cpkt_scripts/release-version.sh" "$cpkt_root")
  bash "$cpkt_scripts/source-archive-verify.sh" \
    "$cpkt_root/dist/$cpkt_provider-$version.tar.gz" "$version" --release-build
  exit 0
fi
bash "$cpkt_scripts/core-dependency.sh" x86_64-linux-gnu
run format-check
run debug
run clangd-surface
run e2e-postgres
run valgrind
if [ "$action" = release-pipeline ]; then run release-matrix; fi
