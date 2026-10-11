#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/lifecycle-common.sh"
version=${1:?version required}
prefix="$cpkt_root/build/package-stage/arm64-apple-darwin/db/cpktdb-$version-arm64-apple-darwin"
workspace="$cpkt_root/build/darwin-smoke-package"
cpkt_owned_path "$workspace"
archive="$cpkt_root/dist/cpktdb-$version-arm64-apple-darwin-smoke-test.zip"
cpkt_owned_path "$archive"
cpkt_owned_path "$archive.tmp"
mkdir -p "$cpkt_root/dist"
trap 'rm -f -- "$archive.tmp"' EXIT
rm -rf -- "$workspace"
mkdir -p "$workspace/darwin-smoke-test/bin" "$workspace/darwin-smoke-test/lib"
for file in "$prefix/lib/"*.dylib; do
  [ -e "$file" ] || cpkt_fail "missing Darwin SDK libraries"
  # The smoke transport has a flat library directory with regular file bytes.
  cp -L -- "$file" "$workspace/darwin-smoke-test/lib/"
done
for name in cpkt_abi_smoke_static cpkt_abi_smoke_shared; do
  cp -- "$cpkt_root/build/arm64-apple-darwin/db/Release/$name" "$workspace/darwin-smoke-test/bin/"
done
"$cpkt_cmake" "-DCPKT_MANIFEST=$prefix/share/cpkt/packages/db.json" \
  "-DCPKT_CORE_MANIFEST=$cpkt_root/.cache/cpkt/arm64-apple-darwin/install/share/cpkt/packages/core.json" \
  "-DCPKT_OUTPUT=$workspace/darwin-smoke-test/packages.json" -P "$cpkt_root/cmake/smoke-metadata.cmake"
cat > "$workspace/darwin-smoke-test/README.txt" <<'EOF'
This transport contains database libraries and smoke executables only.
Install the exact cpkt core SDK identified by packages.json separately.
On arm64 macOS, add its lib directory to DYLD_LIBRARY_PATH before running
bin/cpkt_abi_smoke_static and bin/cpkt_abi_smoke_shared from this directory.
EOF
chmod 0755 "$workspace/darwin-smoke-test" "$workspace/darwin-smoke-test/bin" "$workspace/darwin-smoke-test/lib" "$workspace/darwin-smoke-test/bin/"*
chmod 0644 "$workspace/darwin-smoke-test/lib/"* "$workspace/darwin-smoke-test/packages.json" "$workspace/darwin-smoke-test/README.txt"
bash "$cpkt_scripts/zip-archive.sh" "$workspace/darwin-smoke-test" "$archive"
