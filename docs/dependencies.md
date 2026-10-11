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

`libpslog` is a pinned test-only logging consumer and is not shipped or linked into production facades. CMake, Ninja/Make, Python and host development tools are build/test requirements. Upstream archives, patch series and enabled static/shared features are recorded by the component inventory and recipe. Artifact-local notices accompany each delivered component.
