SHELL := bash
.SHELLFLAGS := -euo pipefail -c
.DEFAULT_GOAL := help
export PYTHONDONTWRITEBYTECODE := 1
.NOTPARALLEL:
GROUP ?= all
PRESET ?= $(if $(filter Darwin,$(shell uname -s)),arm64-apple-darwin-debug,debug)
SCOPE ?=
DEPENDENCY ?=
CORE_ASSET_DIR ?= $(CURDIR)/build/core-release-0.3.0
PRESET_EXPLICIT := $(if $(filter command line environment environment override,$(origin PRESET)),yes,no)
SCOPE_EXPLICIT := $(if $(filter command line environment environment override,$(origin SCOPE)),yes,no)
.PHONY: help print-release-version build build-debug build-host build-release clangd-surface clean clean-dist cross-build cross-test debug deps deps-all deps-cross deps-debug deps-release dev-down dev-logs dev-ps dev-reset dev-up e2e-postgres examples finalize-slice format format-check lifecycle-version-contract package package-checksums package-source package-source-smoke package-verify prerelease prerelease-hardening release release-final-matrix release-matrix release-pipeline source-archive test test-all test-cross test-darwin-native test-darwin-sdk test-debug test-e2e test-github-actions-contracts test-host test-install-tree valgrind verify-release-archives verify-release-privacy verify-source-archive

help:
	@printf '  %-30s %s\n' 'lifecycle-check' 'Run native lifecycle fixtures without configuring cpktdb.'
	@printf '  %-30s %s\n' 'lifecycle-core-assets' 'Verify all pinned published core archives in CORE_ASSET_DIR.'
	@printf 'Usage: make <target> [GROUP=db|all] [PRESET=<preset>] [SCOPE=selected|binary|release]\n\n'
	@printf 'Build and dependencies:\n'
	@printf '  %-30s %s\n' 'help' 'Show this command index.'
	@printf '  %-30s %s\n' 'deps DEPENDENCY=<name>' 'Build one dependency closure (set PRESET, default debug).'
	@printf '  %-30s %s\n' 'deps-core' 'Acquire and validate pinned published core for PRESET.'
	@printf '  %-30s %s\n' 'deps-all' 'Build every dependency closure for PRESET (default debug).'
	@printf '  %-30s %s\n' 'deps-debug' 'Configure the host debug dependency/build graph.'
	@printf '  %-30s %s\n' 'deps-release' 'Configure all shipped Linux release dependency/build graphs.'
	@printf '  %-30s %s\n' 'deps-cross' 'Configure cross release dependency/build graphs.'
	@printf '  %-30s %s\n' 'build' 'Build native Debug; explicit PRESET selects another configuration.'
	@printf '  %-30s %s\n' 'build-debug' 'Build the host debug preset.'
	@printf '  %-30s %s\n' 'build-release' 'Build this repository for six Linux Release targets (or explicit PRESET).'
	@printf '  %-30s %s\n' 'build-host' 'Alias for build-debug.'
	@printf '  %-30s %s\n' 'cross-build' 'Alias for build-release.'
	@printf '  %-30s %s\n' 'debug' 'Build and test the host debug preset.'
	@printf '\nTests:\n'
	@printf '  %-30s %s\n' 'test' 'Build and test native Debug; explicit PRESET selects another configuration.'
	@printf '  %-30s %s\n' 'test-debug' 'Run the host debug tests.'
	@printf '  %-30s %s\n' 'test-host' 'Alias for test-debug.'
	@printf '  %-30s %s\n' 'test-cross' 'Run release preset tests for cross-capable targets.'
	@printf '  %-30s %s\n' 'cross-test' 'Alias for test-cross.'
	@printf '  %-30s %s\n' 'test-all' 'Run the full local confidence gate.'
	@printf '  %-30s %s\n' 'test-install-tree' 'Run install-tree package consumer smoke tests.'
	@printf '  %-30s %s\n' 'examples' 'Build and smoke-test source-tree examples.'
	@printf '  %-30s %s\n' 'clangd-surface' 'Verify compile_commands and public hover comments for examples.'
	@printf '  %-30s %s\n' 'e2e-postgres' 'Run facade e2e against local Podman PostgreSQL and CockroachDB.'
	@printf '  %-30s %s\n' 'test-e2e' 'Alias for e2e-postgres.'
	@printf '  %-30s %s\n' 'dev-up' 'Start local database pods (ports CPKT_DEV_POSTGRES_PORT/CPKT_DEV_COCKROACH_PORT).'
	@printf '  %-30s %s\n' 'dev-down' 'Stop the local database pods.'
	@printf '  %-30s %s\n' 'dev-ps' 'Show local database pods.'
	@printf '  %-30s %s\n' 'dev-logs' 'Show local database logs.'
	@printf '  %-30s %s\n' 'dev-reset' 'Stop pods and remove build/devenv state as this user.'
	@printf '  %-30s %s\n' 'valgrind' 'Run native C facade tests under Valgrind Memcheck.'
	@printf '\nPackaging:\n'
	@printf '  %-30s %s\n' 'package' 'Build package artifacts for all supported release targets.'
	@printf '  %-30s %s\n' 'package-source' 'Build the source release archive.'
	@printf '  %-30s %s\n' 'package-source-smoke' 'Verify the source release archive.'
	@printf '  %-30s %s\n' 'package-checksums' 'Write the complete checksum inventory for the selected artifact scope.'
	@printf '  %-30s %s\n' 'package-verify' 'Verify package layout, checksums, privacy, and install-tree consumers.'
	@printf '  %-30s %s\n' 'verify-release-archives' 'Alias for package-verify.'
	@printf '  %-30s %s\n' 'verify-release-privacy' 'Alias for package-verify; privacy is part of the package gate.'
	@printf '\nRelease:\n'
	@printf '  %-30s %s\n' 'prerelease' 'Run the release proof graph without cleaning generated state first.'
	@printf '  %-30s %s\n' 'prerelease-hardening' 'Alias for prerelease; database has no extra fuzz tier.'
	@printf '  %-30s %s\n' 'release-matrix' 'Build, package, checksum, and verify binary release artifacts.'
	@printf '  %-30s %s\n' 'release-final-matrix' 'Run the final binary and clean source-archive release gate.'
	@printf '  %-30s %s\n' 'finalize-slice' 'Format and run the narrow local pre-commit gate.'
	@printf '  %-30s %s\n' 'lifecycle-version-contract' 'Read-only validation of the clean signed commit and exact lightweight tag.'
	@printf '  %-30s %s\n' 'release' 'Run the clean final binary and source-archive release gate.'
	@printf '  %-30s %s\n' 'print-release-version' 'Print the version used by package and release artifacts.'
	@printf '  %-30s %s\n' 'format' 'Format project-owned C, C++ and header files with clang-format.'
	@printf '  %-30s %s\n' 'format-check' 'Fail if project-owned C, C++ or headers need clang-format.'
	@printf '\nCleanup:\n'
	@printf '  %-30s %s\n' 'clean' 'Remove generated build, cache, and dist output.'
	@printf '  %-30s %s\n' 'clean-dist' 'Remove only release artifacts under dist/.'
	@printf '\nSelection: build/test default to native Debug; build-release/test-cross default to six Linux Release targets.\nSelected Release commands require a Release preset, or use native Release for GROUP=db without PRESET.\nSelected packaging requires explicit Release PRESET, writes only build/, and uses the exact pinned published core SDK.\nRelease/matrix/source gates reject narrowing before mutation. Formatting stays global. Local jobs=8, native Darwin jobs=2; explicit configured limits are honored.\n'
	@printf '  %-30s %s\n' 'test-darwin-native' 'Native Darwin source/runtime proof (Apple tools, jobs=2).'
	@printf '  %-30s %s\n' 'test-darwin-sdk' 'Execute supplied Darwin SDK combinations without changing libraries.'
	@printf '  %-30s %s\n' 'test-github-actions-contracts' 'Alias for the native lifecycle fixture suite.'

