cmake_minimum_required(VERSION 3.21)
include(cmake/CpktGroups.cmake)
execute_process(
  COMMAND "${CMAKE_SOURCE_DIR}/scripts/release-version.sh" "${CMAKE_SOURCE_DIR}"
  OUTPUT_VARIABLE CPKT_PROJECT_VERSION
  RESULT_VARIABLE CPKT_PROJECT_VERSION_RESULT
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT CPKT_PROJECT_VERSION_RESULT EQUAL 0)
  message(FATAL_ERROR "failed to resolve cpkt release version")
endif()
project(cpktdb VERSION "${CPKT_PROJECT_VERSION}" LANGUAGES C CXX)
include(cmake/CpktABIVersions.cmake)
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND CPKT_OSXCROSS_ROOT)
  set(_cpkt_osxcross_link_launcher "")
  foreach(_cpkt_argument IN ITEMS "${CMAKE_COMMAND}" "-E" "env"
        "LD_LIBRARY_PATH=${CPKT_OSXCROSS_ROOT}/lib:$ENV{LD_LIBRARY_PATH}")
    string(REPLACE "'" "'\"'\"'" _cpkt_quoted_argument "${_cpkt_argument}")
    string(APPEND _cpkt_osxcross_link_launcher " '${_cpkt_quoted_argument}'")
  endforeach()
  set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK
      "${_cpkt_osxcross_link_launcher}")
endif()
if(CPKT_DEPENDENCY_PRODUCER AND CMAKE_EXPORT_COMPILE_COMMANDS)
  # Dependency-only graphs contain no project-owned compile commands.
  set(CMAKE_EXPORT_COMPILE_COMMANDS OFF)
endif()
include(CTest)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
option(CPKT_BUILD_DEPENDENCIES "Download and build pinned third-party dependencies." ON)
option(CPKT_BUILD_TESTS "Build ABI/link smoke tests for published dependency artifacts." ON)
if(CPKT_BUILD_DEPENDENCIES AND NOT CPKT_DEPENDENCY_PRODUCER)
  message(FATAL_ERROR "Use scripts/build.sh configure/build to prepare the single owning producer; repository consumer graphs import successful output only")
endif()
function(cpkt_normalize_arch arch out_var)
  string(TOLOWER "${arch}" _cpkt_arch)
  if(_cpkt_arch STREQUAL "amd64")
    set(_cpkt_arch "x86_64")
  elseif(_cpkt_arch STREQUAL "arm64")
    set(_cpkt_arch "aarch64")
  elseif(_cpkt_arch STREQUAL "arm")
    set(_cpkt_arch "armhf")
  endif()
  set(${out_var} "${_cpkt_arch}" PARENT_SCOPE)
endfunction()
include(cmake/CpktDependencyContract.cmake)
function(cpkt_target_executables_runnable out_var)
  string(TOLOWER "${CMAKE_SYSTEM_NAME}" _cpkt_target_os)
  string(TOLOWER "${CMAKE_HOST_SYSTEM_NAME}" _cpkt_host_os)
  cpkt_normalize_arch("${CMAKE_SYSTEM_PROCESSOR}" _cpkt_target_arch)
  cpkt_normalize_arch("${CMAKE_HOST_SYSTEM_PROCESSOR}" _cpkt_host_arch)
  if(CMAKE_CROSSCOMPILING_EMULATOR)
    set(${out_var} ON PARENT_SCOPE)
  elseif(_cpkt_target_os STREQUAL _cpkt_host_os AND _cpkt_target_arch STREQUAL _cpkt_host_arch)
    set(${out_var} ON PARENT_SCOPE)
  else()
    set(${out_var} OFF PARENT_SCOPE)
  endif()
endfunction()
cpkt_target_executables_runnable(CPKT_CAN_RUN_TARGET_EXECUTABLES)
include(cmake/CpktLocalRuntime.cmake)
set(CPKT_TEST_EXECUTABLE_PREFIX)
if(CMAKE_CROSSCOMPILING AND CMAKE_CROSSCOMPILING_EMULATOR)
  set(CPKT_TEST_EXECUTABLE_PREFIX ${CMAKE_CROSSCOMPILING_EMULATOR})
endif()
function(cpkt_add_repo_warning_errors target_name)
  if(NOT TARGET "${target_name}")
    return()
  endif()
  get_target_property(_cpkt_target_type ${target_name} TYPE)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(${target_name} PRIVATE
          -Wall
          -Wextra
          -Wpedantic
          -Werror
          "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=src"
          "-ffile-prefix-map=${CMAKE_BINARY_DIR}=build"
          "-fdebug-prefix-map=${CMAKE_SOURCE_DIR}=src"
          "-fdebug-prefix-map=${CMAKE_BINARY_DIR}=build")
    if(NOT _cpkt_target_type STREQUAL "STATIC_LIBRARY")
      if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        target_link_options(${target_name} PRIVATE LINKER:-fatal_warnings)
      else()
        target_link_options(${target_name} PRIVATE LINKER:--fatal-warnings)
      endif()
    endif()
  elseif(MSVC)
    target_compile_options(${target_name} PRIVATE /W4 /WX)
    if(NOT _cpkt_target_type STREQUAL "STATIC_LIBRARY")
      target_link_options(${target_name} PRIVATE LINKER:/WX)
    endif()
  endif()
endfunction()
function(cpkt_configure_c89_target target_name)
  if(NOT TARGET "${target_name}")
    return()
  endif()
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(${target_name} PRIVATE
          -std=c89
          -Wall
          -Wextra
          -Wpedantic
          -pedantic-errors)
  endif()
endfunction()
function(cpkt_apply_auth_export_catalog target_name catalog_name)
  if(NOT TARGET "${target_name}")
    return()
  endif()
  set(_catalog "${CMAKE_SOURCE_DIR}/cmake/exports/${catalog_name}.txt")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_catalog}")
  file(STRINGS "${_catalog}" _symbols REGEX "^cpkt_")
  if(NOT _symbols)
    message(FATAL_ERROR "empty authentication export catalog: ${_catalog}")
  endif()
  if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(_linker_file "${CMAKE_BINARY_DIR}/${catalog_name}.exports")
    set(_linker_contents "")
    foreach(_symbol IN LISTS _symbols)
      string(APPEND _linker_contents "_${_symbol}\n")
    endforeach()
    target_link_options(${target_name} PRIVATE
          "-Wl,-exported_symbols_list,${_linker_file}")
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_linker_file "${CMAKE_BINARY_DIR}/${catalog_name}.map")
    set(_linker_contents "{\n  global:\n")
    foreach(_symbol IN LISTS _symbols)
      string(APPEND _linker_contents "    ${_symbol};\n")
    endforeach()
    string(APPEND _linker_contents "  local: *;\n};\n")
    target_link_options(${target_name} PRIVATE
          "-Wl,--version-script,${_linker_file}" "-Wl,--exclude-libs,ALL")
  endif()
  if(DEFINED _linker_file)
    include("${CMAKE_SOURCE_DIR}/cmake/CpktMutationPaths.cmake")
    cpkt_register_generated_outputs("${_linker_file}")
    if(EXISTS "${_linker_file}")
      file(READ "${_linker_file}" _previous_linker_contents)
    endif()
    if(NOT "${_previous_linker_contents}" STREQUAL "${_linker_contents}")
      file(WRITE "${_linker_file}" "${_linker_contents}")
    endif()
    set_property(TARGET ${target_name} APPEND PROPERTY LINK_DEPENDS
          "${_linker_file}")
  endif()
