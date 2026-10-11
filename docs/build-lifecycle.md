# Build lifecycle

cpktdb adopts `pkt-systems-cmake-lifecycle`, maintained by the independent cpkt
core repository. Make exposes commands, Bash sequences foreground operations,
CMake owns configure/build/dependency/install work, and CTest executes tests.
Python is limited to declared source/data generators and small data fixtures.
The dependency recipes and public database interfaces remain owned here; the
core SDK is an independently released prerequisite.

## Tools and caches

Host Bash >= 4.4 is required. On macOS, select Homebrew Bash through PATH before
running Make; `/bin/bash` is not the selected lifecycle shell. Check selection
with `bash scripts/require-host-bash.sh`.

The exact vendored skill helper collection lives together under `scripts/`.
`scripts/lifecycle-helpers.txt` records its source. Linux uses the skill's pinned
Bootlin compiler/binutils/sysroot collections, with no host compiler fallback.
Darwin uses selected Apple tools natively or prepared osxcross and pinned host
MIG on Linux. LLVM/clangd/clang-format are workstation prerequisites.
GNU tar/gzip create the SDK/source archives; Info-ZIP `zip` creates the Darwin
smoke ZIP with sorted members, UTC timestamps and incidental metadata stripped.

Shared verified dependency archives use `CPKT_DEPENDENCY_CACHE`, then XDG cache,
then `$HOME/.cache/cpkt/deps`; an explicit CMake PATH takes precedence over the
environment. Tool collections use `CPKT_TOOLCHAIN_CACHE`, then
`${XDG_CACHE_HOME:-$HOME/.cache}/cpkt/toolchains`. Verified cache hits make no
acquisition requests. Explicit preparation owns provisioning; configure,
try_compile and borrowed consumers only discover prepared tools.

Only the declared global cache and installed workstation prerequisites survive
full clean. The required cleanup contract removes all of `build/`, `dist/` and
local `.cache/`, including hidden contents. Sources, generated tools and local
state from another checkout are never shared prerequisites.

## Independent core prerequisite

`dependencies/cpkt.json` pins cpkt 0.2.0 for all seven shipped targets by exact
release URL, archive SHA-256 and manifest package ID. The pins were derived from
the published checksum inventory and verified archive bytes. `make deps-core
PRESET=<preset>` is the explicit SDK acquisition entrypoint. Existing consumers
validate the already prepared SDK and import its component versions; no core
producer or sibling source checkout is involved. Installed packages resolve
`CpktCore` at the exact required version through `CMAKE_PREFIX_PATH`, then check
the core package ID and target against their own prerequisite metadata.

The core is a published Release SDK for both Debug and Release cpktdb consumers.
Database payload and releases are independently owned. Public `<cpkt/...>` names,
facade symbols, upstream dependency pins and shared ABI identities are preserved.

## Native build and verification

`make build`, `make test` and `make debug` use native Debug by default. Explicit
`PRESET=<target>-release` selects another configuration. `make build-release`
and `make test-cross` cover the six Linux Release targets; `make release-matrix`
also includes local osxcross Darwin packaging and final archive verification.
Acquire core explicitly with `make deps-core PRESET=<preset>` before ordinary
consumer work. Aggregate release preparation acquires the seven pinned core SDKs.

CMake ExternalProject builds each owned dependency once per target in a Release
producer shared by Debug and Release consumers. Native input records invalidate
changed owned prerequisites; consumers reject missing or stale output. CMake
configure and try_compile discover prepared tool collections without provisioning.
The outer build entrypoint prepares missing selected tool collections.

CTest owns test discovery, selection and results. Small CMake output stamps reuse
complete host, runtime, memory, archive and consumer scopes with declared inputs.
Direct `ctest --test-dir <build-directory> -R <case> --output-on-failure` runs a
focused check. `CPKT_TEST_RERUN=1` invalidates passed build-test scopes explicitly.
Restored/copied state or changed unrepresented environment needs invalidation.

