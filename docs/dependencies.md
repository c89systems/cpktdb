# cpktdb dependencies

## Shipped libraries

- postgresql (pinned recipe and license: `cmake/components.json`).
- iodbc (pinned recipe and license: `cmake/components.json`).
- sqlite (pinned recipe and license: `cmake/components.json`).

## Prerequisites and test tooling

Core comes exclusively from checksum-pinned `c89systems/cpkt` 0.2.0 release
artifacts. The exact pins are in `dependencies/cpkt.json`. Core is not repackaged.
Installed database packages discover the exact required `CpktCore` independently
through ordinary CMake search paths, then verify the target and package identity.

The compatibility baseline is published cpkt 0.1.0 (`95696cf`); the selected
0.2.0 release is `98f6ed95cd9bbf2263c258bb05abb541847937ce`. Comparison of all
seven released SDKs found unchanged public headers, dependency versions and
source digests, shared ABI identities and exported symbol surfaces. Core facade
filenames and Darwin current versions follow the new release; SONAMEs, install
names and Darwin compatibility versions remain unchanged. Existing static/shared
Linux database consumers built against 0.1.0 also pass with 0.2.0. Darwin runtime
execution is deferred; its SDK is checked with local osxcross tools.

`libpslog` is a pinned test-only logging consumer and is not shipped or linked into production facades. CMake, Ninja/Make, Python and host development tools are build/test requirements. Upstream archives, patch series and enabled static/shared features are recorded by the component inventory and recipe. Artifact-local notices accompany each delivered component.
