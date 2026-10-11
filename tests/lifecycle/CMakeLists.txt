cmake_minimum_required(VERSION 3.21)
project(cpktdb_lifecycle_checks NONE)
enable_testing()
get_filename_component(CPKT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." REALPATH)
set(scratch "${CMAKE_BINARY_DIR}/fixtures")
file(RELATIVE_PATH relative "${CPKT_SOURCE_DIR}/build" "${CMAKE_BINARY_DIR}")
if(relative MATCHES "^[.][.]" OR IS_ABSOLUTE "${relative}"
    OR IS_SYMLINK "${CPKT_SOURCE_DIR}/build" OR IS_SYMLINK "${scratch}")
  message(FATAL_ERROR "Lifecycle checks require owned scratch under build/")
endif()
file(MAKE_DIRECTORY "${scratch}")
find_program(HOST_BASH bash REQUIRED)
add_test(NAME literal_arguments COMMAND "${CMAKE_COMMAND}" "-DCPKT_SOURCE_DIR=${CPKT_SOURCE_DIR}"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/literal-arguments.cmake")
add_test(NAME core_pin_inventory COMMAND "${CMAKE_COMMAND}"
  "-DCPKT_SOURCE_DIR=${CPKT_SOURCE_DIR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/core-pins.cmake")
add_test(NAME cache_defaults COMMAND "${CMAKE_COMMAND}"
  "-DCPKT_SOURCE_DIR=${CPKT_SOURCE_DIR}" "-DCPKT_TEST_ROOT=${scratch}/cache-defaults"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/cache-defaults.cmake")
add_test(NAME dependency_archive_cache COMMAND "${CMAKE_COMMAND}"
  "-DCPKT_SOURCE_DIR=${CPKT_SOURCE_DIR}" "-DCPKT_TEST_ROOT=${scratch}/archive-cache"
  -P "${CPKT_SOURCE_DIR}/tests/dependency_archive_cache_test.cmake")
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
  add_test(NAME toolchain_contract
    COMMAND "${HOST_BASH}" "${CPKT_SOURCE_DIR}/tests/test-cpkt-toolchain-resolvers.sh")
  set_tests_properties(toolchain_contract PROPERTIES LABELS resolver ENVIRONMENT "TMPDIR=${scratch}")
  if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
    add_test(NAME afl_contract
      COMMAND "${HOST_BASH}" "${CPKT_SOURCE_DIR}/tests/test-cpkt-aflpp-resolver.sh")
    set_tests_properties(afl_contract PROPERTIES LABELS resolver ENVIRONMENT "TMPDIR=${scratch}")
  endif()
endif()
if(CPKT_CORE_RELEASE_ASSETS)
  get_filename_component(CPKT_CORE_RELEASE_ASSETS "${CPKT_CORE_RELEASE_ASSETS}"
    ABSOLUTE BASE_DIR "${CPKT_SOURCE_DIR}")
  if(NOT IS_DIRECTORY "${CPKT_CORE_RELEASE_ASSETS}")
    message(FATAL_ERROR "Download the pinned published cpkt assets to CORE_ASSET_DIR first")
  endif()
  foreach(target x86_64-linux-gnu x86_64-linux-musl aarch64-linux-gnu aarch64-linux-musl
      armhf-linux-gnu armhf-linux-musl arm64-apple-darwin)
    add_test(NAME "core_release_${target}" COMMAND "${CMAKE_COMMAND}"
      "-DCPKT_SOURCE_DIR=${CPKT_SOURCE_DIR}" "-DCPKT_CORE_RELEASE_ASSETS=${CPKT_CORE_RELEASE_ASSETS}"
      "-DCPKT_CORE_TARGET=${target}" -P "${CMAKE_CURRENT_SOURCE_DIR}/core-pins.cmake")
    set_tests_properties("core_release_${target}" PROPERTIES LABELS core-release)
  endforeach()
endif()
get_property(checks DIRECTORY PROPERTY TESTS)
set_tests_properties(${checks} PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)

foreach(check dependency_inputs archive_reuse zip_archive_reuse checksum_scope sdk_native_validation verification_scopes)
  add_test(NAME "${check}" COMMAND "${CMAKE_COMMAND}"
    "-DCPKT_ROOT=${CPKT_SOURCE_DIR}" "-DCPKT_SCRATCH=${CMAKE_BINARY_DIR}"
    -P "${CPKT_SOURCE_DIR}/tests/${check}_test.cmake")
  set_tests_properties("${check}" PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
endforeach()
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
  add_test(NAME verification_apple_tools COMMAND "${HOST_BASH}"
    "${CPKT_SOURCE_DIR}/tests/verification_apple_tools_test.sh" "${CPKT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
  set_tests_properties(verification_apple_tools PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
endif()
foreach(check host_bash lifecycle_dispatch release_identity release_flow)
  add_test(NAME "${check}" COMMAND "${HOST_BASH}"
    "${CPKT_SOURCE_DIR}/tests/${check}_test.sh" "${CPKT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
  set_tests_properties("${check}" PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
endforeach()
