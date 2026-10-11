#!/usr/bin/env bash
set -euo pipefail
root=${1:?repository required}
exec bash "$root/tests/test-cpkt-aflpp-resolver.sh"
