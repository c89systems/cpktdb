# cpktdb dependencies

## Shipped libraries

- postgresql (pinned recipe and license: `cmake/components.json`).
- iodbc (pinned recipe and license: `cmake/components.json`).
- sqlite (pinned recipe and license: `cmake/components.json`).

## Prerequisites and test tooling

Build core comes exclusively from checksum-pinned `c89systems/cpkt` 0.3.0 release
artifacts. The exact pins are in `dependencies/cpkt.json`. Core is not repackaged.
The database manifest preserves the exact version, target and package ID in
`built_with_core`. Its `requires_core` records the minimum release version and
target; future core hashes do not need to be known by cpktdb.

Installed database packages discover `CpktCore >= 0.3.0` through ordinary CMake
search paths. Core's version file accepts the requested version only within its
compatibility span (compatibility floor through current release). Consumers may
upgrade core independently while retaining cpktdb. Validation still requires the
same target and verifies both SDKs' manifest identities and payload hashes.
Pkg-config metadata uses `cpkt-core >= 0.3.0`; pkg-config has no compatibility
floor check. Its consumers must first run the same CMake compatibility validation
against the SDK prefixes they intend to use:

```sh
cmake -S /opt/cpktdb/share/doc/cpkt/db/examples/db-sdk \
  -B build/core-compatibility -DCMAKE_PREFIX_PATH="/opt/cpktdb;/opt/cpkt"
```

Only after that configure succeeds should pkg-config resolve the libraries from
those same prefixes. The installed-consumer tests enforce this order for both
CMake and pkg-config links.

`libpslog` is a pinned test-only logging consumer and is not shipped or linked into production facades. CMake, Ninja/Make, Python and host development tools are build/test requirements. Upstream archives, patch series and enabled static/shared features are recorded by the component inventory and recipe. Artifact-local notices accompany each delivered component.
