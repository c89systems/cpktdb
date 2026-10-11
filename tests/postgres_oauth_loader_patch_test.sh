#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
mkdir -p "$repo_root/build"
scratch=$(mktemp -d "$repo_root/build/postgres-oauth-loader.XXXXXX")
trap 'cmake -E remove_directory "$scratch"' EXIT
source_dir="$scratch/src/interfaces/libpq"
mkdir -p "$source_dir"
cat > "$source_dir/fe-auth-oauth.c" <<'SOURCE'
const char *module_name =
#if defined(__darwin__)
    LIBDIR "/libpq-oauth-" PG_MAJORVERSION DLSUFFIX;
#else
    "libpq-oauth-" PG_MAJORVERSION DLSUFFIX;
#endif
SOURCE

cmake -DCPKT_POSTGRESQL_SOURCE_DIR="$scratch" \
  -P "$repo_root/cmake/patch_postgresql_oauth_loader.cmake"
rg -Fq '"@loader_path/libpq-oauth-" PG_MAJORVERSION DLSUFFIX' \
  "$source_dir/fe-auth-oauth.c"
rg -Fq '"libpq-oauth-" PG_MAJORVERSION DLSUFFIX' \
  "$source_dir/fe-auth-oauth.c"
first_hash=$(cmake -E sha256sum "$source_dir/fe-auth-oauth.c")
cmake -DCPKT_POSTGRESQL_SOURCE_DIR="$scratch" \
  -P "$repo_root/cmake/patch_postgresql_oauth_loader.cmake"
test "$first_hash" = "$(cmake -E sha256sum "$source_dir/fe-auth-oauth.c")"