Database API inventories and typed compile probes are declared CMake generators
under `tools/`; native CMake tests compare signatures, layouts, exports, constants,
binding decisions and typed SQLite operations against the reviewed contracts.
The SDK manifest generator encodes installed data and ABI metadata. The only
Python fixture serves fixed archive bytes for native acquisition tests.

`make e2e-postgres` uses rootless Podman PostgreSQL and CockroachDB, with readiness
and teardown owned by the service scripts. `make valgrind` checks the native Debug
CTest memory scope and both real service integrations. Database fuzz products
are not declared; the complete vendored lifecycle helper collection is retained.

## Lifecycle checks

`make lifecycle-check` configures a small CMake/CTest graph with no compiler or
database dependency build. Native Bash/CMake fixtures check core pin completeness,
cache precedence, archive reuse/corruption, toolchain readiness and AFL++ runtime
selection. Fixture state stays under `build/`; no live download is part of this gate.

`make lifecycle-core-assets CORE_ASSET_DIR=<published-assets>` verifies all seven
real core archives, their entries in the published CHECKSUMS and their embedded
manifest identities. Missing archives or mismatched bytes fail the gate.
This proves the prerequisite pins; it does not prove cpktdb compilation or runtime.

Project-owned C and headers retain `make format`, `make format-check`, strict
warnings, C89 coverage, facade tests and clangd checks. `make finalize-slice`
remains required, with `make format-check` after the last edit and before commit.

## Packages and source

Selected `GROUP=db PRESET=<target>-release` packages and checksum inventories stay
under `build/package-stage/`. Aggregate artifacts stay under `dist/`; binary-scope
checksums stay under `build/package-manifests/` until the complete release inventory
exists. Database archives contain only owned payload and declare the exact required
core version, target and package ID.

Archive checks validate final bytes, file modes, licenses, exact inventory,
independent dependency identity, public exports/imports and ELF/Mach-O
architecture and runtime metadata. Real installed CMake/pkg-config consumers,
static PIC consumers, examples and SQLite extensions compile at original and
independently relocated database/core prefixes. Linux consumers execute natively or through the selected QEMU
runner. Darwin cross consumers compile/link; native runtime is an optional diagnostic.

`make package-source` stages tracked source and injects its own `VERSION` and
`RELEASE_MANIFEST`. `make package-source-smoke` validates extraction and reconstructs
a fresh native Release build, tests and SDK consumers in owned scratch. It uses
the extracted helpers, acquires exact core bytes through the shared archive cache,
and never borrows the parent checkout's compiled state. An enclosing Git checkout
does not supply the extracted source's version or disposable cache roots.
Development cache overrides under the parent `build/` or `.cache/` are rebased
under the extracted source root; their contents remain owned by the parent.
Global archive-cache paths pass through unchanged. Job limits and the selected
generator are retained in both cases.

## Execution and release contract

Native SDK install rules stage into a separate database-owned prefix. Extract
core and database releases as separate installations, preserving each packaged
layout. Supply their prefixes through `CMAKE_PREFIX_PATH` or `PKG_CONFIG_PATH`;
do not merge payloads or run `cmake --install` into an existing core SDK. Release
review boundaries are recorded in [review scope](review-exceptions.md).

One operation owns a stable mutable workspace. The terminal/job runner cancels
the complete job/process group; helpers run in the foreground. The new contract
does not require a process broker, descriptor authentication or receipt service.
Ordinary CMake dependencies and small declared suite stamps provide reuse.

Preparation is incremental. Final release checks require a clean committed
checkout, a verified signed final commit and its exact lightweight tag before
cleanup or compilation. One clean local release verifies all selected target
tests and final artifact bytes. Failures stop for a separate repair iteration.
Final release requires archive/tool caches under the global XDG `cpkt` root
outside the checkout. Development may use disposable checkout-local caches;
the release guard rejects those overrides before cleaning so extracted source
never inherits a removed or borrowed cache location.
No default branch or release tag is pushed before the complete local gate passes.

GitHub Actions runs only by explicit manual dispatch on an available ref. Native
Darwin artifacts are diagnostic; local osxcross supplies Darwin release proof.
Publication and pushes remain separately authorized operations.