endfunction()
function(cpkt_register_local_runtime_checks)
  get_property(_cpkt_local_tests DIRECTORY PROPERTY TESTS)
  foreach(_cpkt_test IN LISTS _cpkt_local_tests)
    get_test_property(${_cpkt_test} ENVIRONMENT _cpkt_test_environment)
    if(_cpkt_test_environment MATCHES "(^|;)LD_LIBRARY_PATH=")
      message(FATAL_ERROR "Native test ${_cpkt_test} exports LD_LIBRARY_PATH; use private executable runtime paths")
    endif()
  endforeach()
  get_property(_cpkt_local_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
  set(_cpkt_local_executables)
  foreach(_cpkt_target IN LISTS _cpkt_local_targets)
    get_target_property(_cpkt_type ${_cpkt_target} TYPE)
    if(_cpkt_type STREQUAL "EXECUTABLE")
      list(APPEND _cpkt_local_executables "$<TARGET_FILE:${_cpkt_target}>")
    endif()
  endforeach()
  set(_cpkt_runtime_policy_name local_executable_runtime_policy)
  if(NOT CPKT_GROUP STREQUAL "core")
    string(APPEND _cpkt_runtime_policy_name "_${CPKT_GROUP}")
  endif()
  cpkt_group_add_test(NAME ${_cpkt_runtime_policy_name}
      COMMAND bash "${CMAKE_SOURCE_DIR}/tests/local_executable_runtime_policy_test.sh"
        "${CPKT_RUNTIME_LOADER}" ${_cpkt_local_executables})
endfunction()
if(NOT CPKT_DEPENDENCY_PRODUCER)
  cmake_language(DEFER CALL cpkt_validate_owned_graph)
endif()
if(CPKT_BUILD_TESTS AND CPKT_NATIVE_BOOTLIN_RUNTIME)
  cmake_language(DEFER CALL cpkt_register_local_runtime_checks)
endif()
if(DEFINED CPKT_BUILD_STATIC AND NOT CPKT_BUILD_STATIC)
  message(FATAL_ERROR "cpkt release bundles always include static and shared artifacts; CPKT_BUILD_STATIC cannot be disabled.")
endif()
if(DEFINED CPKT_BUILD_SHARED AND NOT CPKT_BUILD_SHARED)
  message(FATAL_ERROR "cpkt release bundles always include static and shared artifacts; CPKT_BUILD_SHARED cannot be disabled.")
endif()
set(CPKT_BUILD_STATIC ON CACHE INTERNAL "cpkt release bundles always include static artifacts.")
set(CPKT_BUILD_SHARED ON CACHE INTERNAL "cpkt release bundles always include shared artifacts.")
set(CPKT_BUNDLE_VERSION "${PROJECT_VERSION}" CACHE STRING "Published dependency bundle version." FORCE)
set(CPKT_IODBC_VERSION "3.52.16" CACHE STRING "Pinned iODBC release.")
set(CPKT_POSTGRESQL_VERSION "18.6" CACHE STRING "Pinned PostgreSQL/libpq release.")
set(CPKT_SQLITE_VERSION "3.53.4" CACHE STRING "Pinned SQLite release.")
# ABI declarations live in cmake/CpktABIVersions.cmake.
include(cmake/CpktDependencyArchiveCache.cmake)
if(CPKT_BUILD_DEPENDENCIES OR CPKT_BUILD_TESTS)
  cpkt_initialize_dependency_cache()
endif()
set(CPKT_DEPENDENCY_CACHE_LOCK_TIMEOUT "120" CACHE STRING "Maximum shared dependency archive cache lock wait in seconds.")
set(CPKT_DOWNLOAD_ROOT "${CMAKE_SOURCE_DIR}/.cache/downloads" CACHE PATH "Local ExternalProject archive staging root.")
set(CPKT_DIST_DIR "${CMAKE_SOURCE_DIR}/dist" CACHE PATH "Output directory for package archives.")
set(CPKT_TARGET_ARCH "${CMAKE_SYSTEM_PROCESSOR}" CACHE STRING "Artifact architecture name.")
set(CPKT_TARGET_OS "${CMAKE_SYSTEM_NAME}" CACHE STRING "Artifact operating system name.")
set(CPKT_TARGET_LIBC "" CACHE STRING "Artifact libc suffix for Linux targets.")
set(CPKT_DEPENDENCY_BUILD_TYPE "Release")
set(CPKT_DEPENDENCY_BUILD_JOBS_DEFAULT 8)
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
  set(CPKT_DEPENDENCY_BUILD_JOBS_DEFAULT 2)
endif()
if(NOT CPKT_DEPENDENCY_BUILD_JOBS_DEFAULT OR CPKT_DEPENDENCY_BUILD_JOBS_DEFAULT LESS 1)
  set(CPKT_DEPENDENCY_BUILD_JOBS_DEFAULT 1)
endif()
set(CPKT_DEPENDENCY_BUILD_JOBS "${CPKT_DEPENDENCY_BUILD_JOBS_DEFAULT}" CACHE STRING "Parallel dependency build job count.")
set(_cpkt_job_limit 8)
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
  set(_cpkt_job_limit 2)
endif()
if(NOT CPKT_DEPENDENCY_BUILD_JOBS MATCHES "^[1-9][0-9]*$" OR
    CPKT_DEPENDENCY_BUILD_JOBS GREATER _cpkt_job_limit)
  message(FATAL_ERROR "CPKT_DEPENDENCY_BUILD_JOBS must be between 1 and ${_cpkt_job_limit} on this host")
endif()
set(CPKT_DEPENDENCY_DOWNLOAD_TIMEOUT "300" CACHE STRING "Maximum source archive download time in seconds.")
set(CPKT_DEPENDENCY_DOWNLOAD_INACTIVITY_TIMEOUT "60" CACHE STRING "Maximum source archive idle download time in seconds.")
set(CPKT_DEPENDENCY_DOWNLOAD_RETRIES "3" CACHE STRING "Bounded retry count for each dependency archive URL.")
string(TOLOWER "${CPKT_TARGET_OS}" CPKT_TARGET_OS_LOWER)
cpkt_normalize_arch("${CPKT_TARGET_ARCH}" CPKT_TARGET_ARCH_LOWER)
if(CPKT_TARGET_OS_LOWER STREQUAL "darwin" AND CPKT_TARGET_ARCH_LOWER STREQUAL "aarch64")
  set(CPKT_TARGET_ARCH_LOWER "arm64")
endif()
set(CPKT_TARGET_ARCH "${CPKT_TARGET_ARCH_LOWER}" CACHE STRING "Artifact architecture name." FORCE)
if(CPKT_TARGET_OS_LOWER STREQUAL "linux" AND NOT CPKT_TARGET_LIBC)
  execute_process(
      COMMAND "${CMAKE_C_COMPILER}" -dumpmachine
      OUTPUT_VARIABLE cpkt_compiler_machine
      RESULT_VARIABLE cpkt_compiler_machine_result
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
  if(cpkt_compiler_machine_result EQUAL 0 AND cpkt_compiler_machine MATCHES "musl")
    set(CPKT_TARGET_LIBC "musl")
  else()
    set(CPKT_TARGET_LIBC "gnu")
  endif()
endif()
if(CPKT_TARGET_OS_LOWER STREQUAL "darwin")
  set(CPKT_TARGET_ID "${CPKT_TARGET_ARCH_LOWER}-apple-darwin")
elseif(CPKT_TARGET_OS_LOWER STREQUAL "linux")
  set(CPKT_TARGET_ID "${CPKT_TARGET_ARCH_LOWER}-linux-${CPKT_TARGET_LIBC}")
else()
  set(CPKT_TARGET_ID "${CPKT_TARGET_ARCH_LOWER}-${CPKT_TARGET_OS_LOWER}")
endif()
set(CPKT_TARGET_ID "${CPKT_TARGET_ID}" CACHE INTERNAL "Resolved SDK target identity." FORCE)
set(CPKT_REGISTER_HOST_TESTS OFF)
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND ((CPKT_TARGET_ID STREQUAL "x86_64-linux-gnu" AND CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux") OR (CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")))
  set(CPKT_REGISTER_HOST_TESTS ON)
endif()
set(_cpkt_default_external_root "${CMAKE_SOURCE_DIR}/.cache/deps/${CPKT_TARGET_ID}")
set(_cpkt_default_dependency_build_root "${CMAKE_SOURCE_DIR}/.cache/deps-build/${CPKT_TARGET_ID}")
set(CPKT_DEPENDENCY_CONTRACT_ROOT "${CMAKE_SOURCE_DIR}/.cache/dependency-contracts" CACHE PATH "Target-local dependency build contract metadata.")
foreach(pair "CPKT_EXTERNAL_ROOT|${_cpkt_default_external_root}" "CPKT_DEPENDENCY_BUILD_ROOT|${_cpkt_default_dependency_build_root}"
    "CPKT_DEPENDENCY_CONTRACT_ROOT|${CMAKE_SOURCE_DIR}/.cache/dependency-contracts"
    "CPKT_DOWNLOAD_ROOT|${CMAKE_SOURCE_DIR}/.cache/downloads" "CPKT_DIST_DIR|${CMAKE_SOURCE_DIR}/dist")
  string(REPLACE "|" ";" pair "${pair}")
  list(GET pair 0 key)
  list(GET pair 1 owned)
  if(DEFINED ${key} AND NOT "${${key}}" STREQUAL "${owned}")
    message(FATAL_ERROR "${key} must stay in this checkout: ${owned}")
  endif()
  set(${key} "${owned}" CACHE PATH "Owned target-local dependency state" FORCE)
endforeach()
set(CPKT_EXTERNAL_ROOT_LIFECYCLE_OWNED ON)
set(CPKT_DEPENDENCY_BUILD_ROOT_LIFECYCLE_OWNED ON)
include(cmake/CpktComponentInventory.cmake)
cpkt_select_components(CPKT_ACTIVE_COMPONENTS)
include(cmake/CpktDependencies.cmake)
cpkt_configure_dependencies()
if(CPKT_DEPENDENCY_PRODUCER)
  return()
endif()
find_program(CPKT_HOST_PYTHON_EXECUTABLE NAMES python3 NO_CMAKE_FIND_ROOT_PATH REQUIRED)
find_program(CPKT_PYTHON3_EXECUTABLE NAMES python3 REQUIRED)
set(Python3_EXECUTABLE "${CPKT_PYTHON3_EXECUTABLE}")
cpkt_group_add_library(cpkt_postgres_static STATIC src/postgres.c src/postgres_events.c
  src/postgres_event_alloc.c)
cpkt_configure_c89_target(cpkt_postgres_static)
cpkt_add_repo_warning_errors(cpkt_postgres_static)
cpkt_group_target_command(target_include_directories cpkt_postgres_static
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>")
cpkt_group_target_command(target_link_libraries cpkt_postgres_static PUBLIC cpkt::postgresql_static PRIVATE Threads::Threads)
cpkt_group_target_command(target_compile_definitions cpkt_postgres_static PRIVATE CPKT_POSTGRES_FACADE_VERSION="${CPKT_POSTGRES_ABI_VERSION}")
cpkt_group_set_target_properties(cpkt_postgres_static PROPERTIES OUTPUT_NAME cpkt_postgres POSITION_INDEPENDENT_CODE ON)
cpkt_group_add_library(cpkt_postgres_shared SHARED src/postgres.c src/postgres_events.c
  src/postgres_event_alloc.c)
cpkt_apply_auth_export_catalog(cpkt_postgres_shared cpkt_postgres)
cpkt_configure_c89_target(cpkt_postgres_shared)
cpkt_add_repo_warning_errors(cpkt_postgres_shared)
cpkt_group_target_command(target_include_directories cpkt_postgres_shared
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>")
cpkt_group_target_command(target_link_libraries cpkt_postgres_shared PUBLIC cpkt::postgresql_shared PRIVATE Threads::Threads)
cpkt_group_target_command(target_compile_definitions cpkt_postgres_shared PRIVATE CPKT_POSTGRES_FACADE_VERSION="${CPKT_POSTGRES_ABI_VERSION}")
cpkt_group_set_target_properties(cpkt_postgres_shared PROPERTIES OUTPUT_NAME cpkt_postgres POSITION_INDEPENDENT_CODE ON SOVERSION "${CPKT_POSTGRES_ABI_VERSION}" VERSION "${PROJECT_VERSION}")
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  cpkt_group_set_target_properties(cpkt_postgres_shared PROPERTIES INSTALL_NAME_DIR "@rpath" BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path" BUILD_WITH_INSTALL_RPATH ON)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  cpkt_group_target_command(target_link_options cpkt_postgres_shared PRIVATE "-Wl,--exclude-libs,ALL")
  cpkt_group_set_target_properties(cpkt_postgres_shared PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN" BUILD_WITH_INSTALL_RPATH ON)
endif()
cpkt_group_add_library(cpkt_sqlite_static STATIC src/sqlite.c src/sqlite_native_views.c
  src/sqlite_native_methods.c)
cpkt_configure_c89_target(cpkt_sqlite_static)
cpkt_add_repo_warning_errors(cpkt_sqlite_static)
cpkt_group_target_command(target_include_directories cpkt_sqlite_static
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>")
cpkt_group_target_command(target_link_libraries cpkt_sqlite_static PUBLIC
  cpkt::sqlite_static Threads::Threads)
cpkt_group_target_command(target_compile_definitions cpkt_sqlite_static PRIVATE
  CPKT_SQLITE_FACADE_VERSION="${CPKT_SQLITE_ABI_VERSION}"
  SQLITE_ENABLE_CARRAY=1
  SQLITE_ENABLE_NORMALIZE=1
  SQLITE_ENABLE_PREUPDATE_HOOK=1
  SQLITE_ENABLE_SESSION=1)
cpkt_group_set_target_properties(cpkt_sqlite_static PROPERTIES OUTPUT_NAME cpkt_sqlite POSITION_INDEPENDENT_CODE ON)
cpkt_group_add_library(cpkt_sqlite_shared SHARED src/sqlite.c src/sqlite_native_views.c
  src/sqlite_native_methods.c)
cpkt_apply_auth_export_catalog(cpkt_sqlite_shared cpkt_sqlite)
cpkt_configure_c89_target(cpkt_sqlite_shared)
cpkt_add_repo_warning_errors(cpkt_sqlite_shared)
cpkt_group_target_command(target_include_directories cpkt_sqlite_shared
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>")
cpkt_group_target_command(target_link_libraries cpkt_sqlite_shared PUBLIC
  cpkt::sqlite_shared Threads::Threads)
cpkt_group_target_command(target_compile_definitions cpkt_sqlite_shared PRIVATE
  CPKT_SQLITE_FACADE_VERSION="${CPKT_SQLITE_ABI_VERSION}"
  SQLITE_ENABLE_CARRAY=1
  SQLITE_ENABLE_NORMALIZE=1
  SQLITE_ENABLE_PREUPDATE_HOOK=1
  SQLITE_ENABLE_SESSION=1)
cpkt_group_set_target_properties(cpkt_sqlite_shared PROPERTIES OUTPUT_NAME cpkt_sqlite POSITION_INDEPENDENT_CODE ON SOVERSION "${CPKT_SQLITE_ABI_VERSION}" VERSION "${PROJECT_VERSION}")
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  cpkt_group_set_target_properties(cpkt_sqlite_shared PROPERTIES INSTALL_NAME_DIR "@rpath" BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path" BUILD_WITH_INSTALL_RPATH ON)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  cpkt_group_set_target_properties(cpkt_sqlite_shared PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN" BUILD_WITH_INSTALL_RPATH ON)
endif()
if(CPKT_BUILD_TESTS)
  include(cmake/CpktPreflightTests.cmake)
  cpkt_register_preflight_tests("${CMAKE_SOURCE_DIR}")
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Fixture registration is shared in CpktPreflightTests.cmake.
    cpkt_group_set_tests_properties(sdk_runtime_loader PROPERTIES LABELS "packaging;smoke" TIMEOUT 30)
  endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CPKT_TARGET_LIBC STREQUAL "musl")
  else()
  endif()
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CPKT_TARGET_LIBC STREQUAL "musl")
  else()
  endif()
  cpkt_group_add_executable(cpkt_sqlite_vfs_wal_capability_test
      tests/sqlite_vfs_wal_capability_test.c tests/sqlite_vfs_handle_views_test.c)
  cpkt_group_use_local_runtime(cpkt_sqlite_vfs_wal_capability_test cpkt::sqlite_shared)
  cpkt_group_target_command(target_compile_options cpkt_sqlite_vfs_wal_capability_test PRIVATE -std=c99)
  set_source_files_properties(tests/sqlite_vfs_handle_views_test.c PROPERTIES COMPILE_OPTIONS "-std=c89;-pedantic-errors")
  cpkt_add_repo_warning_errors(cpkt_sqlite_vfs_wal_capability_test)
  cpkt_group_target_command(target_link_libraries cpkt_sqlite_vfs_wal_capability_test PRIVATE
      cpkt_sqlite_static cpkt::sqlite_static)
  cpkt_group_add_executable(cpkt_sqlite_vfs_handles_shared_test
      tests/sqlite_vfs_wal_capability_test.c tests/sqlite_vfs_handle_views_test.c)
  cpkt_group_use_local_runtime(cpkt_sqlite_vfs_handles_shared_test cpkt::sqlite_shared)
  cpkt_group_target_command(target_compile_options cpkt_sqlite_vfs_handles_shared_test PRIVATE -std=c99)
  cpkt_add_repo_warning_errors(cpkt_sqlite_vfs_handles_shared_test)
  cpkt_group_target_command(target_link_libraries cpkt_sqlite_vfs_handles_shared_test PRIVATE
      cpkt_sqlite_shared cpkt::sqlite_shared)
  if(CPKT_CAN_RUN_TARGET_EXECUTABLES)
    foreach(_cpkt_vfs_case IN ITEMS borrowed-file provider-identity)
      string(REPLACE "-" "_" _cpkt_vfs_test_name "${_cpkt_vfs_case}")
      cpkt_group_add_test(NAME sqlite_vfs_${_cpkt_vfs_test_name}
              COMMAND cpkt_sqlite_vfs_wal_capability_test
                "${CMAKE_BINARY_DIR}/sqlite-vfs-${_cpkt_vfs_case}.db" "${_cpkt_vfs_case}")
      cpkt_group_set_tests_properties(sqlite_vfs_${_cpkt_vfs_test_name} PROPERTIES LABELS "facade;memcheck" TIMEOUT 30)
      cpkt_group_add_test(NAME sqlite_vfs_${_cpkt_vfs_test_name}_shared
              COMMAND cpkt_sqlite_vfs_handles_shared_test
                "${CMAKE_BINARY_DIR}/sqlite-vfs-${_cpkt_vfs_case}-shared.db" "${_cpkt_vfs_case}")
      cpkt_group_set_tests_properties(sqlite_vfs_${_cpkt_vfs_test_name}_shared PROPERTIES LABELS "facade;memcheck" TIMEOUT 30)
    endforeach()
  endif()
  foreach(_cpkt_pending_linkage IN ITEMS static shared)
    cpkt_group_add_executable(cpkt_postgres_pending_registration_${_cpkt_pending_linkage}_test
          tests/postgres_pending_registration_test.c
          tests/postgres_tracking_lifetime_test.c
          tests/postgres_notice_registration_test.c)
    cpkt_group_use_local_runtime(cpkt_postgres_pending_registration_${_cpkt_pending_linkage}_test
          cpkt::postgresql_shared cpkt::openssl_ssl_shared cpkt::openssl_crypto_shared
          cpkt::gssapi_krb5_shared cpkt::openldap_shared cpkt::cyrus_sasl_shared
          cpkt::curl_shared cpkt::nghttp2_shared cpkt::libssh2_shared cpkt::zlib_shared)
    cpkt_configure_c89_target(cpkt_postgres_pending_registration_${_cpkt_pending_linkage}_test)
    cpkt_add_repo_warning_errors(cpkt_postgres_pending_registration_${_cpkt_pending_linkage}_test)
    cpkt_group_target_command(target_link_libraries cpkt_postgres_pending_registration_${_cpkt_pending_linkage}_test
          PRIVATE cpkt_postgres_${_cpkt_pending_linkage})
    if(CPKT_CAN_RUN_TARGET_EXECUTABLES)
      cpkt_group_add_test(NAME postgres_pending_registration_${_cpkt_pending_linkage}
              COMMAND cpkt_postgres_pending_registration_${_cpkt_pending_linkage}_test)
      cpkt_group_set_tests_properties(postgres_pending_registration_${_cpkt_pending_linkage} PROPERTIES LABELS "facade;memcheck" TIMEOUT 30)
    endif()
  endforeach()
  if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")

  else()
    cpkt_group_add_executable(cpkt_postgres_receiver_test tests/postgres_receiver_test.c)
    cpkt_group_use_local_runtime(cpkt_postgres_receiver_test)
    cpkt_group_target_command(target_compile_options cpkt_postgres_receiver_test PRIVATE -std=c89)
    cpkt_add_repo_warning_errors(cpkt_postgres_receiver_test)
    cpkt_group_target_command(target_link_libraries cpkt_postgres_receiver_test PRIVATE cpkt_postgres_static)
    cpkt_group_add_executable(cpkt_postgres_diagnostic_test
          tests/postgres_diagnostic_test.c)
    cpkt_group_use_local_runtime(cpkt_postgres_diagnostic_test)
    cpkt_configure_c89_target(cpkt_postgres_diagnostic_test)
    cpkt_add_repo_warning_errors(cpkt_postgres_diagnostic_test)
    cpkt_group_target_command(target_link_libraries cpkt_postgres_diagnostic_test PRIVATE
          cpkt_postgres_static)
    cpkt_group_add_executable(cpkt_postgres_events_test tests/postgres_events_test.c)
    cpkt_group_use_local_runtime(cpkt_postgres_events_test)
    cpkt_configure_c89_target(cpkt_postgres_events_test)
    cpkt_add_repo_warning_errors(cpkt_postgres_events_test)
    cpkt_group_target_command(target_link_libraries cpkt_postgres_events_test PRIVATE cpkt_postgres_static)
    cpkt_group_add_executable(cpkt_postgres_nested_copy_notice_test
          tests/postgres_nested_copy_notice_test.c)
    cpkt_group_use_local_runtime(cpkt_postgres_nested_copy_notice_test)
    cpkt_configure_c89_target(cpkt_postgres_nested_copy_notice_test)
    cpkt_add_repo_warning_errors(cpkt_postgres_nested_copy_notice_test)
    cpkt_group_target_command(target_link_libraries cpkt_postgres_nested_copy_notice_test PRIVATE
          cpkt_postgres_static)
    cpkt_group_add_executable(cpkt_postgres_event_concurrent_release_test
          tests/postgres_event_concurrent_release_test.c)
    cpkt_group_use_local_runtime(cpkt_postgres_event_concurrent_release_test)
    cpkt_configure_c89_target(cpkt_postgres_event_concurrent_release_test)
    cpkt_add_repo_warning_errors(cpkt_postgres_event_concurrent_release_test)
    cpkt_group_target_command(target_link_libraries cpkt_postgres_event_concurrent_release_test PRIVATE
          cpkt_postgres_static Threads::Threads)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      cpkt_group_add_executable(cpkt_postgres_notice_event_lifetime_test
              tests/postgres_notice_event_lifetime_test.c)
      cpkt_group_use_local_runtime(cpkt_postgres_notice_event_lifetime_test)
      cpkt_configure_c89_target(cpkt_postgres_notice_event_lifetime_test)
      cpkt_add_repo_warning_errors(cpkt_postgres_notice_event_lifetime_test)
      cpkt_group_target_command(target_include_directories cpkt_postgres_notice_event_lifetime_test PRIVATE
              "$<TARGET_PROPERTY:cpkt::postgresql_static,INTERFACE_INCLUDE_DIRECTORIES>")
      cpkt_group_target_command(target_link_options cpkt_postgres_notice_event_lifetime_test PRIVATE
              "-Wl,--wrap=PQsetNoticeReceiver"
              "-Wl,--wrap=cpkt_postgres_event_allocate")
      cpkt_group_target_command(target_link_libraries cpkt_postgres_notice_event_lifetime_test PRIVATE
              cpkt_postgres_static)
      cpkt_group_add_executable(cpkt_postgres_event_allocation_test
              tests/postgres_event_allocation_test.c
              tests/postgres_tracking_lifetime_test.c
              tests/postgres_notice_registration_test.c)
      cpkt_group_use_local_runtime(cpkt_postgres_event_allocation_test)
      cpkt_configure_c89_target(cpkt_postgres_event_allocation_test)
      cpkt_add_repo_warning_errors(cpkt_postgres_event_allocation_test)
      cpkt_group_target_command(target_link_options cpkt_postgres_event_allocation_test PRIVATE
              "-Wl,--wrap=cpkt_postgres_event_allocate" "-Wl,--wrap=malloc"
              "-Wl,--wrap=calloc")
      cpkt_group_target_command(target_compile_definitions cpkt_postgres_event_allocation_test PRIVATE
              CPKT_POSTGRES_TRACKING_FAULT_INJECTION=1
              CPKT_POSTGRES_NOTICE_FAULT_INJECTION=1)
      cpkt_group_target_command(target_link_libraries cpkt_postgres_event_allocation_test PRIVATE
              cpkt_postgres_static)
      cpkt_group_add_executable(cpkt_postgres_result_retrieval_native_peer
              tests/postgres_result_retrieval_native_peer.c)
      cpkt_group_use_local_runtime(cpkt_postgres_result_retrieval_native_peer)
      cpkt_add_repo_warning_errors(cpkt_postgres_result_retrieval_native_peer)
      cpkt_group_target_command(target_include_directories cpkt_postgres_result_retrieval_native_peer PRIVATE
              "$<TARGET_PROPERTY:cpkt::postgresql_static,INTERFACE_INCLUDE_DIRECTORIES>")
      cpkt_group_target_command(target_link_options cpkt_postgres_result_retrieval_native_peer PRIVATE
              "-Wl,--wrap=PQgetResult" "-Wl,--wrap=PQstatus"
              "-Wl,--wrap=cpkt_postgres_result_binding_allocate"
              "-Wl,--wrap=cpkt_postgres_event_prepare_result")
      cpkt_group_target_command(target_link_libraries cpkt_postgres_result_retrieval_native_peer PRIVATE
              cpkt_postgres_static)
      cpkt_group_add_executable(cpkt_postgres_event_address_reuse_test
              tests/postgres_event_address_reuse_test.c)
      cpkt_group_use_local_runtime(cpkt_postgres_event_address_reuse_test)
      cpkt_configure_c89_target(cpkt_postgres_event_address_reuse_test)
      cpkt_add_repo_warning_errors(cpkt_postgres_event_address_reuse_test)
      cpkt_group_target_command(target_include_directories cpkt_postgres_event_address_reuse_test PRIVATE
              "$<TARGET_PROPERTY:cpkt::postgresql_static,INTERFACE_INCLUDE_DIRECTORIES>")
      cpkt_group_target_command(target_link_options cpkt_postgres_event_address_reuse_test PRIVATE
              "-Wl,--wrap=PQclear" "-Wl,--wrap=PQmakeEmptyPGresult"
              "-Wl,--wrap=malloc" "-Wl,--wrap=free")
      cpkt_group_target_command(target_link_libraries cpkt_postgres_event_address_reuse_test PRIVATE
              cpkt_postgres_static)
      cpkt_group_add_executable(cpkt_postgres_oauth_cleanup_test
              tests/postgres_oauth_cleanup_test.c)
      cpkt_group_use_local_runtime(cpkt_postgres_oauth_cleanup_test)
      cpkt_configure_c89_target(cpkt_postgres_oauth_cleanup_test)
      cpkt_add_repo_warning_errors(cpkt_postgres_oauth_cleanup_test)
      cpkt_group_target_command(target_link_options cpkt_postgres_oauth_cleanup_test PRIVATE
              "-Wl,--wrap=calloc")
      cpkt_group_target_command(target_link_libraries cpkt_postgres_oauth_cleanup_test PRIVATE
              cpkt_postgres_static cpkt::postgresql_static)
    endif()
    cpkt_group_add_executable(cpkt_postgres_integration_test tests/postgres_integration_test.c)
    cpkt_group_use_local_runtime(cpkt_postgres_integration_test)
    cpkt_group_target_command(target_compile_options cpkt_postgres_integration_test PRIVATE -std=c89)
    cpkt_add_repo_warning_errors(cpkt_postgres_integration_test)
    cpkt_group_target_command(target_link_libraries cpkt_postgres_integration_test PRIVATE cpkt_postgres_static)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      cpkt_group_target_command(target_compile_definitions cpkt_postgres_integration_test PRIVATE
              CPKT_POSTGRES_RETRIEVAL_E2E_FAULT=1)
      cpkt_group_target_command(target_link_options cpkt_postgres_integration_test PRIVATE
              "-Wl,--wrap=cpkt_postgres_result_binding_allocate")
    endif()
    cpkt_group_add_executable(cpkt_postgres_oauth_module_test
          tests/postgres_oauth_module_test.c)
    if(CPKT_BUILD_DEPENDENCIES)
      cpkt_group_target_command(add_dependencies cpkt_postgres_oauth_module_test cpkt_postgresql_project)
    endif()
    cpkt_group_use_local_runtime(cpkt_postgres_oauth_module_test)
    cpkt_group_target_command(target_compile_options cpkt_postgres_oauth_module_test PRIVATE -std=c89)
    cpkt_add_repo_warning_errors(cpkt_postgres_oauth_module_test)
    cpkt_group_target_command(target_link_libraries cpkt_postgres_oauth_module_test PRIVATE
          ${CMAKE_DL_LIBS} cpkt::postgresql_shared cpkt::curl_shared)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      set(_cpkt_postgresql_oauth_staging_paths)
      foreach(_cpkt_postgresql_oauth_dependency IN ITEMS
                cpkt::curl_shared cpkt::zlib_shared cpkt::openssl_ssl_shared
                cpkt::libssh2_shared cpkt::nghttp2_shared cpkt::gssapi_krb5_shared
                cpkt::openldap_shared cpkt::cyrus_sasl_shared)
        cpkt_group_get_target_property(_cpkt_postgresql_oauth_dependency_location
                  ${_cpkt_postgresql_oauth_dependency} IMPORTED_LOCATION)
        get_filename_component(_cpkt_postgresql_oauth_dependency_dir
                  "${_cpkt_postgresql_oauth_dependency_location}" DIRECTORY)
        list(APPEND _cpkt_postgresql_oauth_staging_paths
                  "${_cpkt_postgresql_oauth_dependency_dir}")
      endforeach()
      list(JOIN _cpkt_postgresql_oauth_staging_paths ":"
              _cpkt_postgresql_oauth_staging_rpath)
      cpkt_group_target_command(target_link_options cpkt_postgres_oauth_module_test PRIVATE
              LINKER:--disable-new-dtags
              "LINKER:-rpath,${_cpkt_postgresql_oauth_staging_rpath}")
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      if(CPKT_TARGET_ID STREQUAL "armhf-linux-musl")
      else()
      endif()
    endif()
    cpkt_group_get_target_property(cpkt_sasl_native_include_dir cpkt::cyrus_sasl_static
          INTERFACE_INCLUDE_DIRECTORIES)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    endif()
    cpkt_group_add_executable(cpkt_iodbc_static_test tests/iodbc_driver_manager_test.c)
    cpkt_group_use_local_runtime(cpkt_iodbc_static_test)
    cpkt_configure_c89_target(cpkt_iodbc_static_test)
    cpkt_add_repo_warning_errors(cpkt_iodbc_static_test)
    cpkt_group_target_command(target_link_libraries cpkt_iodbc_static_test PRIVATE cpkt::iodbc)
    cpkt_group_add_executable(cpkt_iodbc_shared_test tests/iodbc_driver_manager_test.c)
    cpkt_group_use_local_runtime(cpkt_iodbc_shared_test)
    cpkt_configure_c89_target(cpkt_iodbc_shared_test)
    cpkt_add_repo_warning_errors(cpkt_iodbc_shared_test)
    cpkt_group_target_command(target_link_libraries cpkt_iodbc_shared_test PRIVATE
          cpkt::iodbc_shared cpkt::iodbcinst_shared)
    cpkt_group_add_executable(cpkt_sqlite_facade_test tests/sqlite_facade_test.c)
    cpkt_group_use_local_runtime(cpkt_sqlite_facade_test)
    cpkt_group_target_command(target_compile_options cpkt_sqlite_facade_test PRIVATE -std=c89)
    cpkt_add_repo_warning_errors(cpkt_sqlite_facade_test)
    cpkt_group_target_command(target_link_libraries cpkt_sqlite_facade_test PRIVATE cpkt_sqlite_static)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      cpkt_group_add_executable(cpkt_sqlite_module_adapter_fault_test
              tests/sqlite_module_adapter_fault_test.c)
      cpkt_group_use_local_runtime(cpkt_sqlite_module_adapter_fault_test)
      cpkt_configure_c89_target(cpkt_sqlite_module_adapter_fault_test)
      cpkt_add_repo_warning_errors(cpkt_sqlite_module_adapter_fault_test)
      cpkt_group_target_command(target_link_options cpkt_sqlite_module_adapter_fault_test PRIVATE
              "-Wl,--wrap=calloc" "-Wl,--wrap=free")
      cpkt_group_target_command(target_link_libraries cpkt_sqlite_module_adapter_fault_test PRIVATE
              cpkt_sqlite_static)
    endif()
    cpkt_group_add_executable(cpkt_sqlite_log_native_peer_test
          tests/sqlite_log_native_peer_test.c)
    cpkt_group_use_local_runtime(cpkt_sqlite_log_native_peer_test)
    cpkt_group_target_command(target_compile_options cpkt_sqlite_log_native_peer_test PRIVATE -std=c99)
    cpkt_add_repo_warning_errors(cpkt_sqlite_log_native_peer_test)
    cpkt_group_target_command(target_link_libraries cpkt_sqlite_log_native_peer_test PRIVATE
          cpkt_sqlite_static cpkt::sqlite_static)
    cpkt_group_add_executable(cpkt_sqlite_error_code_native_peer_test
          tests/sqlite_error_code_native_peer_test.c)
    cpkt_group_use_local_runtime(cpkt_sqlite_error_code_native_peer_test)
    cpkt_group_target_command(target_compile_options cpkt_sqlite_error_code_native_peer_test PRIVATE -std=c99)
    cpkt_add_repo_warning_errors(cpkt_sqlite_error_code_native_peer_test)
    cpkt_group_target_command(target_link_libraries cpkt_sqlite_error_code_native_peer_test PRIVATE
          cpkt_sqlite_static cpkt::sqlite_static)
    cpkt_group_add_library(cpkt_sqlite_loadable_extension MODULE
          tests/sqlite_loadable_extension.c)
    cpkt_group_target_command(target_compile_options cpkt_sqlite_loadable_extension PRIVATE -std=c99)
    cpkt_add_repo_warning_errors(cpkt_sqlite_loadable_extension)
    cpkt_group_target_command(target_include_directories cpkt_sqlite_loadable_extension PRIVATE
          "$<TARGET_PROPERTY:cpkt::sqlite_static,INTERFACE_INCLUDE_DIRECTORIES>")
    cpkt_group_target_command(add_dependencies cpkt_sqlite_loadable_extension cpkt_sqlite_static)
    foreach(_sqlite_consumer_variant IN ITEMS static shared)
      cpkt_group_add_executable(cpkt_sqlite_loadable_extension_${_sqlite_consumer_variant}_test
              tests/sqlite_loadable_extension_test.c)
      cpkt_group_use_local_runtime(cpkt_sqlite_loadable_extension_${_sqlite_consumer_variant}_test)
      cpkt_configure_c89_target(cpkt_sqlite_loadable_extension_${_sqlite_consumer_variant}_test)
      cpkt_add_repo_warning_errors(cpkt_sqlite_loadable_extension_${_sqlite_consumer_variant}_test)
      cpkt_group_target_command(target_link_libraries cpkt_sqlite_loadable_extension_${_sqlite_consumer_variant}_test
              PRIVATE cpkt_sqlite_${_sqlite_consumer_variant})
    endforeach()
    cpkt_group_add_executable(cpkt_sqlite_rebase_collection_test
          tests/sqlite_rebase_collection_test.c)
    cpkt_group_use_local_runtime(cpkt_sqlite_rebase_collection_test)
    cpkt_group_target_command(target_compile_options cpkt_sqlite_rebase_collection_test PRIVATE -std=c89)
    cpkt_add_repo_warning_errors(cpkt_sqlite_rebase_collection_test)
    cpkt_group_target_command(target_link_libraries cpkt_sqlite_rebase_collection_test PRIVATE
          cpkt_sqlite_static)
    cpkt_group_add_executable(cpkt_sqlite_changeset_null_conflict_test
          tests/sqlite_changeset_null_conflict_test.c)
    cpkt_group_use_local_runtime(cpkt_sqlite_changeset_null_conflict_test)
    cpkt_configure_c89_target(cpkt_sqlite_changeset_null_conflict_test)
    cpkt_add_repo_warning_errors(cpkt_sqlite_changeset_null_conflict_test)
    cpkt_group_target_command(target_link_libraries cpkt_sqlite_changeset_null_conflict_test PRIVATE
          cpkt_sqlite_static)
    cpkt_group_add_executable(cpkt_sqlite_child_lifetime_concurrency_test
          tests/sqlite_child_lifetime_concurrency_test.c)
    cpkt_group_use_local_runtime(cpkt_sqlite_child_lifetime_concurrency_test)
    cpkt_group_target_command(target_compile_options cpkt_sqlite_child_lifetime_concurrency_test PRIVATE -std=c89)
    cpkt_add_repo_warning_errors(cpkt_sqlite_child_lifetime_concurrency_test)
    cpkt_group_target_command(target_link_libraries cpkt_sqlite_child_lifetime_concurrency_test PRIVATE
          cpkt_sqlite_static Threads::Threads)
    cpkt_group_add_executable(cpkt_sqlite_callback_synchronization_test
          tests/sqlite_callback_synchronization_test.c)
    cpkt_group_use_local_runtime(cpkt_sqlite_callback_synchronization_test)
    cpkt_group_target_command(target_compile_options cpkt_sqlite_callback_synchronization_test
          PRIVATE -std=c89)
    cpkt_add_repo_warning_errors(cpkt_sqlite_callback_synchronization_test)
    cpkt_group_target_command(target_link_libraries cpkt_sqlite_callback_synchronization_test PRIVATE
          cpkt_sqlite_static Threads::Threads)
    cpkt_group_add_executable(cpkt_sqlite_registration_concurrency_test
          tests/sqlite_registration_concurrency_test.c)
    cpkt_group_use_local_runtime(cpkt_sqlite_registration_concurrency_test)
    cpkt_configure_c89_target(cpkt_sqlite_registration_concurrency_test)
    cpkt_add_repo_warning_errors(cpkt_sqlite_registration_concurrency_test)
    cpkt_group_target_command(target_link_libraries cpkt_sqlite_registration_concurrency_test PRIVATE
          cpkt_sqlite_static Threads::Threads)
    cpkt_group_add_executable(cpkt_sqlite_fts5_registration_concurrency_test
          tests/sqlite_fts5_registration_concurrency_test.c)
    cpkt_group_use_local_runtime(cpkt_sqlite_fts5_registration_concurrency_test)
    cpkt_configure_c89_target(cpkt_sqlite_fts5_registration_concurrency_test)
    cpkt_add_repo_warning_errors(cpkt_sqlite_fts5_registration_concurrency_test)
    cpkt_group_target_command(target_link_libraries cpkt_sqlite_fts5_registration_concurrency_test PRIVATE
          cpkt_sqlite_static Threads::Threads)
    cpkt_group_add_executable(cpkt_sqlite_window_aggregate_test
          tests/sqlite_window_aggregate_test.c)
    cpkt_group_use_local_runtime(cpkt_sqlite_window_aggregate_test)
    cpkt_configure_c89_target(cpkt_sqlite_window_aggregate_test)
    cpkt_add_repo_warning_errors(cpkt_sqlite_window_aggregate_test)
    cpkt_group_target_command(target_link_libraries cpkt_sqlite_window_aggregate_test PRIVATE
          cpkt_sqlite_static)
    cpkt_group_add_executable(cpkt_sqlite_unlock_notify_groups_test
          tests/sqlite_unlock_notify_groups_test.c)
    cpkt_group_use_local_runtime(cpkt_sqlite_unlock_notify_groups_test)
    cpkt_configure_c89_target(cpkt_sqlite_unlock_notify_groups_test)
    cpkt_add_repo_warning_errors(cpkt_sqlite_unlock_notify_groups_test)
    cpkt_group_target_command(target_link_libraries cpkt_sqlite_unlock_notify_groups_test PRIVATE
          cpkt_sqlite_static)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      cpkt_group_add_executable(cpkt_sqlite_unlock_notify_race_test
              tests/sqlite_unlock_notify_race_test.c)
      cpkt_group_use_local_runtime(cpkt_sqlite_unlock_notify_race_test)
      cpkt_configure_c89_target(cpkt_sqlite_unlock_notify_race_test)
      cpkt_add_repo_warning_errors(cpkt_sqlite_unlock_notify_race_test)
      cpkt_group_target_command(target_link_options cpkt_sqlite_unlock_notify_race_test PRIVATE
              "-Wl,--wrap=sqlite3_unlock_notify")
      cpkt_group_target_command(target_link_libraries cpkt_sqlite_unlock_notify_race_test PRIVATE
              cpkt_sqlite_static Threads::Threads)
      cpkt_group_add_executable(cpkt_sqlite_auto_extension_lock_order_test
              tests/sqlite_auto_extension_lock_order_test.c)
      cpkt_group_use_local_runtime(cpkt_sqlite_auto_extension_lock_order_test)
      cpkt_configure_c89_target(cpkt_sqlite_auto_extension_lock_order_test)
      cpkt_add_repo_warning_errors(cpkt_sqlite_auto_extension_lock_order_test)
      cpkt_group_target_command(target_link_options cpkt_sqlite_auto_extension_lock_order_test PRIVATE
              "-Wl,--wrap=sqlite3_auto_extension"
              "-Wl,--wrap=sqlite3_cancel_auto_extension")
      cpkt_group_target_command(target_link_libraries cpkt_sqlite_auto_extension_lock_order_test PRIVATE
              cpkt_sqlite_static Threads::Threads)
    endif()
    cpkt_group_add_executable(cpkt_sqlite_page_cache_lifetime_test
          tests/sqlite_page_cache_lifetime_test.c)
    cpkt_group_use_local_runtime(cpkt_sqlite_page_cache_lifetime_test)
    cpkt_group_target_command(target_compile_options cpkt_sqlite_page_cache_lifetime_test PRIVATE -std=c89)
    cpkt_add_repo_warning_errors(cpkt_sqlite_page_cache_lifetime_test)
    cpkt_group_target_command(target_link_libraries cpkt_sqlite_page_cache_lifetime_test PRIVATE
          cpkt_sqlite_static)
    cpkt_group_add_executable(cpkt_sqlite_native_views_test
          tests/sqlite_native_views_test.c)
    cpkt_group_use_local_runtime(cpkt_sqlite_native_views_test)
    cpkt_configure_c89_target(cpkt_sqlite_native_views_test)
    cpkt_add_repo_warning_errors(cpkt_sqlite_native_views_test)
    cpkt_group_target_command(target_link_libraries cpkt_sqlite_native_views_test PRIVATE
          cpkt_sqlite_static)
    cpkt_group_add_executable(cpkt_sqlite_vfs_native_ownership_test
          tests/sqlite_vfs_native_ownership_test.c)
    cpkt_group_use_local_runtime(cpkt_sqlite_vfs_native_ownership_test)
    cpkt_configure_c89_target(cpkt_sqlite_vfs_native_ownership_test)
    cpkt_add_repo_warning_errors(cpkt_sqlite_vfs_native_ownership_test)
    cpkt_group_target_command(target_link_libraries cpkt_sqlite_vfs_native_ownership_test PRIVATE
          cpkt_sqlite_static cpkt::sqlite_static)
    cpkt_group_add_executable(cpkt_sqlite_rebaser_empty_test
          tests/sqlite_rebaser_empty_test.c)
    cpkt_group_use_local_runtime(cpkt_sqlite_rebaser_empty_test)
    cpkt_configure_c89_target(cpkt_sqlite_rebaser_empty_test)
    cpkt_add_repo_warning_errors(cpkt_sqlite_rebaser_empty_test)
    cpkt_group_target_command(target_link_libraries cpkt_sqlite_rebaser_empty_test PRIVATE
          cpkt_sqlite_static)
    cpkt_group_add_executable(cpkt_sqlite_completion_test
          tests/sqlite_completion_test.c)
    cpkt_group_use_local_runtime(cpkt_sqlite_completion_test)
    cpkt_configure_c89_target(cpkt_sqlite_completion_test)
    cpkt_add_repo_warning_errors(cpkt_sqlite_completion_test)
    cpkt_group_target_command(target_link_libraries cpkt_sqlite_completion_test PRIVATE
          cpkt_sqlite_static)
    cpkt_group_add_executable(cpkt_sqlite_stream_failure_test
          tests/sqlite_stream_failure_test.c)
    cpkt_group_use_local_runtime(cpkt_sqlite_stream_failure_test)
    cpkt_configure_c89_target(cpkt_sqlite_stream_failure_test)
    cpkt_add_repo_warning_errors(cpkt_sqlite_stream_failure_test)
    cpkt_group_target_command(target_link_libraries cpkt_sqlite_stream_failure_test PRIVATE
          cpkt_sqlite_static)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      cpkt_group_add_executable(cpkt_sqlite_temp_directory_test
              tests/sqlite_temp_directory_test.c
              tests/sqlite_temp_directory_native_peer.c)
      cpkt_group_use_local_runtime(cpkt_sqlite_temp_directory_test)
      cpkt_add_repo_warning_errors(cpkt_sqlite_temp_directory_test)
      set_source_files_properties(tests/sqlite_temp_directory_test.c PROPERTIES COMPILE_OPTIONS "-std=c89;-pedantic-errors")
      cpkt_group_target_command(target_link_options cpkt_sqlite_temp_directory_test PRIVATE
              "-Wl,--wrap=sqlite3_mprintf")
      cpkt_group_target_command(target_link_libraries cpkt_sqlite_temp_directory_test PRIVATE
              cpkt_sqlite_static)
    endif()
    cpkt_group_add_executable(cpkt_sqlite_auto_extension_shutdown_test
          tests/sqlite_auto_extension_shutdown_test.c)
    cpkt_group_use_local_runtime(cpkt_sqlite_auto_extension_shutdown_test)
    cpkt_group_target_command(target_compile_options cpkt_sqlite_auto_extension_shutdown_test PRIVATE -std=c89)
    cpkt_add_repo_warning_errors(cpkt_sqlite_auto_extension_shutdown_test)
    cpkt_group_target_command(target_link_libraries cpkt_sqlite_auto_extension_shutdown_test PRIVATE
          cpkt_sqlite_static)
    cpkt_group_add_executable(cpkt_sqlite_rtree_registration_failure_test
          tests/sqlite_rtree_registration_failure_test.c)
    cpkt_group_use_local_runtime(cpkt_sqlite_rtree_registration_failure_test)
    cpkt_group_target_command(target_compile_options cpkt_sqlite_rtree_registration_failure_test PRIVATE -std=c89)
    cpkt_add_repo_warning_errors(cpkt_sqlite_rtree_registration_failure_test)
    cpkt_group_target_command(target_link_libraries cpkt_sqlite_rtree_registration_failure_test PRIVATE
          cpkt_sqlite_static)
    cpkt_group_add_executable(cpkt_sqlite_session_hook_guard_test
          tests/sqlite_session_hook_guard_test.c)
    cpkt_group_use_local_runtime(cpkt_sqlite_session_hook_guard_test)
    cpkt_group_target_command(target_compile_options cpkt_sqlite_session_hook_guard_test PRIVATE -std=c89)
    cpkt_add_repo_warning_errors(cpkt_sqlite_session_hook_guard_test)
    cpkt_group_target_command(target_link_libraries cpkt_sqlite_session_hook_guard_test PRIVATE
          cpkt_sqlite_static)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      cpkt_group_add_executable(cpkt_sqlite_page_cache_discard_test
              tests/sqlite_page_cache_discard_test.c)
      cpkt_group_use_local_runtime(cpkt_sqlite_page_cache_discard_test)
      cpkt_group_target_command(target_compile_options cpkt_sqlite_page_cache_discard_test PRIVATE -std=c99)
      cpkt_group_target_command(target_link_options cpkt_sqlite_page_cache_discard_test PRIVATE
              "-Wl,--wrap=calloc" "-Wl,--wrap=free")
      cpkt_add_repo_warning_errors(cpkt_sqlite_page_cache_discard_test)
      cpkt_group_target_command(target_link_libraries cpkt_sqlite_page_cache_discard_test PRIVATE
              cpkt_sqlite_static cpkt::sqlite_static)
    endif()
    cpkt_group_add_executable(cpkt_sqlite_collation_failure_test
          tests/sqlite_collation_failure_test.c)
    cpkt_group_use_local_runtime(cpkt_sqlite_collation_failure_test)
    cpkt_group_target_command(target_compile_options cpkt_sqlite_collation_failure_test PRIVATE -std=c89)
    cpkt_add_repo_warning_errors(cpkt_sqlite_collation_failure_test)
    cpkt_group_target_command(target_link_libraries cpkt_sqlite_collation_failure_test PRIVATE
          cpkt_sqlite_static)
  endif()
  set(_cpkt_fixture_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/opcua-fixture")
  if(CPKT_CAN_RUN_TARGET_EXECUTABLES)
    set(_cpkt_curl_harness_runner)
    foreach(_cpkt_runner_argument IN LISTS CPKT_TEST_EXECUTABLE_PREFIX)
      list(APPEND _cpkt_curl_harness_runner "--runner=${_cpkt_runner_argument}")
    endforeach()
    # Fixture registration is shared in CpktPreflightTests.cmake.
    # Fixture registration is shared in CpktPreflightTests.cmake.
    cpkt_group_set_tests_properties(patch_series_portability postgresql_probe_rpaths PROPERTIES LABELS "packaging;smoke" TIMEOUT 30)

    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    elseif(NOT CPKT_NATIVE_BOOTLIN_RUNTIME)
    endif()
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
      cpkt_group_add_test(NAME postgres_receiver_shell COMMAND cpkt_postgres_receiver_test)
      cpkt_group_add_test(NAME postgres_diagnostic
              COMMAND cpkt_postgres_diagnostic_test)
      cpkt_group_set_tests_properties(postgres_diagnostic PROPERTIES LABELS "facade;memcheck")
      cpkt_group_add_test(NAME postgres_events COMMAND cpkt_postgres_events_test)
      cpkt_group_add_test(NAME postgres_nested_copy_notice
              COMMAND cpkt_postgres_nested_copy_notice_test)
      cpkt_group_set_tests_properties(postgres_nested_copy_notice PROPERTIES TIMEOUT 30)
      cpkt_group_add_test(NAME postgres_event_concurrent_release
              COMMAND cpkt_postgres_event_concurrent_release_test)
      cpkt_group_set_tests_properties(postgres_event_concurrent_release PROPERTIES TIMEOUT 90)
      if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        cpkt_group_add_test(NAME postgres_notice_event_lifetime
                  COMMAND cpkt_postgres_notice_event_lifetime_test)
        cpkt_group_add_test(NAME postgres_event_allocation
                  COMMAND cpkt_postgres_event_allocation_test)
        cpkt_group_add_test(NAME postgres_result_retrieval_native_peer
                  COMMAND cpkt_postgres_result_retrieval_native_peer)
        cpkt_group_add_test(NAME postgres_event_address_reuse
                  COMMAND cpkt_postgres_event_address_reuse_test)
        cpkt_group_add_test(NAME postgres_oauth_cleanup
                  COMMAND cpkt_postgres_oauth_cleanup_test)
      endif()
      string(REGEX MATCH "^[0-9]+" _cpkt_postgresql_oauth_major
              "${CPKT_POSTGRESQL_VERSION}")
      cpkt_group_get_target_property(_cpkt_postgresql_shared_location
              cpkt::postgresql_shared IMPORTED_LOCATION)
      get_filename_component(_cpkt_postgresql_library_dir
              "${_cpkt_postgresql_shared_location}" DIRECTORY)
      cpkt_group_add_test(NAME postgres_oauth_module
              COMMAND cpkt_postgres_oauth_module_test
                "${_cpkt_postgresql_library_dir}/libpq-oauth-${_cpkt_postgresql_oauth_major}${CMAKE_SHARED_MODULE_SUFFIX}")
      cpkt_group_add_test(NAME sqlite_facade COMMAND cpkt_sqlite_facade_test)
      if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        cpkt_group_add_test(NAME sqlite_module_adapter_fault
                  COMMAND cpkt_sqlite_module_adapter_fault_test)
      endif()
      cpkt_group_add_test(NAME sqlite_log_native_peer COMMAND cpkt_sqlite_log_native_peer_test)
      cpkt_group_set_tests_properties(sqlite_log_native_peer PROPERTIES LABELS "facade;memcheck;integration" TIMEOUT 30)
      cpkt_group_add_test(NAME sqlite_error_code_native_peer
              COMMAND cpkt_sqlite_error_code_native_peer_test)
      cpkt_group_set_tests_properties(sqlite_error_code_native_peer PROPERTIES LABELS "facade;memcheck;integration" TIMEOUT 30)
      foreach(_sqlite_consumer_variant IN ITEMS static shared)
        cpkt_group_add_test(NAME sqlite_loadable_extension_${_sqlite_consumer_variant}
                  COMMAND cpkt_sqlite_loadable_extension_${_sqlite_consumer_variant}_test
                    "$<TARGET_FILE:cpkt_sqlite_loadable_extension>")
        cpkt_group_set_tests_properties(sqlite_loadable_extension_${_sqlite_consumer_variant} PROPERTIES TIMEOUT 30 LABELS "facade;memcheck;integration")
      endforeach()
      cpkt_group_add_test(NAME sqlite_rebase_collection
              COMMAND cpkt_sqlite_rebase_collection_test)
      cpkt_group_add_test(NAME sqlite_changeset_null_conflict
              COMMAND cpkt_sqlite_changeset_null_conflict_test)
      cpkt_group_add_test(NAME sqlite_vfs_wal_capability
              COMMAND cpkt_sqlite_vfs_wal_capability_test
                "${CMAKE_CURRENT_BINARY_DIR}/sqlite-vfs-no-shm.db")
      cpkt_group_add_test(NAME iodbc_static COMMAND cpkt_iodbc_static_test)
      cpkt_group_add_test(NAME iodbc_shared COMMAND cpkt_iodbc_shared_test)
      cpkt_group_set_tests_properties(iodbc_static iodbc_shared PROPERTIES LABELS "integration;memcheck")
      cpkt_group_add_test(NAME sqlite_child_lifetime_concurrency
              COMMAND cpkt_sqlite_child_lifetime_concurrency_test)
      cpkt_group_add_test(NAME sqlite_callback_synchronization
              COMMAND cpkt_sqlite_callback_synchronization_test)
      cpkt_group_set_tests_properties(sqlite_callback_synchronization PROPERTIES TIMEOUT 15)
      cpkt_group_add_test(NAME sqlite_registration_concurrency
              COMMAND cpkt_sqlite_registration_concurrency_test)
      cpkt_group_set_tests_properties(sqlite_registration_concurrency PROPERTIES TIMEOUT 15)
      cpkt_group_add_test(NAME sqlite_fts5_registration_concurrency
              COMMAND cpkt_sqlite_fts5_registration_concurrency_test)
      cpkt_group_set_tests_properties(sqlite_fts5_registration_concurrency PROPERTIES TIMEOUT 30)
      cpkt_group_add_test(NAME sqlite_window_aggregate
              COMMAND cpkt_sqlite_window_aggregate_test)
      cpkt_group_add_test(NAME sqlite_unlock_notify_groups
              COMMAND cpkt_sqlite_unlock_notify_groups_test)
      if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        cpkt_group_add_test(NAME sqlite_unlock_notify_race
                  COMMAND cpkt_sqlite_unlock_notify_race_test)
        cpkt_group_set_tests_properties(sqlite_unlock_notify_race PROPERTIES TIMEOUT 15)
      endif()
      cpkt_group_add_test(NAME sqlite_page_cache_lifetime
              COMMAND cpkt_sqlite_page_cache_lifetime_test)
      cpkt_group_add_test(NAME sqlite_auto_extension_shutdown
              COMMAND cpkt_sqlite_auto_extension_shutdown_test)
      if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        cpkt_group_add_test(NAME sqlite_auto_extension_lock_order
                  COMMAND cpkt_sqlite_auto_extension_lock_order_test)
        cpkt_group_set_tests_properties(sqlite_auto_extension_lock_order PROPERTIES TIMEOUT 15)
      endif()
      cpkt_group_add_test(NAME sqlite_rtree_registration_failure
              COMMAND cpkt_sqlite_rtree_registration_failure_test)
      cpkt_group_add_test(NAME sqlite_session_hook_guard
              COMMAND cpkt_sqlite_session_hook_guard_test)
      if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        cpkt_group_add_test(NAME sqlite_page_cache_discard
                  COMMAND cpkt_sqlite_page_cache_discard_test)
      endif()
      cpkt_group_add_test(NAME sqlite_collation_failure
              COMMAND cpkt_sqlite_collation_failure_test)
      file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/db-completion")
      cpkt_group_add_test(NAME sqlite_native_views
              COMMAND cpkt_sqlite_native_views_test
                "${CMAKE_BINARY_DIR}/db-completion/native-view.sqlite"
                "${CMAKE_BINARY_DIR}/db-completion/sparse-view.sqlite")
      cpkt_group_set_tests_properties(sqlite_native_views PROPERTIES TIMEOUT 30)
      cpkt_group_add_test(NAME sqlite_vfs_native_direct
              COMMAND cpkt_sqlite_vfs_native_ownership_test direct)
      cpkt_group_add_test(NAME sqlite_vfs_native_file_object
              COMMAND cpkt_sqlite_vfs_native_ownership_test file
                "${CMAKE_BINARY_DIR}/db-completion/native-file-object.sqlite")
      cpkt_group_set_tests_properties(sqlite_vfs_native_direct sqlite_vfs_native_file_object PROPERTIES TIMEOUT 30)
      cpkt_group_add_test(NAME sqlite_rebaser_empty
              COMMAND cpkt_sqlite_rebaser_empty_test)
      cpkt_group_set_tests_properties(sqlite_rebaser_empty PROPERTIES TIMEOUT 30)
      cpkt_group_add_test(NAME sqlite_completion
              COMMAND cpkt_sqlite_completion_test
                "${CMAKE_BINARY_DIR}/db-completion/completion.sqlite")
      cpkt_group_set_tests_properties(sqlite_completion PROPERTIES TIMEOUT 30)
      cpkt_group_add_test(NAME sqlite_stream_failure
              COMMAND cpkt_sqlite_stream_failure_test
                "${CMAKE_BINARY_DIR}/db-completion/stream-source.sqlite"
                "${CMAKE_BINARY_DIR}/db-completion/stream-replica.sqlite")
      cpkt_group_set_tests_properties(sqlite_stream_failure PROPERTIES TIMEOUT 60)
      if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        cpkt_group_add_test(NAME sqlite_temp_directory
                  COMMAND cpkt_sqlite_temp_directory_test)
        cpkt_group_set_tests_properties(sqlite_temp_directory PROPERTIES TIMEOUT 30)
      endif()
      cpkt_group_add_test(
              NAME postgres_e2e_harness
              COMMAND bash "${CMAKE_SOURCE_DIR}/tests/postgres_e2e_harness_test.sh")
      cpkt_group_add_test(NAME devenv_lifecycle
              COMMAND bash "${CMAKE_SOURCE_DIR}/tests/devenv_lifecycle_test.sh"
                "${CMAKE_SOURCE_DIR}")
      cpkt_group_add_test(
              NAME postgres_oauth_loader_patch
              COMMAND bash "${CMAKE_SOURCE_DIR}/tests/postgres_oauth_loader_patch_test.sh")
      cpkt_group_add_test(
              NAME clean_source_test_scratch
              COMMAND bash "${CMAKE_SOURCE_DIR}/tests/clean_source_test_scratch_test.sh"
                      "${CMAKE_SOURCE_DIR}")
      cpkt_group_set_tests_properties(clean_source_test_scratch PROPERTIES LABELS "packaging;smoke" TIMEOUT 30)
      if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(_cpkt_auth_symbol_format macho)
      else()
        set(_cpkt_auth_symbol_format elf)
      endif()

      if(CPKT_TARGET_ID STREQUAL "x86_64-linux-gnu")

      endif()
      if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(_cpkt_db_symbol_format macho)
      else()
        set(_cpkt_db_symbol_format elf)
      endif()
      foreach(_cpkt_db_provider IN ITEMS postgres sqlite)
        cpkt_group_add_test(NAME ${_cpkt_db_provider}_export_policy
                  COMMAND bash "${CMAKE_SOURCE_DIR}/tests/db_export_policy_test.sh"
                    "${_cpkt_db_provider}"
                    "$<TARGET_FILE:cpkt_${_cpkt_db_provider}_shared>"
                    "${CMAKE_SOURCE_DIR}/cmake/exports/cpkt_${_cpkt_db_provider}.txt"
                    "${_cpkt_db_symbol_format}" "${CMAKE_NM}"
                    "${CPKT_EXTERNAL_ROOT}")
        cpkt_group_set_tests_properties(${_cpkt_db_provider}_export_policy PROPERTIES LABELS "facade;packaging" TIMEOUT 30)
      endforeach()
      if(CPKT_TARGET_ID STREQUAL "x86_64-linux-gnu")
        foreach(_cpkt_catalog_generator IN ITEMS Ninja Makefiles)
          if(_cpkt_catalog_generator STREQUAL "Ninja")
            set(_cpkt_catalog_generator_name "Ninja")
          else()
            set(_cpkt_catalog_generator_name "Unix Makefiles")
          endif()

        endforeach()
      endif()
      file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/sasl-empty-plugin-path")
      set(_cpkt_memcheck_facade_tests postgres_receiver_shell postgres_events postgres_nested_copy_notice postgres_event_concurrent_release sqlite_facade sqlite_rebase_collection sqlite_changeset_null_conflict sqlite_vfs_wal_capability sqlite_child_lifetime_concurrency sqlite_page_cache_lifetime sqlite_native_views sqlite_vfs_native_direct sqlite_vfs_native_file_object sqlite_rebaser_empty sqlite_completion sqlite_stream_failure sqlite_log_native_peer sqlite_error_code_native_peer sqlite_callback_synchronization sqlite_registration_concurrency sqlite_fts5_registration_concurrency sqlite_window_aggregate sqlite_unlock_notify_groups sqlite_auto_extension_shutdown sqlite_rtree_registration_failure sqlite_session_hook_guard sqlite_collation_failure)
      if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        cpkt_group_set_tests_properties(sqlite_unlock_notify_race PROPERTIES LABELS "facade;memcheck")
        cpkt_group_set_tests_properties(sqlite_auto_extension_lock_order postgres_oauth_cleanup postgres_event_allocation postgres_result_retrieval_native_peer postgres_event_address_reuse PROPERTIES LABELS "facade;memcheck")
      endif()
      if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        list(APPEND _cpkt_memcheck_facade_tests
                  sqlite_page_cache_discard sqlite_temp_directory postgres_notice_event_lifetime sqlite_module_adapter_fault)
      endif()
      foreach(_cpkt_memcheck_test IN LISTS _cpkt_memcheck_facade_tests)
        cpkt_group_set_property(TEST ${_cpkt_memcheck_test} APPEND PROPERTY LABELS
                  facade memcheck)
      endforeach()
      cpkt_group_add_test(
              NAME linux_toolchain_sysroot_policy
              COMMAND
                "${CMAKE_SOURCE_DIR}/tests/linux_toolchain_sysroot_policy_test.sh"
                "${CMAKE_SOURCE_DIR}")
      cpkt_group_add_test(
              NAME osxcross_linker_cache_refresh
              COMMAND
                "${CMAKE_SOURCE_DIR}/tests/osxcross_linker_cache_refresh_test.sh"
                "${CMAKE_SOURCE_DIR}")
      cpkt_group_add_test(
              NAME repo_dependency_cache_policy
              COMMAND
                "${CMAKE_SOURCE_DIR}/tests/repo_dependency_cache_policy_test.sh"
                "${CMAKE_SOURCE_DIR}")
      cpkt_group_add_test(
              NAME repo_dependency_stale_root
              COMMAND
                "${CMAKE_SOURCE_DIR}/tests/repo_dependency_stale_root_test.sh"
                "${CMAKE_SOURCE_DIR}")
      cpkt_group_add_test(
              NAME external_project_toolchain_environment
              COMMAND
                "${CMAKE_COMMAND}"
                "-DCPKT_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
                "-DCPKT_TEST_BINARY_DIR=${CMAKE_BINARY_DIR}/external-project-toolchain-env-fixture"
                -P "${CMAKE_SOURCE_DIR}/tests/external_project_toolchain_env_test.cmake")
      cpkt_group_add_test(
              NAME pinned_toolchain_resolvers
              COMMAND
                "${CMAKE_SOURCE_DIR}/tests/toolchain_resolver_test.sh"
                "${CMAKE_SOURCE_DIR}")
      cpkt_group_add_test(
              NAME lifecycle_toolchain_resolvers
              COMMAND
                "${CMAKE_SOURCE_DIR}/tests/toolchain_resolver_test.sh"
                "${CMAKE_SOURCE_DIR}"
                "${CMAKE_SOURCE_DIR}/scripts/cpkt-toolchains.sh")
      cpkt_group_add_test(
              NAME aflpp_resolver_cleanup
              COMMAND
                "${CMAKE_SOURCE_DIR}/tests/aflpp_resolver_cleanup_test.sh"
                "${CMAKE_SOURCE_DIR}")
      cpkt_group_add_test(
              NAME dependency_archive_cache
              COMMAND
                "${CMAKE_SOURCE_DIR}/tests/dependency_archive_cache_test.sh"
                "${CMAKE_SOURCE_DIR}")

            cpkt_group_add_test(
              NAME darwin_install_name_normalization
              COMMAND
                "${CMAKE_SOURCE_DIR}/tests/darwin_install_name_normalization_test.sh"
                "${CMAKE_SOURCE_DIR}"
                "${CMAKE_COMMAND}")
      cpkt_group_set_tests_properties(darwin_install_name_normalization PROPERTIES LABELS "packaging;smoke" TIMEOUT 30)
      if(CPKT_TARGET_ID STREQUAL "x86_64-linux-gnu")
        cpkt_group_add_test(
                  NAME dependency_reuse_configure
                  COMMAND
                    "${CMAKE_SOURCE_DIR}/tests/dependency_reuse_configure_test.sh"
                    "${CMAKE_SOURCE_DIR}"
                    "${CMAKE_COMMAND}"
                    "${CMAKE_GENERATOR}"
                    "${CMAKE_MAKE_PROGRAM}"
                    "${CMAKE_TOOLCHAIN_FILE}"
                    "${CMAKE_BUILD_TYPE}")
        cpkt_group_set_tests_properties(dependency_reuse_configure PROPERTIES LABELS "packaging;smoke" TIMEOUT 60)
      endif()
      cpkt_group_add_test(
              NAME privacy_scan_failure
              COMMAND
                "${CMAKE_SOURCE_DIR}/tests/privacy_scan_failure_test.sh")
      cpkt_group_set_tests_properties(privacy_scan_failure PROPERTIES LABELS "packaging" TIMEOUT 30)
      # Fixture registration is shared in CpktPreflightTests.cmake.
      cpkt_group_set_tests_properties(source_archive_verify_failure PROPERTIES LABELS "packaging;smoke" TIMEOUT 30)
      foreach(_cpkt_install_generator IN ITEMS Ninja Makefiles)
        if(_cpkt_install_generator STREQUAL "Makefiles")
          set(_cpkt_install_generator_name "Unix Makefiles")
        else()
          set(_cpkt_install_generator_name "Ninja")
        endif()
        # Fixture registration is shared in CpktPreflightTests.cmake.
        cpkt_group_set_tests_properties(dependency_install_order_${_cpkt_install_generator} PROPERTIES LABELS "packaging;smoke" TIMEOUT 30)
      endforeach()

      cpkt_group_add_test(
              NAME native_hardening_host
              COMMAND
                "${CMAKE_SOURCE_DIR}/tests/native_hardening_host_test.sh"
                "${CMAKE_SOURCE_DIR}")
      cpkt_group_set_tests_properties(native_hardening_host PROPERTIES LABELS "smoke" TIMEOUT 30)

      cpkt_group_add_test(
              NAME facade_c89_target_policy
              COMMAND
                bash
                "${CMAKE_SOURCE_DIR}/tests/facade_c89_target_policy_test.sh"
                "${CMAKE_SOURCE_DIR}")
      cpkt_group_add_test(
              NAME postgres_header_facade
              COMMAND
                "${CMAKE_SOURCE_DIR}/tests/postgres_header_facade_test.sh"
                "${CMAKE_SOURCE_DIR}")
      cpkt_group_add_test(
              NAME postgres_api_coverage
              COMMAND
                bash
                "${CMAKE_SOURCE_DIR}/tests/postgres_api_coverage_test.sh"
                "${CMAKE_SOURCE_DIR}/src/postgres.c"
                "$<TARGET_PROPERTY:cpkt::postgresql_static,INTERFACE_INCLUDE_DIRECTORIES>")
      cpkt_group_add_test(
              NAME sqlite_header_facade
              COMMAND
                "${CMAKE_SOURCE_DIR}/tests/sqlite_header_facade_test.sh"
                "${CMAKE_SOURCE_DIR}")
      cpkt_group_add_test(
              NAME sqlite_api_coverage
              COMMAND
                bash
                "${CMAKE_SOURCE_DIR}/tests/sqlite_api_coverage_test.sh"
                "${CMAKE_SOURCE_DIR}/src/sqlite.c"
                "$<TARGET_PROPERTY:cpkt::sqlite_static,INTERFACE_INCLUDE_DIRECTORIES>/sqlite3.h"
                "$<TARGET_PROPERTY:cpkt::sqlite_static,INTERFACE_INCLUDE_DIRECTORIES>/sqlite3session.h"
                "$<TARGET_PROPERTY:cpkt::sqlite_static,INTERFACE_INCLUDE_DIRECTORIES>/sqlite3rtree.h"
                "$<TARGET_PROPERTY:cpkt::sqlite_static,INTERFACE_INCLUDE_DIRECTORIES>/fts5.h")
      if(CPKT_TARGET_ID STREQUAL "x86_64-linux-gnu")

        cpkt_group_add_test(
                  NAME clangd_surface
                  COMMAND
                    "${CMAKE_SOURCE_DIR}/scripts/verify-clangd-surface.sh"
                    "${CMAKE_SOURCE_DIR}"
                    "${CMAKE_BINARY_DIR}")
      endif()
    endif()
  endif()
endif()
include(cmake/CpktPackage.cmake)
if(CPKT_BUILD_TESTS AND CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  foreach(linkage IN ITEMS static shared)
    cpkt_group_add_executable(cpkt_abi_smoke_${linkage} tests/release_smoke.c)
    cpkt_configure_c89_target(cpkt_abi_smoke_${linkage})
    cpkt_add_repo_warning_errors(cpkt_abi_smoke_${linkage})
    target_link_libraries(cpkt_abi_smoke_${linkage} PRIVATE cpkt_sqlite_${linkage})
    set_target_properties(cpkt_abi_smoke_${linkage} PROPERTIES
          BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "@executable_path/../lib")
    if(CPKT_CAN_RUN_TARGET_EXECUTABLES)
    cpkt_group_add_test(NAME release_smoke_${linkage}
          COMMAND "${CMAKE_COMMAND}" -E env
            "DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}:${CPKT_EXTERNAL_ROOT}/sqlite/install/lib"
            "$<TARGET_FILE:cpkt_abi_smoke_${linkage}>")
    endif()
  endforeach()
endif()

# Each linked shared module proves an owned static native/facade archive is PIC.
if(CPKT_BUILD_TESTS)
  cpkt_group_add_library(cpkt_static_archive_pic_postgres_facade SHARED tests/sdk-consumers/cpkt_postgres_facade_strict.c)
  cpkt_group_target_command(target_compile_options cpkt_static_archive_pic_postgres_facade PRIVATE -std=c89)
  cpkt_add_repo_warning_errors(cpkt_static_archive_pic_postgres_facade)
  cpkt_group_target_command(target_link_libraries cpkt_static_archive_pic_postgres_facade PRIVATE cpkt_postgres_static)
  cpkt_group_add_library(cpkt_static_archive_pic_sqlite_facade SHARED tests/sdk-consumers/cpkt_sqlite_facade_strict.c)
  cpkt_group_target_command(target_compile_options cpkt_static_archive_pic_sqlite_facade PRIVATE -std=c89)
  cpkt_add_repo_warning_errors(cpkt_static_archive_pic_sqlite_facade)
  cpkt_group_target_command(target_link_libraries cpkt_static_archive_pic_sqlite_facade PRIVATE cpkt_sqlite_static)
  if(CPKT_CAN_RUN_TARGET_EXECUTABLES)
    cpkt_group_add_test(NAME static_archive_pic_link
      COMMAND "${CMAKE_SOURCE_DIR}/tests/static_archive_pic_link_test.sh"
        "$<TARGET_FILE:cpkt_static_archive_pic_postgres_facade>"
        "$<TARGET_FILE:cpkt_static_archive_pic_sqlite_facade>"
    )
  endif()
endif()

if(CPKT_BUILD_TESTS)
  cpkt_group_add_executable(cpkt_db_sdk_example examples/db-sdk/main.c)
  cpkt_group_use_local_runtime(cpkt_db_sdk_example)
  cpkt_configure_c89_target(cpkt_db_sdk_example)
  cpkt_add_repo_warning_errors(cpkt_db_sdk_example)
  cpkt_group_target_command(target_link_libraries cpkt_db_sdk_example PRIVATE cpkt_sqlite_static)
  if(CPKT_CAN_RUN_TARGET_EXECUTABLES)
    cpkt_group_add_test(NAME db_sdk_example COMMAND cpkt_db_sdk_example)
    cpkt_group_set_tests_properties(db_sdk_example PROPERTIES LABELS "smoke;example" TIMEOUT 30)
  endif()
  include(cmake/CpktDBAPI.cmake)
  include(cmake/CpktNativeChecks.cmake)
endif()
