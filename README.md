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
before Make or scripts. On Linux, install/upgrade the host `bash` package.
Run `bash scripts/require-host-bash.sh` to verify the selected shell. The repository
vendors the exact helper collection from the installed lifecycle skill; its
provenance is in `scripts/lifecycle-helpers.txt`.
See [host setup](docs/build-lifecycle.md) for the scope and PATH requirement.

Run `make help` for the authoritative command index. The default local Debug
preset uses the pinned Bootlin GNU toolchain. Every Linux target uses its own
complete pinned compiler collection. LLVM/Clang, clangd, clang-format, Valgrind
and QEMU are host tools; Darwin cross builds require osxcross and its Apple SDK.
The configured concurrency limit remains eight locally and two on native Darwin.

```sh
make deps-core PRESET=debug
make debug
make finalize-slice
make test-all
```

`finalize-slice` formats project-owned C and headers, runs the Debug tests and
native clangd checks, then asserts formatting. `format-check` is required after
the final edit and before each commit. `test-all` includes the relevant native
hardening and integration tiers. Cross-target and extracted-package consumers
remain part of the complete release gate.

Before consumer operations, explicitly acquire the prerequisite:

```sh
make deps-core PRESET=debug
make debug
```

`dependencies/cpkt.json` pins one published core version and, for every supported
target, its exact GitHub URL, SHA-256 archive digest and portable package ID.
All seven 0.2.0 pins come from checksum-verified published archives and their
embedded core package manifests. There is no sibling-checkout or host-library
fallback. Ordinary consumer operations only validate an existing prerequisite;
`deps-core` is the explicit acquisition action.

An optional release need not have the same version as core. Its manifest records
the exact core release, target and package ID. Each SDK remains in its own
installation prefix; `find_package(CpktCore)` validates the independently installed
core through ordinary CMake search paths before importing
libraries. Never merge or overlay provider payloads.

For installed consumers, supply both installations using standard discovery:

```sh
cmake -S examples/db-sdk -B build/installed-example \
  -DCMAKE_PREFIX_PATH="/opt/cpktdb;/opt/cpkt"
PKG_CONFIG_PATH=/opt/cpktdb/lib/pkgconfig:/opt/cpkt/lib/pkgconfig \
  pkg-config --static --libs cpkt-postgres
```

## Database integration

`make e2e-postgres` tests the PostgreSQL facade against rootless Podman PostgreSQL
and CockroachDB. The Podman Kube manifest is `devenv.yaml.in`; state lives under
`build/devenv/`. E2e always stops the environment, including on failure. `make
valgrind` also runs the facade integration under native memory checking.

## Distribution and release

All seven targets are retained: x86_64, aarch64 and armhf Linux with GNU and musl,
and arm64 Darwin. Each release ships:

- `cpktdb-<version>-<target>.tar.gz` for each target;
- `cpktdb-<version>-arm64-apple-darwin-smoke-test.zip`;
- `cpktdb-<version>.tar.gz`, the independently reconstructed source archive;
- `cpktdb-<version>-CHECKSUMS`, the exact upload inventory.

The adopted lifecycle requires a clean release build against the final signed
commit and exact lightweight tag before any release ref is pushed. Darwin release
assets and evidence come from local osxcross. GitHub Actions is an optional manual
diagnostic on an already available ref; never push to trigger it. `make
lifecycle-version-contract` checks the final signature/tag without changing refs.
`make release-matrix` provides incremental binary preparation; `make release`
validates final identity before full cleanup and the clean local shipment gate.

Shared dependency archives use
`${CPKT_DEPENDENCY_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/cpkt/deps}`;
compiler collections use the corresponding `cpkt/toolchains` root. A verified
SHA-256 cache hit performs no network acquisition. `clean` removes the entire
`build/`, `dist/` and local `.cache/`, including hidden entries, and preserves both
shared caches. Source reconstruction uses its own extracted helpers and compiled
state; only the declared global caches are shared.

Run `make lifecycle-check` for native migration fixtures without configuring
cpktdb. `make lifecycle-core-assets CORE_ASSET_DIR=<directory>` verifies every
pinned published SDK against its archive checksum, published inventory and
embedded package identity. Downloaded audit inputs stay under ignored `build/`.

Run `make package-source-smoke` to extract the source archive and build, test and
verify a fresh native Release SDK, including relocated installed consumers.

The adopted native workflow is documented in
[build lifecycle architecture](docs/build-lifecycle.md).
