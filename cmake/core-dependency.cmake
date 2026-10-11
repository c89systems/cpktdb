cmake_minimum_required(VERSION 3.21)
include("${CMAKE_CURRENT_LIST_DIR}/CpktCore.cmake")
cpkt_read_core_pin("${CPKT_REPO_ROOT}" "${CPKT_CORE_TARGET}")
set(prefix "${CPKT_REPO_ROOT}/.cache/cpkt/${CPKT_CORE_TARGET}/install")
if(CPKT_CORE_ACTION STREQUAL "validate")
  cpkt_validate_core("${CPKT_REPO_ROOT}" "${CPKT_CORE_TARGET}" "${prefix}" manifest)
  return()
endif()
if(NOT CPKT_CORE_ACTION STREQUAL "prepare")
  message(FATAL_ERROR "Unknown core prerequisite action")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/CpktMutationPaths.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CpktDependencyArchiveCache.cmake")
cpkt_initialize_dependency_cache()
set(CPKT_DEPENDENCY_CACHE_LOCK_TIMEOUT 120)
set(CPKT_DEPENDENCY_DOWNLOAD_TIMEOUT 300)
set(CPKT_DEPENDENCY_DOWNLOAD_INACTIVITY_TIMEOUT 60)
cpkt_acquire_dependency_archive(archive NAME "${CPKT_CORE_NAME}"
  SHA256 "${CPKT_CORE_sha256}" URLS "${CPKT_CORE_URL}"
  SEED_PATHS "${CPKT_REPO_ROOT}/build/core-release-${CPKT_CORE_VERSION}/${CPKT_CORE_NAME}")
# Preserve an already valid installation; validate in a fresh CMake process so
# a failed installation does not cache success in this preparation operation.
execute_process(COMMAND "${CMAKE_COMMAND}" "-DCPKT_REPO_ROOT=${CPKT_REPO_ROOT}"
  "-DCPKT_CORE_TARGET=${CPKT_CORE_TARGET}" -DCPKT_CORE_ACTION=validate
  -P "${CMAKE_CURRENT_LIST_FILE}" RESULT_VARIABLE ready OUTPUT_QUIET ERROR_QUIET)
if(NOT ready EQUAL 0)
  set(stage "${CPKT_REPO_ROOT}/build/core-acquisition/${CPKT_CORE_TARGET}/sdk")
  cpkt_validate_mutation_paths("${stage}" "${prefix}")
  include("${CMAKE_CURRENT_LIST_DIR}/extract-release-archive.cmake")
  cpkt_extract_release_archive("${archive}" "${stage}"
    "cpkt-${CPKT_CORE_VERSION}-${CPKT_CORE_TARGET}" TRUE)
  cpkt_validate_core("${CPKT_REPO_ROOT}" "${CPKT_CORE_TARGET}" "${stage}" manifest)
  file(REMOVE_RECURSE "${prefix}")
  get_filename_component(parent "${prefix}" DIRECTORY)
  file(MAKE_DIRECTORY "${parent}")
  file(RENAME "${stage}" "${prefix}")
endif()
message(STATUS "Prepared pinned cpkt ${CPKT_CORE_VERSION} SDK: ${CPKT_CORE_TARGET}")
