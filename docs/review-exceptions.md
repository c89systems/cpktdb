# Release review scope

## Installing directly into a core SDK prefix

Core, database and misc are independent installations in separate prefixes.
Native `cmake --install` rules stage only this provider's payload into its own
prefix. Consumers discover prerequisites through `CMAKE_PREFIX_PATH` and
`PKG_CONFIG_PATH` and link imported targets. Never merge or overlay provider
payloads, install optional payload inside core, or repackage core libraries.

The maintainer clarified this boundary during v0.1.0 preparation and updated the
lifecycle skill accordingly. The review probe that installs directly into an
existing core prefix uses an unsupported workflow. Do not add merger, overlay,
in-place preservation or migration mechanisms to support it.

Requiring a merged prefix in package metadata or consumer checks is a supported
dependency-discovery defect and must be fixed. Required tests cover separate
installations, missing/wrong prerequisites, independent relocation and unchanged
published core bytes. Reconsider merged installation only after an explicit
maintainer change to the product contract.
