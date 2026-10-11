include(ExternalProject)
include("${CMAKE_CURRENT_LIST_DIR}/CpktDependencyArchiveCache.cmake")
macro(cpkt_cached_external_project_add)
set(_cpkt_ep_args ${ARGV})
# Before 3.24, ExternalProject preserves archive timestamps by default and
# treats this newer keyword as another value of the preceding argument.
if(CMAKE_VERSION VERSION_LESS "3.24")
list(FIND _cpkt_ep_args "DOWNLOAD_EXTRACT_TIMESTAMP" _cpkt_ep_timestamp_index)
if(NOT _cpkt_ep_timestamp_index LESS 0)
list(REMOVE_AT _cpkt_ep_args ${_cpkt_ep_timestamp_index})
list(REMOVE_AT _cpkt_ep_args ${_cpkt_ep_timestamp_index})
endif()
endif()
list(FIND _cpkt_ep_args "URL" _cpkt_ep_url_index)
list(FIND _cpkt_ep_args "URL_HASH" _cpkt_ep_hash_index)
if(_cpkt_ep_url_index LESS 1 OR _cpkt_ep_hash_index LESS 0 OR _cpkt_ep_hash_index LESS _cpkt_ep_url_index)
message(FATAL_ERROR "cpkt_cached_external_project_add requires URL and URL_HASH after the target name")
endif()
math(EXPR _cpkt_ep_url_start "${_cpkt_ep_url_index} + 1")
math(EXPR _cpkt_ep_url_count "${_cpkt_ep_hash_index} - ${_cpkt_ep_url_start}")
if(_cpkt_ep_url_count LESS 1)
message(FATAL_ERROR "cpkt_cached_external_project_add requires at least one URL")
endif()
list(SUBLIST _cpkt_ep_args ${_cpkt_ep_url_start} ${_cpkt_ep_url_count} _cpkt_ep_urls)
math(EXPR _cpkt_ep_hash_value_index "${_cpkt_ep_hash_index} + 1")
list(GET _cpkt_ep_args ${_cpkt_ep_hash_value_index} _cpkt_ep_hash)
string(REGEX REPLACE "^SHA256=" "" _cpkt_ep_sha256 "${_cpkt_ep_hash}")
string(LENGTH "${_cpkt_ep_sha256}" _cpkt_ep_sha256_length)
if(NOT _cpkt_ep_hash MATCHES "^SHA256="
      OR NOT _cpkt_ep_sha256_length EQUAL 64
      OR NOT "${_cpkt_ep_sha256}" MATCHES "^[A-Fa-f0-9]+$")
message(FATAL_ERROR "cpkt_cached_external_project_add requires URL_HASH SHA256=<digest>")
endif()
list(FIND _cpkt_ep_args "DOWNLOAD_NAME" _cpkt_ep_download_name_index)
if(_cpkt_ep_download_name_index LESS 0)
list(GET _cpkt_ep_urls 0 _cpkt_ep_name_url)
string(REGEX REPLACE "[?#].*$" "" _cpkt_ep_name_url "${_cpkt_ep_name_url}")
get_filename_component(_cpkt_ep_archive_name "${_cpkt_ep_name_url}" NAME)
else()
math(EXPR _cpkt_ep_download_name_value_index "${_cpkt_ep_download_name_index} + 1")
list(GET _cpkt_ep_args ${_cpkt_ep_download_name_value_index} _cpkt_ep_archive_name)
endif()
if(_cpkt_ep_archive_name STREQUAL "")
message(FATAL_ERROR "cpkt_cached_external_project_add could not determine an archive name")
endif()
cpkt_acquire_dependency_archive(_cpkt_ep_cached_archive
    NAME "${_cpkt_ep_archive_name}"
    SHA256 "${_cpkt_ep_sha256}"
    URLS ${_cpkt_ep_urls}
    SEED_PATHS "${CPKT_DOWNLOAD_ROOT}/${_cpkt_ep_archive_name}")
math(EXPR _cpkt_ep_remove_count "${_cpkt_ep_url_count} + 1")
foreach(_cpkt_ep_remove_index RANGE 1 ${_cpkt_ep_remove_count})
list(REMOVE_AT _cpkt_ep_args ${_cpkt_ep_url_index})
endforeach()
list(INSERT _cpkt_ep_args ${_cpkt_ep_url_index} URL "${_cpkt_ep_cached_archive}")
ExternalProject_Add(${_cpkt_ep_args})
endmacro()
function(cpkt_order_shared_install shared_project static_project)
  # Both variants write common package metadata into one install prefix.
  # CMP0114 NEW makes each step target own its command. Keep the shared build
  # as a separately schedulable prerequisite of the shared install.
  ExternalProject_Add_StepTargets(${static_project} install)
  ExternalProject_Add_StepTargets(${shared_project} build install)
  ExternalProject_Add_StepDependencies(${shared_project} install
    ${static_project}-install)
endfunction()

function(cpkt_record_dependency_target target_name)
set_property(GLOBAL APPEND PROPERTY CPKT_DEPENDENCY_TARGETS "${target_name}")
endfunction()
function(cpkt_component_producer_prerequisites output component)
if(NOT DEFINED CPKT_INVENTORY)
set(${output} "${ARGN}" PARENT_SCOPE)
return()
endif()
string(JSON _count LENGTH "${CPKT_INVENTORY}" components "${component}" dependencies)
set(_targets "")
if(_count GREATER 0)
math(EXPR _last "${_count} - 1")
foreach(_index RANGE 0 ${_last})
string(JSON _dependency GET "${CPKT_INVENTORY}" components "${component}" dependencies ${_index})
get_property(_registered GLOBAL PROPERTY "CPKT_PRODUCER_TARGETS_${_dependency}" SET)
if(NOT _registered)
message(FATAL_ERROR "${component} prerequisite ${_dependency} has not been validated")
endif()
get_property(_owned GLOBAL PROPERTY "CPKT_PRODUCER_TARGETS_${_dependency}")
list(APPEND _targets ${_owned})
endforeach()
endif()
set(${output} "${_targets}" PARENT_SCOPE)
endfunction()
function(cpkt_require_dependency_file path label)
if(NOT EXISTS "${path}")
message(FATAL_ERROR
      "${label} was not found at ${path}\n"
      "Provide the dependency tree for this preset under CPKT_EXTERNAL_ROOT.\n"
      "You can prebuild it explicitly with scripts/deps.sh, but normal build/test/release entry points do not do that for you.")
