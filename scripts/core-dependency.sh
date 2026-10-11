#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/lifecycle-common.sh"
target=${1:?target required}
cpkt_check_cache_overrides
workspace="$cpkt_root/build/core-acquisition/$target"
cpkt_owned_path "$workspace"
cpkt_owned_path "$workspace/sdk"
mkdir -p "$workspace"
trap 'rm -rf -- "$workspace/sdk"' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
"$cpkt_cmake" "-DCPKT_REPO_ROOT=$cpkt_root" "-DCPKT_CORE_TARGET=$target" \
  -DCPKT_CORE_ACTION=prepare -P "$cpkt_root/cmake/core-dependency.cmake"