print-release-version:
	@bash scripts/release-version.sh "$(CURDIR)"

build build-debug build-host build-release clangd-surface clean clean-dist cross-build cross-test debug deps deps-all deps-cross deps-debug deps-release dev-down dev-logs dev-ps dev-reset dev-up e2e-postgres examples finalize-slice format format-check lifecycle-version-contract package package-checksums package-source package-source-smoke package-verify prerelease prerelease-hardening release release-final-matrix release-matrix release-pipeline source-archive test test-all test-cross test-darwin-native test-darwin-sdk test-debug test-e2e test-github-actions-contracts test-host test-install-tree valgrind verify-release-archives verify-release-privacy verify-source-archive:
	@bash scripts/lifecycle.sh "$@" --group "$(GROUP)" --preset "$(PRESET)" --preset-explicit "$(PRESET_EXPLICIT)" --scope "$(SCOPE)" --scope-explicit "$(SCOPE_EXPLICIT)" --dependency "$(DEPENDENCY)"

.PHONY: deps-core
deps-core:
	@bash scripts/lifecycle.sh deps-core --group "$(GROUP)" --preset "$(PRESET)"

.PHONY: lifecycle-check lifecycle-core-assets
lifecycle-check:
	@cmake -S tests/lifecycle -B build/lifecycle-checks -DCPKT_CORE_RELEASE_ASSETS=
	@ctest --test-dir build/lifecycle-checks --stop-on-failure --output-on-failure --no-tests=error

lifecycle-core-assets:
	@cmake -S tests/lifecycle -B build/lifecycle-core-assets -DCPKT_CORE_RELEASE_ASSETS="$(CORE_ASSET_DIR)"
	@ctest --test-dir build/lifecycle-core-assets -L core-release --stop-on-failure --output-on-failure --no-tests=error