endif()
endfunction()
function(cpkt_get_target_triple out_var)
string(TOLOWER "${CPKT_TARGET_OS}" _cpkt_target_os_lower)
if(_cpkt_target_os_lower STREQUAL "darwin")
if(CPKT_TARGET_ARCH STREQUAL "arm64")
set(_triple "aarch64-apple-darwin")
else()
message(FATAL_ERROR "Unsupported Darwin CPKT_TARGET_ARCH: ${CPKT_TARGET_ARCH}")
endif()
elseif(CPKT_TARGET_ARCH STREQUAL "x86_64")
if(CPKT_TARGET_LIBC STREQUAL "musl")
set(_triple "x86_64-linux-musl")
else()
set(_triple "x86_64-linux-gnu")
endif()
elseif(CPKT_TARGET_ARCH STREQUAL "aarch64")
if(CPKT_TARGET_LIBC STREQUAL "musl")
set(_triple "aarch64-linux-musl")
else()
set(_triple "aarch64-linux-gnu")
endif()
elseif(CPKT_TARGET_ARCH STREQUAL "armhf")
if(CPKT_TARGET_LIBC STREQUAL "musl")
set(_triple "arm-linux-musleabihf")
else()
set(_triple "arm-linux-gnueabihf")
endif()
else()
message(FATAL_ERROR "Unsupported CPKT_TARGET_ARCH: ${CPKT_TARGET_ARCH}")
endif()
set(${out_var} "${_triple}" PARENT_SCOPE)
endfunction()
function(cpkt_get_external_c_flags out_var)
set(_flags "-O2 -DNDEBUG -g0")
if(CMAKE_C_COMPILER_ID MATCHES "^(AppleClang|Clang|GNU)$")
string(APPEND _flags
      " -fmacro-prefix-map=${CPKT_DEPENDENCY_BUILD_ROOT}=deps-build"
      " -fmacro-prefix-map=${CPKT_EXTERNAL_ROOT}=deps"
    )
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
string(APPEND _flags " -include stdint.h -include sys/types.h")
if(CPKT_MACOS_DEPLOYMENT_TARGET)
string(APPEND _flags
        " -mmacosx-version-min=${CPKT_MACOS_DEPLOYMENT_TARGET}")
endif()
endif()
if(NOT "${CMAKE_C_FLAGS}" STREQUAL "")
set(_flags "${CMAKE_C_FLAGS} ${_flags}")
endif()
string(STRIP "${_flags}" _flags)
set(${out_var} "${_flags}" PARENT_SCOPE)
endfunction()
function(cpkt_append_external_pkg_config_env_args out_var)
set(_args ${${out_var}})
set(_pkg_config_dirs "")
foreach(_prefix_var IN ITEMS CPKT_CORE_PREFIX CPKT_POSTGRESQL_PREFIX)
if(DEFINED ${_prefix_var} AND NOT "${${_prefix_var}}" STREQUAL "")
list(APPEND _pkg_config_dirs
        "${${_prefix_var}}/lib/pkgconfig"
        "${${_prefix_var}}/share/pkgconfig")
endif()
endforeach()
if(_pkg_config_dirs)
list(REMOVE_DUPLICATES _pkg_config_dirs)
string(JOIN ":" _pkg_config_libdir ${_pkg_config_dirs})
else()
set(_pkg_config_libdir "${CPKT_EXTERNAL_ROOT}/.pkgconfig-empty")
endif()
list(APPEND _args
    PKG_CONFIG_PATH=
    PKG_CONFIG_DIR=
    PKG_CONFIG_LIBDIR=${_pkg_config_libdir})
set(${out_var} "${_args}" PARENT_SCOPE)
endfunction()
function(cpkt_append_darwin_external_env_args out_var)
set(_args ${${out_var}})
cpkt_append_external_pkg_config_env_args(_args)
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
foreach(_cpkt_required_var IN ITEMS
        CMAKE_C_COMPILER
        CMAKE_CXX_COMPILER
        CMAKE_AR
        CMAKE_RANLIB
        CMAKE_STRIP
        CMAKE_NM)
if(NOT DEFINED ${_cpkt_required_var}
          OR "${${_cpkt_required_var}}" STREQUAL "")
message(FATAL_ERROR
          "Darwin external builds require ${_cpkt_required_var}")
endif()
endforeach()
list(APPEND _args
      CC=${CMAKE_C_COMPILER}
      CXX=${CMAKE_CXX_COMPILER}
      AR=${CMAKE_AR}
      RANLIB=${CMAKE_RANLIB}
      STRIP=${CMAKE_STRIP}
      NM=${CMAKE_NM}
    )
if(CMAKE_OSX_SYSROOT)
list(APPEND _args "SDKROOT=${CMAKE_OSX_SYSROOT}")
endif()
if(CPKT_MACOS_DEPLOYMENT_TARGET)
list(APPEND _args
        MACOSX_DEPLOYMENT_TARGET=${CPKT_MACOS_DEPLOYMENT_TARGET})
endif()
if(CPKT_OSXCROSS_ROOT)
list(APPEND _args
        PATH=${CPKT_OSXCROSS_BIN_DIR}:$ENV{PATH}
        LD_LIBRARY_PATH=${CPKT_OSXCROSS_ROOT}/lib:$ENV{LD_LIBRARY_PATH})
endif()
if(CPKT_OSXCROSS_ROOT AND CMAKE_LINKER)
list(APPEND _args LDFLAGS=--ld-path=${CMAKE_LINKER})
endif()
endif()
set(${out_var} "${_args}" PARENT_SCOPE)
endfunction()
function(cpkt_append_pinned_external_toolchain_env_args out_var)
set(_args ${${out_var}})
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CPKT_BUILD_DEPENDENCIES)
foreach(_cpkt_required_var IN ITEMS
        CPKT_TOOLCHAIN_ROOT
        CMAKE_C_COMPILER
        CMAKE_CXX_COMPILER
        CMAKE_LINKER
        CMAKE_AR
        CMAKE_RANLIB
        CMAKE_STRIP
        CMAKE_NM
        CMAKE_OBJCOPY
        CMAKE_OBJDUMP
        CMAKE_ADDR2LINE
        CMAKE_READELF)
if(NOT DEFINED ${_cpkt_required_var}
          OR "${${_cpkt_required_var}}" STREQUAL "")
message(FATAL_ERROR
          "Pinned Linux external builds require ${_cpkt_required_var}")
endif()
endforeach()
set(_cpkt_toolchain_bin "${CPKT_TOOLCHAIN_ROOT}/bin")
if(NOT IS_DIRECTORY "${_cpkt_toolchain_bin}")
message(FATAL_ERROR
        "Pinned Linux toolchain bin directory is missing: ${_cpkt_toolchain_bin}")
endif()
list(APPEND _args
      PATH=${_cpkt_toolchain_bin}:$ENV{PATH}
      CC=${CMAKE_C_COMPILER}
      CXX=${CMAKE_CXX_COMPILER}
      LD=${CMAKE_LINKER}
      AR=${CMAKE_AR}
      RANLIB=${CMAKE_RANLIB}
      STRIP=${CMAKE_STRIP}
      NM=${CMAKE_NM}
      OBJCOPY=${CMAKE_OBJCOPY}
      OBJDUMP=${CMAKE_OBJDUMP}
      ADDR2LINE=${CMAKE_ADDR2LINE}
      READELF=${CMAKE_READELF})
