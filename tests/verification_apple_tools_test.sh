#!/usr/bin/env bash
set -euo pipefail
repo=${1:?repository required}
work=${2:?scratch required}/apple-scope-tools
rm -rf -- "$work"
mkdir -p "$work/bin"
report=$(bash "$repo/scripts/cpkt-toolchains.sh" discover x86_64-linux-gnu)
grep -qx 'status=ready' <<< "$report"
export CPKT_APPLE_FIXTURE_CC=$(sed -n 's/^cc=//p' <<< "$report")
export CPKT_APPLE_FIXTURE_SDK=$(sed -n 's/^sysroot=//p' <<< "$report")
export CPKT_APPLE_FIXTURE_LOG="$work/xcrun.log"
cat > "$work/bin/xcrun" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf '%s\n' "$*" >> "$CPKT_APPLE_FIXTURE_LOG"
[ "${CPKT_APPLE_FIXTURE_FAIL:-0}" = 0 ] || exit 42
case "$*" in
  '--find clang') printf '%s\n' "$CPKT_APPLE_FIXTURE_CC" ;;
  '--show-sdk-path') printf '%s\n' "$CPKT_APPLE_FIXTURE_SDK" ;;
  *) exit 2 ;;
esac
EOF
chmod +x "$work/bin/xcrun"
export PATH="$work/bin:$PATH"
command=(cmake -DCPKT_SCOPE_FIXTURE_HOST=Darwin "-DCPKT_ROOT=$repo" "-DCPKT_SCRATCH=$work"
  -P "$repo/tests/verification_scopes_test.cmake")
"${command[@]}" > "$work/success.log" 2>&1 || { cat "$work/success.log" >&2; exit 1; }
[ "$(cat "$CPKT_APPLE_FIXTURE_LOG")" = $'--find clang\n--show-sdk-path' ]
if CPKT_APPLE_FIXTURE_FAIL=1 "${command[@]}" > "$work/failure.log" 2>&1; then
  printf 'Scope fixture accepted failed Apple tool selection\n' >&2
  exit 1
fi
grep -Fq 'Command failed (42)' "$work/failure.log"
