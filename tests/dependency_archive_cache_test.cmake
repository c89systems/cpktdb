if(NOT DEFINED CPKT_SOURCE_DIR OR CPKT_SOURCE_DIR STREQUAL "")
  message(FATAL_ERROR "CPKT_SOURCE_DIR is required")
endif()
if(NOT DEFINED CPKT_TEST_ROOT OR CPKT_TEST_ROOT STREQUAL "")
  message(FATAL_ERROR "CPKT_TEST_ROOT is required")
endif()

set(CPKT_DEPENDENCY_CACHE "${CPKT_TEST_ROOT}/shared/deps")
set(CPKT_DEPENDENCY_CACHE_LOCK_TIMEOUT 5)
set(CPKT_DEPENDENCY_DOWNLOAD_TIMEOUT 5)
set(CPKT_DEPENDENCY_DOWNLOAD_INACTIVITY_TIMEOUT 5)
set(CPKT_DEPENDENCY_DOWNLOAD_RETRIES 2)
include("${CPKT_SOURCE_DIR}/cmake/CpktDependencyArchiveCache.cmake")

set(_cpkt_input "${CPKT_TEST_ROOT}/fixture.tar.gz")
set(_cpkt_payload_dir "${CPKT_TEST_ROOT}/fixture-payload")
set(_cpkt_local_root "${CPKT_TEST_ROOT}/local/.cache/deps")
file(MAKE_DIRECTORY "${_cpkt_payload_dir}")
file(WRITE "${_cpkt_payload_dir}/fixture-payload.txt" "cpkt dependency archive cache fixture\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar czf "${_cpkt_input}"
  --format=gnutar fixture-payload.txt
  WORKING_DIRECTORY "${_cpkt_payload_dir}" RESULT_VARIABLE _cpkt_archive_status)
if(NOT _cpkt_archive_status EQUAL 0)
  message(FATAL_ERROR "Cannot create the dependency archive fixture")
endif()
file(SHA256 "${_cpkt_input}" _cpkt_expected_sha256)
set(_cpkt_url "file://${_cpkt_input}")

cpkt_acquire_dependency_archive(_cpkt_archive
  NAME "fixture.tar.gz"
  SHA256 "${_cpkt_expected_sha256}"
  URLS "file://${CPKT_TEST_ROOT}/unavailable-fixture.tar.gz"
  SEED_PATHS "${_cpkt_input}")
if(NOT EXISTS "${_cpkt_archive}")
  message(FATAL_ERROR "initial cache miss did not publish an archive")
endif()
file(SHA256 "${_cpkt_archive}" _cpkt_actual_sha256)
if(NOT _cpkt_actual_sha256 STREQUAL _cpkt_expected_sha256)
  message(FATAL_ERROR "initial cache archive checksum did not match")
endif()
file(GLOB _cpkt_partial_archives "${CPKT_DEPENDENCY_CACHE}/archives/sha256/${_cpkt_expected_sha256}/.*.part-*")
if(_cpkt_partial_archives)
  message(FATAL_ERROR "initial cache miss left partial archives: ${_cpkt_partial_archives}")
endif()

file(MAKE_DIRECTORY "${_cpkt_local_root}")
file(COPY_FILE "${_cpkt_archive}" "${_cpkt_local_root}/fixture.tar.gz")
file(REMOVE_RECURSE "${_cpkt_local_root}")
# Hide the origin for the offline hit, then restore the exact pinned bytes.
# Regenerating gzip can change its timestamp and therefore its checksum.
file(RENAME "${_cpkt_input}" "${_cpkt_input}.saved")
cpkt_acquire_dependency_archive(_cpkt_offline_archive
  NAME "fixture.tar.gz"
  SHA256 "${_cpkt_expected_sha256}"
  URLS "${_cpkt_url}")
if(NOT _cpkt_offline_archive STREQUAL _cpkt_archive)
  message(FATAL_ERROR "offline cache hit returned a different archive path")
endif()

