if(CPKT_REGISTER_HOST_TESTS)
  file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/native-fixtures")
  set(dependency_inputs_inputs cmake/CpktDependencyContract.cmake cmake/CpktComponentInventory.cmake cmake/CpktMutationPaths.cmake cmake/CpktDependencies.cmake cmake/CpktDependencyArchiveCache.cmake cmake/components.json)
  set(archive_reuse_inputs scripts/archive.sh scripts/require-host-bash.sh)
  set(zip_archive_reuse_inputs scripts/zip-archive.sh scripts/require-host-bash.sh)
  set(checksum_scope_inputs cmake/verify-checksums.cmake)
  set(sdk_native_validation_inputs cmake/CpktSDKValidate.cmake)
  set(verification_scopes_inputs cmake/CpktVerification.cmake cmake/CpktMutationPaths.cmake scripts/cpkt-toolchains.sh scripts/cpkt-archive-cache.sh scripts/require-host-bash.sh)
  set(verification_apple_tools_inputs ${verification_scopes_inputs} tests/verification_scopes_test.cmake tests/test-command.cmake)
  set(github_actions_contracts_inputs .github/workflows/darwin-bundle.yml)
  set(host_bash_inputs scripts/require-host-bash.sh)
  set(lifecycle_dispatch_inputs Makefile scripts/lifecycle.sh scripts/package.sh)
  set(release_identity_inputs scripts/version-contract.sh scripts/release-version.sh scripts/lifecycle-common.sh scripts/mutation-paths.sh scripts/require-host-bash.sh scripts/cpkt-archive-cache.sh scripts/cpkt-toolchains.sh scripts/cpkt-aflpp.sh scripts/cpkt-afl-runtime.sh)
  set(release_flow_inputs scripts/release.sh scripts/source-reconstruct.sh scripts/source-environment.sh scripts/lifecycle-common.sh scripts/mutation-paths.sh cmake/lifecycle-info.cmake cmake/CpktGroups.cmake cmake/CpktLiteralArguments.cmake cmake/components.json)
  foreach(check dependency_inputs archive_reuse zip_archive_reuse checksum_scope sdk_native_validation verification_scopes github_actions_contracts)
    set(file "${check}_test.cmake")
    if(check STREQUAL "github_actions_contracts")
      set(file github_actions_contract_test.cmake)
    endif()
    cpkt_group_add_test(NAME "native_${check}" COMMAND "${CMAKE_COMMAND}"
      "-DCPKT_ROOT=${CMAKE_SOURCE_DIR}" "-DCPKT_REPO=${CMAKE_SOURCE_DIR}"
      "-DCPKT_SCRATCH=${CMAKE_BINARY_DIR}/native-fixtures"
      -P "${CMAKE_SOURCE_DIR}/tests/${file}")
    set_property(GLOBAL PROPERTY "CPKT_TEST_INPUTS_native_${check}"
      "${CMAKE_SOURCE_DIR}/tests/${file};${CMAKE_SOURCE_DIR}/tests/test-command.cmake")
    foreach(input IN LISTS ${check}_inputs)
      set_property(GLOBAL APPEND PROPERTY "CPKT_TEST_INPUTS_native_${check}" "${CMAKE_SOURCE_DIR}/${input}")
    endforeach()
    set_tests_properties("native_${check}" PROPERTIES LABELS "host-lifecycle" TIMEOUT 60 RUN_SERIAL TRUE)
  endforeach()
  set(bash_checks host_bash lifecycle_dispatch release_identity release_flow)
  if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND bash_checks verification_apple_tools)
  endif()
  foreach(check IN LISTS bash_checks)
    cpkt_group_add_test(NAME "native_${check}" COMMAND bash "${CMAKE_SOURCE_DIR}/tests/${check}_test.sh"
      "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/native-fixtures")
    set_property(GLOBAL PROPERTY "CPKT_TEST_INPUTS_native_${check}" "${CMAKE_SOURCE_DIR}/tests/${check}_test.sh")
    foreach(input IN LISTS ${check}_inputs)
      set_property(GLOBAL APPEND PROPERTY "CPKT_TEST_INPUTS_native_${check}" "${CMAKE_SOURCE_DIR}/${input}")
    endforeach()
    set_tests_properties("native_${check}" PROPERTIES LABELS "host-lifecycle" TIMEOUT 60 RUN_SERIAL TRUE)
  endforeach()
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  cpkt_group_add_test(NAME sdk_destdir COMMAND "${CMAKE_COMMAND}"
    "-DCPKT_ROOT=${CMAKE_SOURCE_DIR}" "-DCPKT_BINARY=${CMAKE_BINARY_DIR}"
    -P "${CMAKE_SOURCE_DIR}/tests/sdk_destdir_test.cmake")
  set_property(GLOBAL APPEND PROPERTY CPKT_TEST_INPUTS_sdk_destdir
    "${CMAKE_BINARY_DIR}/cmake_install.cmake" "${CMAKE_BINARY_DIR}/cpkt-sdk-install.cmake"
    "${CMAKE_BINARY_DIR}/cpkt-package-components.json"
    "${CMAKE_SOURCE_DIR}/tests/test-command.cmake"
    "${CMAKE_SOURCE_DIR}/cmake/package_metadata.cmake"
    "${CMAKE_SOURCE_DIR}/cmake/sdk-discovery.cmake"
    "${CMAKE_SOURCE_DIR}/tools/generate_sdk_manifest.py")
  set_tests_properties(sdk_destdir PROPERTIES LABELS packaging TIMEOUT 60 RUN_SERIAL TRUE)
endif()
