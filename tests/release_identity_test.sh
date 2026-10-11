#!/usr/bin/env bash
set -euo pipefail
repo=${1:?repository required}
root=${2:?scratch required}/release-identity
rm -rf -- "$root"
mkdir -p "$root/scripts" "$root/cmake" "$root/empty-template"
for name in release-version.sh version-contract.sh lifecycle-common.sh require-host-bash.sh mutation-paths.sh cpkt-archive-cache.sh cpkt-toolchains.sh cpkt-aflpp.sh cpkt-afl-runtime.sh; do
  cp -- "$repo/scripts/$name" "$root/scripts/"
done
printf 'execute_process(COMMAND "${CMAKE_COMMAND}" -E echo db)\n' > "$root/cmake/lifecycle-info.cmake"
printf '/build/\n' > "$root/.gitignore"
unset CPKT_RELEASE_VERSION_OVERRIDE CPKT_LIFECYCLE_SCRIPTS_DIR GROUP PRESET SCOPE
# This project fixture has its own Git identity and an empty template.
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1
export GIT_AUTHOR_NAME=Fixture GIT_COMMITTER_NAME=Fixture
export GIT_AUTHOR_EMAIL=fixture@example.invalid GIT_COMMITTER_EMAIL=fixture@example.invalid
git -C "$root" init -q --template="$root/empty-template"
git -C "$root" add .
git -C "$root" -c commit.gpgsign=false commit -qm 'test: fixture'
check() { bash "$root/scripts/version-contract.sh" check; }
reject() { if check; then printf 'Invalid release identity was accepted\n' >&2; exit 1; fi; }
[ "$(bash "$root/scripts/release-version.sh" "$root")" = 0.0.0 ]
reject
git -C "$root" -c tag.gpgsign=false tag v1.2.3
reject
# Sign only this fixture with a disposable key, independently of account keys.
mkdir -p "$root/build/signing"
ssh-keygen -q -t ed25519 -N '' -C fixture@example.invalid -f "$root/build/signing/key"
printf 'fixture@example.invalid %s\n' "$(cat "$root/build/signing/key.pub")" > "$root/build/signing/allowed"
git -C "$root" config gpg.format ssh
git -C "$root" config user.signingkey "$root/build/signing/key"
git -C "$root" config gpg.ssh.allowedSignersFile "$root/build/signing/allowed"
git -C "$root" commit --amend --no-edit -S -q
git -C "$root" tag -d v1.2.3
git -C "$root" -c tag.gpgsign=false tag v1.2.3
refs=$(git -C "$root" show-ref)
[ "$(bash "$root/scripts/release-version.sh" "$root")" = 1.2.3 ]
check
[ "$(git -C "$root" show-ref)" = "$refs" ]
CPKT_TOOLCHAIN_CACHE="$root/build/owned-tools" reject
CPKT_DEPENDENCY_CACHE="$root/.cache/local-deps" reject
XDG_CACHE_HOME="$root/build/local-xdg" reject
CPKT_DEPENDENCY_CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/cpkt/deps" check
CPKT_TOOLCHAIN_CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/cpkt/toolchains" check
other="$root/build/other-checkout"
git -C "$root" worktree add --detach -q "$other" HEAD
CPKT_TOOLCHAIN_CACHE="$other/build/tools" reject
CPKT_DEPENDENCY_CACHE="$other/build/deps" reject
CPKT_TOOLCHAIN_CACHE="$repo/build/rehearsal-toolchains" reject
if CPKT_TOOLCHAIN_CACHE="$root/build/owned-tools" bash "$other/scripts/version-contract.sh" check; then exit 1; fi
mkdir -p "$other/build/tools"
ln -s "$other/build/tools" "$root/build/cache-alias"
CPKT_TOOLCHAIN_CACHE="$root/build/cache-alias" reject
cp "$root/build/signing/allowed" "$root/build/signing/allowed.saved"
: > "$root/build/signing/allowed"
reject
mv "$root/build/signing/allowed.saved" "$root/build/signing/allowed"
if CPKT_RELEASE_VERSION_OVERRIDE=2.0.0 check; then exit 1; fi
printf dirty > "$root/dirty"
reject
rm -- "$root/dirty"
git -C "$root" -c tag.gpgsign=false tag v1.2.4
reject
git -C "$root" tag -d v1.2.4 v1.2.3
git -C "$root" -c tag.gpgsign=false tag -a -m annotated v1.2.3
reject
mkdir -p "$root/build/extracted"
printf '3.4.5\n' > "$root/build/extracted/VERSION"
printf 'VERSION\nRELEASE_MANIFEST\n' > "$root/build/extracted/RELEASE_MANIFEST"
[ "$(bash "$root/scripts/release-version.sh" "$root/build/extracted")" = 3.4.5 ]
# An extracted archive owns its own disposable state even under a Git checkout.
mkdir -p "$root/build/extracted/scripts" "$root/build/extracted/cmake"
cp -- "$root/scripts/"*.sh "$root/build/extracted/scripts/"
cp -- "$root/cmake/lifecycle-info.cmake" "$root/build/extracted/cmake/"
cache_check() {
  CPKT_LIFECYCLE_SCRIPTS_DIR="$root/build/extracted/scripts" bash -c \
    'source "$1/scripts/lifecycle-common.sh"; cpkt_validate_cache_location "$2"' \
    bash "$root/build/extracted" "$1"
}
cache_check "$root/build/extracted/.cache/deps"
cache_check "${XDG_CACHE_HOME:-$HOME/.cache}/cpkt/deps"
if cache_check "$root/build/parent-deps"; then exit 1; fi
