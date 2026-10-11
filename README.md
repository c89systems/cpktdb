# cpktdb: Database C89 SDK

[cpktdb on GitHub](https://github.com/c89systems/cpktdb) ships static and
shared libraries with strict C89 public interfaces, CMake packages, pkg-config
metadata, examples and complete third-party license notices.

## Delivered components

- postgresql
- iodbc
- sqlite

Public headers keep the `<cpkt/...>` namespace and existing facade library names.
Shared-library ABI majors retain their existing identities. Repository release
versions are independent of those ABI majors.

## Independent repositories

| Repository | Scope | Prerequisite |
| --- | --- | --- |
| [cpkt](https://github.com/c89systems/cpkt) | Common libraries and cmocka | None |
| [cpktdb](https://github.com/c89systems/cpktdb) | PostgreSQL, SQLite and iODBC | Pinned cpkt SDK |
| [cpktmisc](https://github.com/c89systems/cpktmisc) | OPC UA, PDF, audio and speech | Pinned cpkt SDK |

Database and misc do not depend on each other. Only core maintains `skills/`.
Each producer owns its source, dependency recipes, tests, notices, artifacts and
release cycle. A database or misc change never starts a core producer or runs
core's standalone test suites.

## Development

Host Bash >= 4.4 is required for the lifecycle helpers. On macOS, run
`brew install bash`, then `export PATH="$(brew --prefix bash)/bin:$PATH"`
before Make or scripts. Check the selected shell with
`bash scripts/require-host-bash.sh`.

Install CMake >= 3.21, Ninja/Make, Python for source generators, GNU tar/gzip,
Info-ZIP, LLVM/Clang, clangd and clang-format. Memory checks use Valgrind; ARM
Linux tests use QEMU; database integration uses rootless Podman. Linux builds
use pinned Bootlin toolchains; Darwin cross builds require prepared osxcross
and an Apple SDK. Default concurrency is eight locally and two on native Darwin.

```sh
make deps-core
make debug
make finalize-slice
```

`make help` lists commands and selectors. Build/test commands default to native
Debug; `PRESET=<target>-release` selects a Release target. Acquire its core SDK
with `make deps-core PRESET=<preset>` before ordinary build/test operations.
`finalize-slice` formats C and headers and checks Debug tests and clangd;
`make format-check` checks formatting before commits. `make test-all` includes
database integration and memory checks; `make lifecycle-check` runs lightweight
lifecycle fixtures.

Shared archives and toolchains live under `${XDG_CACHE_HOME:-$HOME/.cache}/cpkt`
in `deps/` and `toolchains/`. Override them with `CPKT_DEPENDENCY_CACHE` and
`CPKT_TOOLCHAIN_CACHE`. `make clean` removes all of `build/`, `dist/` and local
`.cache/`, including hidden contents, while preserving global caches and
installed tools. Each checkout owns its sources and generated state.

## Using the SDK

`dependencies/cpkt.json` pins the published cpkt core 0.2.0 SDK for every target
by URL, SHA-256 and package ID. Install matching core and database SDKs in
separate prefixes, then supply both through standard discovery:

```sh
cmake -S examples/db-sdk -B build/installed-example \
  -DCMAKE_PREFIX_PATH="/opt/cpktdb;/opt/cpkt"
PKG_CONFIG_PATH=/opt/cpktdb/lib/pkgconfig:/opt/cpkt/lib/pkgconfig \
  pkg-config --static --libs cpkt-postgres
```

Database packages validate the exact core version, target and package identity.
Provider payloads remain separate. The public interfaces are documented in the
[PostgreSQL spec](docs/postgres-c89-facade-spec.md),
[SQLite spec](docs/sqlite-c89-facade-spec.md) and
[SQLite surface contract](docs/sqlite-c89-facade-surface.md).

## Distribution and release

Targets are x86_64, aarch64 and armhf Linux with GNU and musl, plus arm64 Darwin.
Each release ships:

- `cpktdb-<version>-<target>.tar.gz` for each target;
- `cpktdb-<version>-arm64-apple-darwin-smoke-test.zip`;
- `cpktdb-<version>.tar.gz`, the independently reconstructed source archive;
- `cpktdb-<version>-CHECKSUMS`, the exact upload inventory.

The `pkt-systems-cmake-lifecycle` skill governs releases. `make prerelease`
provides incremental preparation; `make release` requires a clean signed commit
and its exact lightweight `vX.Y.Z` tag, then cleans and verifies the full shipment.
Final release caches must be under the global `cpkt` cache. Push and publish only
after the complete local gate passes. Darwin release proof uses local osxcross;
GitHub Actions is an optional manual diagnostic.

`make package-source-smoke` reconstructs and tests the source archive using its
own build state. Package verification checks final bytes and installed/relocated
consumers; the complete CHECKSUMS file selects the publication assets.