endif()
cpkt_append_darwin_external_env_args(_args)
set(${out_var} "${_args}" PARENT_SCOPE)
endfunction()
function(cpkt_get_strip_dependency_install_command out_var install_dir)
if(NOT CMAKE_STRIP)
message(FATAL_ERROR "CMAKE_STRIP is required when building release dependencies")
endif()
set(_strip_static_archives ON)
set(_strip_shared_libraries ON)
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
set(_strip_static_archives OFF)
set(_strip_shared_libraries OFF)
endif()
set(_command
    ${CMAKE_COMMAND}
      -DCPKT_STRIP_BIN=${CMAKE_STRIP}
      -DCPKT_STRIP_ROOT=${install_dir}
      -DCPKT_STRIP_STATIC_ARCHIVES=${_strip_static_archives}
      -DCPKT_STRIP_SHARED_LIBRARIES=${_strip_shared_libraries}
      -P ${CMAKE_SOURCE_DIR}/cmake/strip_dependency_install_tree.cmake
  )
set(${out_var} "${_command}" PARENT_SCOPE)
endfunction()
function(cpkt_get_autotools_link_flags out_var)
set(_flags "")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
set(_flags "-Wl,--enable-new-dtags,-rpath,\\\\$$ORIGIN")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
set(_flags "-Wl,-rpath,@loader_path")
if(NOT CMAKE_CROSSCOMPILING)
string(APPEND _flags " -Wl,-not_for_dyld_shared_cache")
endif()
endif()
if(NOT "${CMAKE_SHARED_LINKER_FLAGS}" STREQUAL "")
string(APPEND _flags " ${CMAKE_SHARED_LINKER_FLAGS}")
endif()
string(STRIP "${_flags}" _flags)
set(${out_var} "${_flags}" PARENT_SCOPE)
endfunction()
function(cpkt_add_postgresql)
cpkt_component_producer_prerequisites(producer_prerequisites postgresql cpkt_openldap_project cpkt_cyrus_sasl_project cpkt_krb5_shared_project cpkt_curl_project cpkt_zlib_project cpkt_openssl_project)
set(project_name "cpkt_postgresql_project")
set(prefix_dir "${CPKT_DEPENDENCY_BUILD_ROOT}/postgresql")
set(source_dir "${prefix_dir}/src")
set(build_dir "${prefix_dir}/build")
set(install_dir "${CPKT_EXTERNAL_ROOT}/postgresql/install")
set(stage_dir "${install_dir}/stage")
set(stamp_dir "${prefix_dir}/stamp")
set(tmp_dir "${prefix_dir}/tmp")
set(static_library "${install_dir}/lib/libpq${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(common_static_library "${install_dir}/lib/libpgcommon_shlib${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(port_static_library "${install_dir}/lib/libpgport${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(shared_library "${install_dir}/lib/libpq${CMAKE_SHARED_LIBRARY_SUFFIX}")
string(REGEX MATCH "^[0-9]+" postgresql_major_version "${CPKT_POSTGRESQL_VERSION}")
set(oauth_static_library "${install_dir}/lib/libpq-oauth${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(oauth_shared_library "${install_dir}/lib/libpq-oauth-${postgresql_major_version}${CMAKE_SHARED_LIBRARY_SUFFIX}")
cpkt_get_target_triple(target_triple)
cpkt_get_external_c_flags(external_cflags)
cpkt_get_autotools_link_flags(external_ldflags)
set(postgresql_ldflags "-L${CPKT_CORE_PREFIX}/lib ${external_ldflags}")
set(postgresql_curl_libs "-L${CPKT_CORE_PREFIX}/lib -lcurl")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
string(APPEND postgresql_ldflags
      # libgssapi_krb5 records libkrb5 and its sibling libraries as direct
      # DT_NEEDED dependencies.  PostgreSQL probes GSSAPI at configure time,
      # so GNU ld needs the bundled Kerberos directory in its link-time DSO
      # search closure even though no runtime path is embedded here.
      " -Wl,-rpath-link,${CPKT_CORE_PREFIX}/lib")
string(APPEND postgresql_curl_libs
      " -Wl,-rpath-link,${CPKT_CORE_PREFIX}/lib")
endif()
set(postgresql_darwin_install_name_normalize_command ${CMAKE_COMMAND} -E true)
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
set(postgresql_darwin_install_name_normalize_command
      ${CMAKE_COMMAND}
        -DCPKT_DARWIN_LIBRARY_DIR=${install_dir}/lib
        -DCPKT_DARWIN_STAGE_LIBRARY_DIR=${stage_dir}/usr/lib
        -DCPKT_DARWIN_INSTALL_NAME_TOOL=${CMAKE_INSTALL_NAME_TOOL}
        -DCPKT_DARWIN_OTOOL=${CPKT_OTOOL}
        -P ${CMAKE_SOURCE_DIR}/cmake/normalize_darwin_dylib_install_names.cmake)
endif()
set(env_args "")
cpkt_append_pinned_external_toolchain_env_args(env_args)
list(APPEND env_args
    # libpq static archives are part of the supported shared-consumer closure.
    "CFLAGS=${external_cflags} -fPIC"
    "CPPFLAGS=-I${CPKT_CORE_PREFIX}/include"
    "LDFLAGS=${postgresql_ldflags}"
    # OpenLDAP keeps liblutil as a private static archive.  It is required
    # after libldap for PostgreSQL's LDAP configure and final link checks.
    "LIBS=-llutil"
    "LIBCURL_CFLAGS=-I${CPKT_CORE_PREFIX}/include"
    "LIBCURL_LIBS=${postgresql_curl_libs}")
set(postgresql_configure_env_args ${env_args})
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
list(APPEND postgresql_configure_env_args
      "LD_LIBRARY_PATH=${CPKT_CORE_PREFIX}/lib")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND NOT CMAKE_CROSSCOMPILING)
set(postgresql_probe_ldflags "${postgresql_ldflags}")
string(APPEND postgresql_probe_ldflags " -Wl,-rpath,${CPKT_CORE_PREFIX}/lib")
list(REMOVE_ITEM postgresql_configure_env_args "LDFLAGS=${postgresql_ldflags}")
list(APPEND postgresql_configure_env_args "LDFLAGS=${postgresql_probe_ldflags}")
endif()
set(postgresql_post_configure_command
    COMMAND ${CMAKE_COMMAND}
      -DCPKT_POSTGRESQL_SOURCE_DIR=${source_dir}
      -P ${CMAKE_SOURCE_DIR}/cmake/patch_postgresql_buildinfo.cmake)
if(DEFINED postgresql_probe_ldflags)
list(APPEND postgresql_post_configure_command
      COMMAND ${CMAKE_COMMAND}
        -DCPKT_POSTGRESQL_BUILD_DIR=${build_dir}
        "-DCPKT_POSTGRESQL_PROBE_LDFLAGS=${postgresql_probe_ldflags}"
        "-DCPKT_POSTGRESQL_BASE_LDFLAGS=${postgresql_ldflags}"
        -P ${CMAKE_SOURCE_DIR}/cmake/remove_postgresql_probe_rpaths.cmake)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
list(APPEND postgresql_post_configure_command
      COMMAND ${CMAKE_COMMAND}
        -DCPKT_POSTGRESQL_SOURCE_DIR=${source_dir}
        -P ${CMAKE_SOURCE_DIR}/cmake/patch_postgresql_oauth_loader.cmake)
endif()
set(postgresql_make_env_args ${env_args} "MAKEFLAGS=")
cpkt_get_strip_dependency_install_command(strip_install_command "${install_dir}")
if(CPKT_BUILD_DEPENDENCIES)
file(MAKE_DIRECTORY "${install_dir}/include" "${install_dir}/lib")
endif()
if(CPKT_BUILD_DEPENDENCIES)
cpkt_cached_external_project_add(${project_name}
      URL "https://ftp.postgresql.org/pub/source/v${CPKT_POSTGRESQL_VERSION}/postgresql-${CPKT_POSTGRESQL_VERSION}.tar.bz2"
      URL_HASH "SHA256=555610c24d53e4316da5b7d3fc25c279d96856d5e0e23ee308c328c5fa881d9f"
      DOWNLOAD_NAME "postgresql-${CPKT_POSTGRESQL_VERSION}.tar.bz2"
      PREFIX "${prefix_dir}"
      DOWNLOAD_DIR "${CPKT_DOWNLOAD_ROOT}"
      SOURCE_DIR "${source_dir}"
      BINARY_DIR "${build_dir}"
      STAMP_DIR "${stamp_dir}"
      TMP_DIR "${tmp_dir}"
      TIMEOUT ${CPKT_DEPENDENCY_DOWNLOAD_TIMEOUT}
      INACTIVITY_TIMEOUT ${CPKT_DEPENDENCY_DOWNLOAD_INACTIVITY_TIMEOUT}
      DEPENDS ${producer_prerequisites}
      PATCH_COMMAND ${CMAKE_COMMAND}
        -DCPKT_PATCH_WORKING_DIRECTORY=${source_dir}
        -DCPKT_PATCH_SERIES=${CMAKE_SOURCE_DIR}/cmake/patches/postgresql.series
        -P ${CMAKE_SOURCE_DIR}/cmake/apply_patch_series.cmake
      CONFIGURE_COMMAND ${CMAKE_COMMAND} -E chdir "${build_dir}"
        ${CMAKE_COMMAND} -E env ${postgresql_configure_env_args}
        "${source_dir}/configure"
        --host=${target_triple}
        --prefix=/usr
        --libdir=/usr/lib
        --includedir=/usr/include
        --sysconfdir=/etc
        --disable-rpath
        --disable-nls
        --without-icu
        --without-readline
        --with-ssl=openssl
        --with-gssapi
        --with-ldap
        --with-libcurl
        ${postgresql_post_configure_command}
      BUILD_COMMAND ${CMAKE_COMMAND} -E chdir "${build_dir}"
        # Generate the server-owned headers consumed by the frontend common
        # library without building PostgreSQL server or client executables.
        ${CMAKE_COMMAND} -E env ${postgresql_make_env_args} make -C src/backend generated-headers -j${CPKT_DEPENDENCY_BUILD_JOBS}
        COMMAND ${CMAKE_COMMAND} -E chdir "${build_dir}"
        ${CMAKE_COMMAND} -E env ${postgresql_make_env_args} make -C src/common -j${CPKT_DEPENDENCY_BUILD_JOBS}
        COMMAND ${CMAKE_COMMAND} -E chdir "${build_dir}"
        ${CMAKE_COMMAND} -E env ${postgresql_make_env_args} make -C src/port -j${CPKT_DEPENDENCY_BUILD_JOBS}
        COMMAND ${CMAKE_COMMAND} -E chdir "${build_dir}"
        ${CMAKE_COMMAND} -E env ${postgresql_make_env_args} make -C src/interfaces/libpq -j${CPKT_DEPENDENCY_BUILD_JOBS}
        COMMAND ${CMAKE_COMMAND} -E chdir "${build_dir}"
        ${CMAKE_COMMAND} -E env ${postgresql_make_env_args} make -C src/interfaces/libpq-oauth -j${CPKT_DEPENDENCY_BUILD_JOBS}
      INSTALL_COMMAND ${CMAKE_COMMAND} -E remove_directory "${install_dir}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${stage_dir}/usr/include" "${stage_dir}/usr/lib"
        COMMAND ${CMAKE_COMMAND} -E chdir "${build_dir}"
        ${CMAKE_COMMAND} -E env ${postgresql_make_env_args} make -C src/interfaces/libpq install DESTDIR=${stage_dir}
        COMMAND ${CMAKE_COMMAND} -E chdir "${build_dir}"
        ${CMAKE_COMMAND} -E env ${postgresql_make_env_args} make -C src/interfaces/libpq-oauth install DESTDIR=${stage_dir}
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${stage_dir}/usr/include" "${install_dir}/include"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${stage_dir}/usr/lib" "${install_dir}/lib"
        COMMAND ${postgresql_darwin_install_name_normalize_command}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${build_dir}/src/common/libpgcommon_shlib${CMAKE_STATIC_LIBRARY_SUFFIX}"
          "${common_static_library}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${build_dir}/src/port/libpgport${CMAKE_STATIC_LIBRARY_SUFFIX}"
          "${port_static_library}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${source_dir}/src/include/postgres_ext.h"
          "${install_dir}/include/postgres_ext.h"
        COMMAND ${CMAKE_COMMAND} -E make_directory
          "${install_dir}/include/libpq"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${source_dir}/src/include/libpq/libpq-fs.h"
          "${install_dir}/include/libpq/libpq-fs.h"
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${install_dir}/share"
        COMMAND ${strip_install_command}
      BUILD_BYPRODUCTS "${static_library}" "${common_static_library}" "${port_static_library}" "${oauth_static_library}" "${oauth_shared_library}" "${shared_library}"
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
endif()
add_library(cpkt::postgresql_static STATIC IMPORTED GLOBAL)
set_target_properties(cpkt::postgresql_static PROPERTIES
    IMPORTED_LOCATION "${static_library}"
    INTERFACE_INCLUDE_DIRECTORIES "${install_dir}/include"
    INTERFACE_LINK_LIBRARIES "${oauth_static_library};${common_static_library};${port_static_library};cpkt::openldap_static;cpkt::gssapi_krb5_static;cpkt::cyrus_sasl_static;cpkt::curl_static;cpkt::openssl_ssl_static;cpkt::openssl_crypto_static;cpkt::zlib_static;${CMAKE_DL_LIBS};Threads::Threads;m")
add_library(cpkt::postgresql_shared SHARED IMPORTED GLOBAL)
set_target_properties(cpkt::postgresql_shared PROPERTIES
    IMPORTED_LOCATION "${shared_library}"
    INTERFACE_INCLUDE_DIRECTORIES "${install_dir}/include"
    INTERFACE_LINK_LIBRARIES "cpkt::openldap_shared;cpkt::gssapi_krb5_shared;cpkt::cyrus_sasl_shared;cpkt::curl_shared;cpkt::openssl_ssl_shared;cpkt::openssl_crypto_shared;cpkt::zlib_shared")
if(CPKT_BUILD_DEPENDENCIES)
add_dependencies(cpkt::postgresql_static ${project_name})
add_dependencies(cpkt::postgresql_shared ${project_name})
cpkt_record_dependency_target(${project_name})
else()
cpkt_require_dependency_file("${static_library}" "PostgreSQL libpq static library")
cpkt_require_dependency_file("${common_static_library}" "PostgreSQL frontend common static library")
cpkt_require_dependency_file("${port_static_library}" "PostgreSQL frontend port static library")
cpkt_require_dependency_file("${oauth_static_library}" "PostgreSQL OAuth static library")
cpkt_require_dependency_file("${oauth_shared_library}" "PostgreSQL OAuth loadable module")
cpkt_require_dependency_file("${shared_library}" "PostgreSQL libpq shared library")
cpkt_require_dependency_file("${install_dir}/include/libpq-fe.h" "PostgreSQL libpq header")
cpkt_require_dependency_file("${install_dir}/include/libpq/libpq-fs.h" "PostgreSQL large-object flags header")
endif()
set(CPKT_POSTGRESQL_PREFIX "${install_dir}" PARENT_SCOPE)
endfunction()
function(cpkt_add_iodbc)
set(project_name cpkt_iodbc_project)
set(prefix_dir "${CPKT_DEPENDENCY_BUILD_ROOT}/iodbc")
set(source_dir "${prefix_dir}/src")
set(build_dir "${prefix_dir}/build")
set(stage_dir "${prefix_dir}/stage")
set(install_dir "${CPKT_EXTERNAL_ROOT}/iodbc/install")
set(iodbc_static_library "${install_dir}/lib/libiodbc${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(iodbc_shared_library "${install_dir}/lib/libiodbc${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(inst_static_library "${install_dir}/lib/libiodbcinst${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(inst_shared_library "${install_dir}/lib/libiodbcinst${CMAKE_SHARED_LIBRARY_SUFFIX}")
cpkt_get_target_triple(target_triple)
cpkt_get_external_c_flags(external_cflags)
cpkt_get_autotools_link_flags(external_ldflags)
set(env_args "")
cpkt_append_pinned_external_toolchain_env_args(env_args)
set(iodbc_cflags "${external_cflags} -fPIC -std=gnu89")
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
string(APPEND iodbc_cflags
      " -Wno-deprecated-non-prototype -Wno-pointer-sign")
endif()
list(APPEND env_args
    "CFLAGS=${iodbc_cflags}"
    "LDFLAGS=${external_ldflags}")
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
list(APPEND env_args
      "LD=${CMAKE_LINKER}"
      "STRIP=true")
endif()
set(darwin_normalize_command ${CMAKE_COMMAND} -E true)
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
set(darwin_normalize_command
      ${CMAKE_COMMAND}
        -DCPKT_DARWIN_LIBRARY_DIR=${install_dir}/lib
        -DCPKT_DARWIN_STAGE_LIBRARY_DIR=${stage_dir}/usr/lib
        -DCPKT_DARWIN_INSTALL_NAME_TOOL=${CMAKE_INSTALL_NAME_TOOL}
        -DCPKT_DARWIN_OTOOL=${CPKT_OTOOL}
        -P ${CMAKE_SOURCE_DIR}/cmake/normalize_darwin_dylib_install_names.cmake)
endif()
cpkt_get_strip_dependency_install_command(strip_install_command "${install_dir}")
if(CPKT_BUILD_DEPENDENCIES)
file(MAKE_DIRECTORY "${install_dir}/include" "${install_dir}/lib")
endif()
if(CPKT_BUILD_DEPENDENCIES)
cpkt_cached_external_project_add(${project_name}
      URL "https://github.com/openlink/iODBC/releases/download/v${CPKT_IODBC_VERSION}/libiodbc-${CPKT_IODBC_VERSION}.tar.gz"
      URL_HASH "SHA256=3898b32d07961360f6f2cf36db36036b719a230e476469258a80f32243e845fa"
      DOWNLOAD_NAME "libiodbc-${CPKT_IODBC_VERSION}.tar.gz"
      PREFIX "${prefix_dir}"
      DOWNLOAD_DIR "${CPKT_DOWNLOAD_ROOT}"
      SOURCE_DIR "${source_dir}"
      BINARY_DIR "${build_dir}"
      STAMP_DIR "${prefix_dir}/stamp"
      TMP_DIR "${prefix_dir}/tmp"
      PATCH_COMMAND ${CMAKE_COMMAND}
        -DCPKT_PATCH_WORKING_DIRECTORY=${source_dir}
        -DCPKT_PATCH_SERIES=${CMAKE_SOURCE_DIR}/cmake/patches/iodbc.series
        -P ${CMAKE_SOURCE_DIR}/cmake/apply_patch_series.cmake
      TIMEOUT ${CPKT_DEPENDENCY_DOWNLOAD_TIMEOUT}
      INACTIVITY_TIMEOUT ${CPKT_DEPENDENCY_DOWNLOAD_INACTIVITY_TIMEOUT}
      CONFIGURE_COMMAND ${CMAKE_COMMAND} -E chdir "${build_dir}"
        ${CMAKE_COMMAND} -E env ${env_args}
        "${source_dir}/configure"
        --host=${target_triple}
        --prefix=/usr
        --libdir=/usr/lib
        --includedir=/usr/include
        --sysconfdir=/etc
        --enable-static
        --enable-shared
        --disable-gui
        --disable-libodbc
      BUILD_COMMAND ${CMAKE_COMMAND} -E chdir "${build_dir}"
        ${CMAKE_COMMAND} -E env ${env_args} make -s -C iodbcinst -j${CPKT_DEPENDENCY_BUILD_JOBS}
        COMMAND ${CMAKE_COMMAND} -E chdir "${build_dir}"
        ${CMAKE_COMMAND} -E env ${env_args} make -s -C iodbc -j${CPKT_DEPENDENCY_BUILD_JOBS}
      INSTALL_COMMAND ${CMAKE_COMMAND} -E remove_directory "${install_dir}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${stage_dir}/usr/include" "${stage_dir}/usr/lib"
        COMMAND ${CMAKE_COMMAND} -E chdir "${build_dir}"
        ${CMAKE_COMMAND} -E env ${env_args} make -s -C include install DESTDIR=${stage_dir}
        COMMAND ${CMAKE_COMMAND} -E chdir "${build_dir}"
        ${CMAKE_COMMAND} -E env ${env_args} make -s -C iodbcinst install DESTDIR=${stage_dir}
        COMMAND ${CMAKE_COMMAND} -E chdir "${build_dir}"
        ${CMAKE_COMMAND} -E env ${env_args} make -s -C iodbc install DESTDIR=${stage_dir}
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${stage_dir}/usr/include" "${install_dir}/include"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${stage_dir}/usr/lib" "${install_dir}/lib"
        COMMAND ${darwin_normalize_command}
        COMMAND ${strip_install_command}
      BUILD_BYPRODUCTS "${iodbc_static_library}" "${iodbc_shared_library}"
        "${inst_static_library}" "${inst_shared_library}"
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
endif()
add_library(cpkt::iodbc_static STATIC IMPORTED GLOBAL)
set(iodbc_static_system_libraries "${CMAKE_DL_LIBS};Threads::Threads")
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
list(APPEND iodbc_static_system_libraries "-Wl,-framework,Carbon")
endif()
set_target_properties(cpkt::iodbc_static PROPERTIES
    IMPORTED_LOCATION "${iodbc_static_library}"
    INTERFACE_INCLUDE_DIRECTORIES "${install_dir}/include"
    INTERFACE_LINK_LIBRARIES "${iodbc_static_system_libraries}")
add_library(cpkt::iodbc_shared SHARED IMPORTED GLOBAL)
set_target_properties(cpkt::iodbc_shared PROPERTIES
    IMPORTED_LOCATION "${iodbc_shared_library}"
    INTERFACE_INCLUDE_DIRECTORIES "${install_dir}/include")
add_library(cpkt::iodbcinst_static STATIC IMPORTED GLOBAL)
set_target_properties(cpkt::iodbcinst_static PROPERTIES
    IMPORTED_LOCATION "${inst_static_library}"
    INTERFACE_INCLUDE_DIRECTORIES "${install_dir}/include"
    INTERFACE_LINK_LIBRARIES "${CMAKE_DL_LIBS};Threads::Threads")
add_library(cpkt::iodbcinst_shared SHARED IMPORTED GLOBAL)
set_target_properties(cpkt::iodbcinst_shared PROPERTIES
    IMPORTED_LOCATION "${inst_shared_library}"
    INTERFACE_INCLUDE_DIRECTORIES "${install_dir}/include")
add_library(cpkt::iodbc INTERFACE IMPORTED GLOBAL)
set_target_properties(cpkt::iodbc PROPERTIES
    INTERFACE_LINK_LIBRARIES "cpkt::iodbc_static;cpkt::iodbcinst_static")
if(CPKT_BUILD_DEPENDENCIES)
foreach(target_name cpkt::iodbc_static cpkt::iodbc_shared
        cpkt::iodbcinst_static cpkt::iodbcinst_shared)
add_dependencies(${target_name} ${project_name})
endforeach()
cpkt_record_dependency_target(${project_name})
else()
foreach(required_file "${iodbc_static_library}" "${iodbc_shared_library}"
        "${inst_static_library}" "${inst_shared_library}"
        "${install_dir}/include/sql.h" "${install_dir}/include/odbcinst.h")
cpkt_require_dependency_file("${required_file}" "iODBC artifact")
endforeach()
endif()
set(CPKT_IODBC_PREFIX "${install_dir}" PARENT_SCOPE)
endfunction()
function(cpkt_add_sqlite)
set(project_name "cpkt_sqlite_project")
set(headers_project_name "cpkt_sqlite_headers_project")
set(prefix_dir "${CPKT_DEPENDENCY_BUILD_ROOT}/sqlite")
set(source_dir "${prefix_dir}/src")
set(headers_source_dir "${prefix_dir}/headers-src")
set(build_dir "${prefix_dir}/build")
set(install_dir "${CPKT_EXTERNAL_ROOT}/sqlite/install")
set(stamp_dir "${prefix_dir}/stamp")
set(tmp_dir "${prefix_dir}/tmp")
set(static_library "${install_dir}/lib/libsqlite3${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(shared_library "${install_dir}/lib/libsqlite3${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(source_amalgamation "${CMAKE_SOURCE_DIR}/src/sqlite_native_amalgamation.c")
set(source_header "${source_dir}/sqlite3.h")
cpkt_get_external_c_flags(external_cflags)
separate_arguments(sqlite_cflags NATIVE_COMMAND "${external_cflags}")
set(sqlite_link_flags "")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
list(APPEND sqlite_link_flags "-Wl,--enable-new-dtags,-rpath,$ORIGIN")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
list(APPEND sqlite_link_flags "-Wl,-rpath,@loader_path")
list(APPEND sqlite_link_flags
      "-mmacosx-version-min=${CPKT_MACOS_DEPLOYMENT_TARGET}")
endif()
if(NOT "${CMAKE_SHARED_LINKER_FLAGS}" STREQUAL "")
separate_arguments(sqlite_user_link_flags NATIVE_COMMAND "${CMAKE_SHARED_LINKER_FLAGS}")
list(APPEND sqlite_link_flags ${sqlite_user_link_flags})
endif()
set(sqlite_dl_link_flags "")
foreach(sqlite_dl_library IN LISTS CMAKE_DL_LIBS)
if(IS_ABSOLUTE "${sqlite_dl_library}" OR sqlite_dl_library MATCHES "^-")
list(APPEND sqlite_dl_link_flags "${sqlite_dl_library}")
else()
list(APPEND sqlite_dl_link_flags "-l${sqlite_dl_library}")
endif()
endforeach()
set(sqlite_compile_definitions
    -DSQLITE_THREADSAFE=1
    -DSQLITE_ENABLE_COLUMN_METADATA
    -DSQLITE_ENABLE_CARRAY
    -DSQLITE_ENABLE_DBSTAT_VTAB
    -DSQLITE_ENABLE_DESERIALIZE
    -DSQLITE_ENABLE_FTS3
    -DSQLITE_ENABLE_FTS3_PARENTHESIS
    -DSQLITE_ENABLE_FTS4
    -DSQLITE_ENABLE_FTS5
    -DSQLITE_ENABLE_GEOPOLY
    -DSQLITE_ENABLE_MATH_FUNCTIONS
    -DSQLITE_ENABLE_NORMALIZE
    -DSQLITE_ENABLE_PREUPDATE_HOOK
    -DSQLITE_ENABLE_RTREE
    -DSQLITE_ENABLE_SESSION
    -DSQLITE_ENABLE_SETLK_TIMEOUT
    -DSQLITE_ENABLE_SNAPSHOT
    -DSQLITE_ENABLE_STMT_SCANSTATUS
    -DSQLITE_ENABLE_STMTVTAB
    -DSQLITE_ENABLE_UNLOCK_NOTIFY)
set(sqlite_thread_flags "")
set(sqlite_shared_link_flags "")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
list(APPEND sqlite_thread_flags -pthread)
set(sqlite_shared_real_library "libsqlite3.so.0.8.6")
set(sqlite_shared_abi_library "libsqlite3.so.0")
list(APPEND sqlite_shared_link_flags
      -shared
      "-Wl,-soname,${sqlite_shared_abi_library}")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
set(sqlite_shared_real_library "libsqlite3.0.dylib")
set(sqlite_shared_abi_library "libsqlite3.0.dylib")
list(APPEND sqlite_shared_link_flags
      -dynamiclib
      "-Wl,-install_name,@rpath/${sqlite_shared_abi_library}"
      -Wl,-compatibility_version,9.0.0
      -Wl,-current_version,9.6.0)
else()
message(FATAL_ERROR "SQLite has no shared-library recipe for ${CMAKE_SYSTEM_NAME}")
endif()
set(sqlite_shared_install_commands
    COMMAND ${CMAKE_COMMAND} -E copy "${build_dir}/${sqlite_shared_real_library}"
      "${install_dir}/lib/${sqlite_shared_real_library}")
if(NOT sqlite_shared_real_library STREQUAL sqlite_shared_abi_library)
list(APPEND sqlite_shared_install_commands
      COMMAND ${CMAKE_COMMAND} -E create_symlink "${sqlite_shared_real_library}"
        "${install_dir}/lib/${sqlite_shared_abi_library}")
endif()
list(APPEND sqlite_shared_install_commands
    COMMAND ${CMAKE_COMMAND} -E create_symlink "${sqlite_shared_abi_library}"
      "${shared_library}")
cpkt_get_strip_dependency_install_command(strip_install_command "${install_dir}")
if(CPKT_BUILD_DEPENDENCIES)
file(MAKE_DIRECTORY "${install_dir}/include" "${install_dir}/lib")
endif()
if(CPKT_BUILD_DEPENDENCIES)
set(sqlite_compile_command "${CMAKE_C_COMPILER}"
    ${sqlite_cflags} ${sqlite_compile_definitions} ${sqlite_thread_flags}
    -fPIC -I "${source_dir}" -c "${source_amalgamation}"
    -o "${build_dir}/sqlite3.o")
# Keep the effective production argv available when a verified warm node
# replaces its build rule. The warning gate must still use current flags.
include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CpktPackageMetadata.cmake")
set(sqlite_compile_json "[]")
set(sqlite_compile_index 0)
foreach(argument IN LISTS sqlite_compile_command)
  cpkt_json_quote(quoted_argument "${argument}")
  string(JSON sqlite_compile_json SET "${sqlite_compile_json}" ${sqlite_compile_index} "${quoted_argument}")
  math(EXPR sqlite_compile_index "${sqlite_compile_index} + 1")
endforeach()
cpkt_validate_mutation_paths("${CMAKE_BINARY_DIR}/sqlite-compile-command.json")
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/sqlite-compile-command.json"
    CONTENT "${sqlite_compile_json}\n")
cpkt_cached_external_project_add(${headers_project_name}
      URL "https://www.sqlite.org/2026/sqlite-src-3530400.zip"
      URL_HASH "SHA256=d18fa15aec74d8c17e1463f861095adc01b5ad190256acb4f91d22f0368d232b"
      DOWNLOAD_NAME "sqlite-src-3530400.zip"
      PREFIX "${prefix_dir}/headers"
      DOWNLOAD_DIR "${CPKT_DOWNLOAD_ROOT}"
      SOURCE_DIR "${headers_source_dir}"
      BINARY_DIR "${prefix_dir}/headers-build"
      STAMP_DIR "${prefix_dir}/headers-stamp"
      TMP_DIR "${prefix_dir}/headers-tmp"
      TIMEOUT ${CPKT_DEPENDENCY_DOWNLOAD_TIMEOUT}
      INACTIVITY_TIMEOUT ${CPKT_DEPENDENCY_DOWNLOAD_INACTIVITY_TIMEOUT}
      CONFIGURE_COMMAND ${CMAKE_COMMAND} -E true
      BUILD_COMMAND ${CMAKE_COMMAND} -E true
      INSTALL_COMMAND ${CMAKE_COMMAND} -E true
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
cpkt_cached_external_project_add(${project_name}
      URL "https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip"
      URL_HASH "SHA256=1e71ddf93849c6a6ecf58b827c0692073d2dd7ee40196158068f7b29f422e87d"
      DOWNLOAD_NAME "sqlite-amalgamation-3530400.zip"
      PREFIX "${prefix_dir}"
      DOWNLOAD_DIR "${CPKT_DOWNLOAD_ROOT}"
      SOURCE_DIR "${source_dir}"
      BINARY_DIR "${build_dir}"
      STAMP_DIR "${stamp_dir}"
      TMP_DIR "${tmp_dir}"
      DEPENDS ${headers_project_name}
      TIMEOUT ${CPKT_DEPENDENCY_DOWNLOAD_TIMEOUT}
      INACTIVITY_TIMEOUT ${CPKT_DEPENDENCY_DOWNLOAD_INACTIVITY_TIMEOUT}
      CONFIGURE_COMMAND ${CMAKE_COMMAND} -E true
      BUILD_COMMAND ${CMAKE_COMMAND} -E make_directory "${build_dir}"
        COMMAND ${sqlite_compile_command}
        COMMAND ${CMAKE_AR} rcs "${build_dir}/libsqlite3${CMAKE_STATIC_LIBRARY_SUFFIX}"
          "${build_dir}/sqlite3.o"
        COMMAND ${CMAKE_RANLIB} "${build_dir}/libsqlite3${CMAKE_STATIC_LIBRARY_SUFFIX}"
        COMMAND ${CMAKE_C_COMPILER}
          ${sqlite_link_flags}
          ${sqlite_shared_link_flags}
          ${sqlite_thread_flags}
          -o "${build_dir}/${sqlite_shared_real_library}"
          "${build_dir}/sqlite3.o"
          -lm
          ${sqlite_dl_link_flags}
      INSTALL_COMMAND ${CMAKE_COMMAND} -E remove_directory "${install_dir}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${install_dir}/include" "${install_dir}/lib"
        COMMAND ${CMAKE_COMMAND} -E copy "${source_header}" "${install_dir}/include/sqlite3.h"
        COMMAND ${CMAKE_COMMAND} -E copy "${source_dir}/sqlite3ext.h"
          "${install_dir}/include/sqlite3ext.h"
        COMMAND ${CMAKE_COMMAND} -E copy
          "${headers_source_dir}/ext/session/sqlite3session.h"
          "${install_dir}/include/sqlite3session.h"
        COMMAND ${CMAKE_COMMAND} -E copy
          "${headers_source_dir}/ext/rtree/sqlite3rtree.h"
          "${install_dir}/include/sqlite3rtree.h"
        COMMAND ${CMAKE_COMMAND} -E copy
          "${headers_source_dir}/ext/fts5/fts5.h"
          "${install_dir}/include/fts5.h"
        COMMAND ${CMAKE_COMMAND} -E copy "${build_dir}/libsqlite3${CMAKE_STATIC_LIBRARY_SUFFIX}"
          "${static_library}"
        ${sqlite_shared_install_commands}
        COMMAND ${strip_install_command}
      BUILD_BYPRODUCTS "${static_library}" "${shared_library}"
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
endif()
add_library(cpkt::sqlite_static STATIC IMPORTED GLOBAL)
set_target_properties(cpkt::sqlite_static PROPERTIES
    IMPORTED_LOCATION "${static_library}"
    INTERFACE_INCLUDE_DIRECTORIES "${install_dir}/include"
    INTERFACE_LINK_LIBRARIES "m;${CMAKE_DL_LIBS};Threads::Threads")
add_library(cpkt::sqlite_shared SHARED IMPORTED GLOBAL)
set_target_properties(cpkt::sqlite_shared PROPERTIES
    IMPORTED_LOCATION "${shared_library}"
    INTERFACE_INCLUDE_DIRECTORIES "${install_dir}/include"
    INTERFACE_LINK_LIBRARIES "m;${CMAKE_DL_LIBS};Threads::Threads")
if(CPKT_BUILD_DEPENDENCIES)
add_dependencies(cpkt::sqlite_static ${project_name})
add_dependencies(cpkt::sqlite_shared ${project_name})
cpkt_record_dependency_target(${project_name})
else()
cpkt_require_dependency_file("${static_library}" "SQLite static library")
cpkt_require_dependency_file("${shared_library}" "SQLite shared library")
cpkt_require_dependency_file("${install_dir}/include/sqlite3.h" "SQLite header")
cpkt_require_dependency_file("${install_dir}/include/sqlite3ext.h" "SQLite extension header")
cpkt_require_dependency_file("${install_dir}/include/sqlite3session.h" "SQLite session header")
cpkt_require_dependency_file("${install_dir}/include/sqlite3rtree.h" "SQLite RTree header")
cpkt_require_dependency_file("${install_dir}/include/fts5.h" "SQLite FTS5 header")
endif()
set(CPKT_SQLITE_PREFIX "${install_dir}" PARENT_SCOPE)
endfunction()
function(cpkt_configure_dependencies)
  include("${CMAKE_SOURCE_DIR}/cmake/CpktCoreImports.cmake")
  cpkt_import_core()
cpkt_prepare_dependency_component(
    NAME postgresql
    BUILD_ROOT "${CPKT_DEPENDENCY_BUILD_ROOT}/postgresql"
    INSTALL_ROOT "${CPKT_EXTERNAL_ROOT}/postgresql/install"
    VARIABLES CPKT_POSTGRESQL_VERSION CPKT_CORE_PACKAGE_ID
    DEPENDS openldap cyrus-sasl krb5 curl zlib openssl
    INPUT_FILES
      "${CMAKE_SOURCE_DIR}/cmake/CpktDependencyContract.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/CpktDependencyArchiveCache.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/patch_postgresql_buildinfo.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/remove_postgresql_probe_rpaths.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/apply_patch_series.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/patch_postgresql_oauth_loader.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/normalize_darwin_dylib_install_names.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/strip_dependency_install_tree.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/patches/postgresql.series"
      "${CMAKE_SOURCE_DIR}/cmake/patches/postgresql_logging.patch"
    INPUT_FILES "${CMAKE_SOURCE_DIR}/cmake/CpktDependencies.cmake" )
cpkt_prepare_dependency_component(
    NAME iodbc
    BUILD_ROOT "${CPKT_DEPENDENCY_BUILD_ROOT}/iodbc"
    INSTALL_ROOT "${CPKT_EXTERNAL_ROOT}/iodbc/install"
    VARIABLES CPKT_IODBC_VERSION
    INPUT_FILES
      "${CMAKE_SOURCE_DIR}/cmake/CpktDependencyContract.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/CpktDependencyArchiveCache.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/patches/iodbc.series"
      "${CMAKE_SOURCE_DIR}/cmake/patches/iodbc_c89_sqlbigint.patch"
      "${CMAKE_SOURCE_DIR}/cmake/patches/iodbc_diag_native_error.patch"
      "${CMAKE_SOURCE_DIR}/cmake/apply_patch_series.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/normalize_darwin_dylib_install_names.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/strip_dependency_install_tree.cmake"
    INPUT_FILES "${CMAKE_SOURCE_DIR}/cmake/CpktDependencies.cmake" )
cpkt_prepare_dependency_component(
    NAME sqlite
    BUILD_ROOT "${CPKT_DEPENDENCY_BUILD_ROOT}/sqlite"
    INSTALL_ROOT "${CPKT_EXTERNAL_ROOT}/sqlite/install"
    VARIABLES CPKT_SQLITE_VERSION
    INPUT_FILES
      "${CMAKE_SOURCE_DIR}/cmake/CpktDependencyContract.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/CpktDependencyArchiveCache.cmake"
      "${CMAKE_SOURCE_DIR}/src/sqlite_native_amalgamation.c"
      "${CMAKE_SOURCE_DIR}/cmake/strip_dependency_install_tree.cmake"
    INPUT_FILES "${CMAKE_SOURCE_DIR}/cmake/CpktDependencies.cmake" )
set(_all_dependency_targets "")
foreach(_component IN LISTS CPKT_ACTIVE_COMPONENTS)
    string(JSON _external ERROR_VARIABLE _missing GET "${CPKT_INVENTORY}" components "${_component}" external)
    if(NOT _missing)
      continue()
    endif()
string(JSON _owner GET "${CPKT_INVENTORY}" components "${_component}" group)
string(REPLACE "-" "_" _function "cpkt_add_${_component}")
set(CPKT_BUILD_DEPENDENCIES OFF)
if(CPKT_DEPENDENCY_PRODUCER AND _owner STREQUAL CPKT_GROUP)
set(CPKT_BUILD_DEPENDENCIES ON)
endif()
get_property(_before GLOBAL PROPERTY CPKT_DEPENDENCY_TARGETS)
cmake_language(CALL "${_function}")
get_property(_after GLOBAL PROPERTY CPKT_DEPENDENCY_TARGETS)
set(_component_targets "${_after}")
if(_before)
list(REMOVE_ITEM _component_targets ${_before})
endif()
set_property(GLOBAL PROPERTY "CPKT_PRODUCER_TARGETS_${_component}" "${_component_targets}")
if(_component_targets)
set(_remaining "${_component_targets}")
list(POP_BACK _remaining _last_project)
get_property(_complete GLOBAL PROPERTY "CPKT_COMPONENT_COMPLETE_${_component}")
ExternalProject_Add_Step(${_last_project} component-complete
  COMMAND "${CMAKE_COMMAND}" -E touch "${_complete}"
  DEPENDEES install DEPENDS ${_remaining} BYPRODUCTS "${_complete}")
add_custom_target(cpkt_deps_${_component} DEPENDS ${_component_targets})
list(APPEND _all_dependency_targets ${_component_targets})
endif()
endforeach()
if(CPKT_DEPENDENCY_PRODUCER)
add_custom_target(cpkt_deps_all DEPENDS ${_all_dependency_targets})
add_custom_target(cpkt_deps DEPENDS cpkt_deps_all)
endif()
endfunction()