file(WRITE "${_cpkt_archive}" "corrupt archive\n")
file(RENAME "${_cpkt_input}.saved" "${_cpkt_input}")
cpkt_acquire_dependency_archive(_cpkt_repaired_archive
  NAME "fixture.tar.gz"
  SHA256 "${_cpkt_expected_sha256}"
  URLS "${_cpkt_url}")
file(SHA256 "${_cpkt_repaired_archive}" _cpkt_repaired_sha256)
if(NOT _cpkt_repaired_sha256 STREQUAL _cpkt_expected_sha256)
  message(FATAL_ERROR "corrupt cache archive was not repaired before reuse")
endif()

file(REMOVE "${_cpkt_archive}")
set(_cpkt_retry_url
  "file://${CPKT_TEST_ROOT}/missing-retry-fixture.tar.gz"
  "${_cpkt_url}")
cpkt_acquire_dependency_archive(_cpkt_retry_archive
  NAME "fixture.tar.gz"
  SHA256 "${_cpkt_expected_sha256}"
  URLS ${_cpkt_retry_url})
file(SHA256 "${_cpkt_retry_archive}" _cpkt_retry_sha256)
if(NOT _cpkt_retry_sha256 STREQUAL _cpkt_expected_sha256)
  message(FATAL_ERROR "retry fallback acquisition did not publish the verified archive")
endif()

# Exercise the actual cached ExternalProject wrapper with two producers. On
# pre-3.24 CMake, an unknown trailing option becomes a shared Ninja byproduct.
# This fixture runs with the selected host CMake, including the supported 3.21.
set(_cpkt_project_source "${CPKT_TEST_ROOT}/byproduct-project-source")
set(_cpkt_project_build "${CPKT_TEST_ROOT}/byproduct-project-build")
file(MAKE_DIRECTORY "${_cpkt_project_source}")
file(WRITE "${_cpkt_project_source}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.21)
project(cached_external_project NONE)
include("${CPKT_REPO}/cmake/CpktDependencies.cmake")
set(CPKT_DEPENDENCY_CACHE "${CPKT_FIXTURE_ROOT}/shared/deps")
set(CPKT_DEPENDENCY_CACHE_LOCK_TIMEOUT 5)
set(CPKT_DOWNLOAD_ROOT "${CPKT_FIXTURE_ROOT}")
foreach(name one two)
  set(binary "${CMAKE_BINARY_DIR}/${name}-binary")
  cpkt_cached_external_project_add(${name}
    URL "file://${CPKT_FIXTURE_ROOT}/unavailable-fixture.tar.gz"
    URL_HASH "SHA256=${CPKT_FIXTURE_SHA256}"
    DOWNLOAD_NAME fixture.tar.gz
    BINARY_DIR "${binary}"
    CONFIGURE_COMMAND "${CMAKE_COMMAND}" -E true
    BUILD_COMMAND "${CMAKE_COMMAND}" -E touch "${binary}/artifact"
    INSTALL_COMMAND "${CMAKE_COMMAND}" -E true
    BUILD_BYPRODUCTS "${binary}/artifact"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
endforeach()
]=])
include("${CPKT_SOURCE_DIR}/tests/test-command.cmake")
cpkt_test_command("${CMAKE_COMMAND}" -S "${_cpkt_project_source}" -B "${_cpkt_project_build}" -G Ninja
  "-DCPKT_REPO=${CPKT_SOURCE_DIR}" "-DCPKT_FIXTURE_ROOT=${CPKT_TEST_ROOT}"
  "-DCPKT_FIXTURE_SHA256=${_cpkt_expected_sha256}")
cpkt_test_command("${CMAKE_COMMAND}" --build "${_cpkt_project_build}" --target one two)
foreach(name one two)
  if(NOT EXISTS "${_cpkt_project_build}/${name}-binary/artifact")
    message(FATAL_ERROR "Cached external producer did not deliver its byproduct: ${name}")
  endif()
endforeach()
message(STATUS "dependency archive cache and external producer helper passed")
