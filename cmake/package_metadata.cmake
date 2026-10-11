# Existing supported static/shared discovery and platform closures.
function(cpkt_metadata_file operation)
  set(args ${ARGN})
  if(operation STREQUAL "WRITE" OR operation STREQUAL "MAKE_DIRECTORY")
    list(GET args 0 path)
    string(REGEX MATCH "/lib/cmake/([^/]+)" matched "${path}")
    if(matched AND NOT CMAKE_MATCH_1 IN_LIST CPKT_METADATA_CMAKE)
      return()
    endif()
    string(REGEX MATCH "/lib/pkgconfig/([^/]+)\\.pc$" matched "${path}")
    if(matched AND NOT CMAKE_MATCH_1 IN_LIST CPKT_METADATA_PC)
      return()
    endif()
  endif()
  if(operation STREQUAL "WRITE")
    # Expanding ARGN as a list destroys semicolons in link interfaces.
    set(content "")
    math(EXPR last "${ARGC} - 1")
    foreach(index RANGE 2 ${last})
      string(APPEND content "${ARGV${index}}")
    endforeach()
    file(WRITE "${ARGV1}" "${content}")
  elseif(operation STREQUAL "MAKE_DIRECTORY")
    file(MAKE_DIRECTORY ${ARGN})
  else()
    message(FATAL_ERROR "Unsupported metadata file operation: ${operation}")
  endif()
endfunction()
set(_cpkt_static_library_suffix ".a")
if(CPKT_TARGET_ID STREQUAL "arm64-apple-darwin")
  set(_cpkt_shared_library_suffix ".dylib")
  set(_cpkt_postgresql_shared_library_name "libpq.5.dylib")
else()
  set(_cpkt_shared_library_suffix ".so")
  set(_cpkt_postgresql_shared_library_name "libpq.so.5")
endif()

function(cpkt_write_config_version package_dir config_stem package_version)
  if(NOT package_dir IN_LIST CPKT_METADATA_CMAKE)
    return()
  endif()
  cpkt_metadata_file(MAKE_DIRECTORY "${_stage_root}/lib/cmake/${package_dir}")
  cpkt_metadata_file(WRITE "${_stage_root}/lib/cmake/${package_dir}/${config_stem}ConfigVersion.cmake"
    "set(PACKAGE_VERSION \"${package_version}\")\n"
    "if(PACKAGE_FIND_VERSION VERSION_EQUAL PACKAGE_VERSION)\n"
    "  set(PACKAGE_VERSION_EXACT TRUE)\n"
    "endif()\n"
    "if(PACKAGE_FIND_VERSION VERSION_LESS_EQUAL PACKAGE_VERSION)\n"
    "  set(PACKAGE_VERSION_COMPATIBLE TRUE)\n"
    "endif()\n"
  )
endfunction()
cpkt_metadata_file(MAKE_DIRECTORY "${_stage_root}/lib/cmake/CpktIodbc")
cpkt_metadata_file(WRITE "${_stage_root}/lib/cmake/CpktIodbc/CpktIodbcConfig.cmake"
  "include(CMakeFindDependencyMacro)\n"
  "find_dependency(Threads REQUIRED)\n"
  "get_filename_component(_cpkt_iodbc_prefix \"\${CMAKE_CURRENT_LIST_DIR}/../../..\" ABSOLUTE)\n"
  "set(CpktIodbc_FOUND TRUE)\n"
  "set(CpktIodbc_VERSION \"${CPKT_IODBC_VERSION}\")\n"
  "set(_cpkt_iodbc_static_system_libraries Threads::Threads)\n"
  "if(CMAKE_SYSTEM_NAME STREQUAL \"Linux\")\n"
  "  list(APPEND _cpkt_iodbc_static_system_libraries dl)\n"
  "endif()\n"
  "foreach(_cpkt_iodbc_component iodbc iodbcinst)\n"
  "  set(_cpkt_iodbc_component_system_libraries \"\${_cpkt_iodbc_static_system_libraries}\")\n"
  "  if(_cpkt_iodbc_component STREQUAL \"iodbc\" AND CMAKE_SYSTEM_NAME STREQUAL \"Darwin\")\n"
  "    list(APPEND _cpkt_iodbc_component_system_libraries \"-Wl,-framework,Carbon\")\n"
  "  endif()\n"
  "  if(NOT TARGET cpkt::\${_cpkt_iodbc_component}_static)\n"
  "    add_library(cpkt::\${_cpkt_iodbc_component}_static STATIC IMPORTED)\n"
  "    set_target_properties(cpkt::\${_cpkt_iodbc_component}_static PROPERTIES IMPORTED_LOCATION \"\${_cpkt_iodbc_prefix}/lib/lib\${_cpkt_iodbc_component}${_cpkt_static_library_suffix}\" INTERFACE_INCLUDE_DIRECTORIES \"\${_cpkt_iodbc_prefix}/include\" INTERFACE_LINK_LIBRARIES \"\${_cpkt_iodbc_component_system_libraries}\")\n"
  "  endif()\n"
  "  if(NOT TARGET cpkt::\${_cpkt_iodbc_component}_shared)\n"
  "    add_library(cpkt::\${_cpkt_iodbc_component}_shared SHARED IMPORTED)\n"
  "    set_target_properties(cpkt::\${_cpkt_iodbc_component}_shared PROPERTIES IMPORTED_LOCATION \"\${_cpkt_iodbc_prefix}/lib/lib\${_cpkt_iodbc_component}${_cpkt_shared_library_suffix}\" INTERFACE_INCLUDE_DIRECTORIES \"\${_cpkt_iodbc_prefix}/include\")\n"
  "  endif()\n"
  "endforeach()\n"
  "if(NOT TARGET cpkt::iodbc)\n"
  "  add_library(cpkt::iodbc INTERFACE IMPORTED)\n"
  "  set_target_properties(cpkt::iodbc PROPERTIES INTERFACE_LINK_LIBRARIES \"cpkt::iodbc_static;cpkt::iodbcinst_static\")\n"
  "endif()\n")
cpkt_write_config_version("CpktIodbc" "CpktIodbc" "${CPKT_IODBC_VERSION}")
set(_cpkt_sqlite_static_system_libraries "m;Threads::Threads")
set(_cpkt_sqlite_static_private_pc_libraries "-lm -pthread")
if(CPKT_TARGET_ID MATCHES "-linux-")
  list(APPEND _cpkt_sqlite_static_system_libraries "${CMAKE_DL_LIBS}")
  string(APPEND _cpkt_sqlite_static_private_pc_libraries " -ldl")
endif()
cpkt_metadata_file(MAKE_DIRECTORY "${_stage_root}/lib/cmake/SQLite3")
cpkt_metadata_file(WRITE "${_stage_root}/lib/cmake/SQLite3/SQLite3Config.cmake"
  "include(CMakeFindDependencyMacro)\n"
  "find_dependency(Threads REQUIRED)\n"
  "get_filename_component(_cpkt_sqlite_prefix \"\${CMAKE_CURRENT_LIST_DIR}/../../..\" ABSOLUTE)\n"
  "set(SQLite3_FOUND TRUE)\n"
  "set(SQLite3_VERSION \"${CPKT_SQLITE_VERSION}\")\n"
  "set(SQLite3_INCLUDE_DIRS \"\${_cpkt_sqlite_prefix}/include\")\n"
  "set(SQLite3_LIBRARY \"\${_cpkt_sqlite_prefix}/lib/libsqlite3${_cpkt_static_library_suffix}\")\n"
  "if(NOT TARGET SQLite::SQLite3)\n"
  "  add_library(SQLite::SQLite3 STATIC IMPORTED)\n"
  "  set_target_properties(SQLite::SQLite3 PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${SQLite3_LIBRARY}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${SQLite3_INCLUDE_DIRS}\"\n"
  "    INTERFACE_LINK_LIBRARIES \"${_cpkt_sqlite_static_system_libraries}\"\n"
  "  )\n"
  "endif()\n"
  "if(NOT TARGET cpkt::sqlite3_shared)\n"
  "  add_library(cpkt::sqlite3_shared SHARED IMPORTED)\n"
  "  set_target_properties(cpkt::sqlite3_shared PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_sqlite_prefix}/lib/libsqlite3${_cpkt_shared_library_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${SQLite3_INCLUDE_DIRS}\"\n"
  "  )\n"
  "endif()\n"
)
cpkt_write_config_version("SQLite3" "SQLite3" "${CPKT_SQLITE_VERSION}")

cpkt_metadata_file(MAKE_DIRECTORY "${_stage_root}/lib/cmake/CpktSqlite")
cpkt_metadata_file(WRITE "${_stage_root}/lib/cmake/CpktSqlite/CpktSqliteConfig.cmake"
  "include(CMakeFindDependencyMacro)\n"
  "get_filename_component(_cpkt_sqlite_facade_prefix \"\${CMAKE_CURRENT_LIST_DIR}/../../..\" ABSOLUTE)\n"
  "set(SQLite3_DIR \"\${_cpkt_sqlite_facade_prefix}/lib/cmake/SQLite3\")\n"
  "find_dependency(SQLite3 CONFIG REQUIRED)\n"
  "set(CpktSqlite_FOUND TRUE)\n"
  "set(CpktSqlite_VERSION \"${CPKT_SQLITE_VERSION}\")\n"
  "if(NOT TARGET cpkt::sqlite)\n"
  "  add_library(cpkt::sqlite STATIC IMPORTED)\n"
  "  set_target_properties(cpkt::sqlite PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_sqlite_facade_prefix}/lib/libcpkt_sqlite${_cpkt_static_library_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${_cpkt_sqlite_facade_prefix}/include\"\n"
  "    INTERFACE_LINK_LIBRARIES SQLite::SQLite3\n"
  "  )\n"
  "endif()\n"
  "if(NOT TARGET cpkt::sqlite_shared)\n"
  "  add_library(cpkt::sqlite_shared SHARED IMPORTED)\n"
  "  set_target_properties(cpkt::sqlite_shared PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_sqlite_facade_prefix}/lib/libcpkt_sqlite${_cpkt_shared_library_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${_cpkt_sqlite_facade_prefix}/include\"\n"
  "    INTERFACE_LINK_LIBRARIES cpkt::sqlite3_shared\n"
  "  )\n"
  "endif()\n"
)
cpkt_write_config_version("CpktSqlite" "CpktSqlite" "${CPKT_SQLITE_VERSION}")
set(_cpkt_postgres_static_system_libraries "m")
set(_cpkt_postgres_static_private_pc_libraries "-lm")
if(CPKT_TARGET_ID MATCHES "-linux-")
  list(APPEND _cpkt_postgres_static_system_libraries resolv)
  string(APPEND _cpkt_postgres_static_private_pc_libraries " -lresolv -ldl -pthread")
elseif(CPKT_TARGET_ID MATCHES "-apple-darwin$")
  list(APPEND _cpkt_postgres_static_system_libraries resolv "-Wl,-framework,Kerberos")
  string(APPEND _cpkt_postgres_static_private_pc_libraries " -lresolv -Wl,-framework,Kerberos")
endif()
cpkt_metadata_file(MAKE_DIRECTORY "${_stage_root}/lib/cmake/CpktPostgres")
cpkt_metadata_file(WRITE "${_stage_root}/lib/cmake/CpktPostgres/CpktPostgresConfig.cmake"
  "include(CMakeFindDependencyMacro)\n"
  "find_dependency(Threads REQUIRED)\n"
  "get_filename_component(_cpkt_postgres_prefix \"\${CMAKE_CURRENT_LIST_DIR}/../../..\" ABSOLUTE)\n"
  "find_dependency(OpenSSL CONFIG REQUIRED NO_CMAKE_FIND_ROOT_PATH)\n"
  "find_dependency(ZLIB CONFIG REQUIRED NO_CMAKE_FIND_ROOT_PATH)\n"
  "find_dependency(CURL CONFIG REQUIRED NO_CMAKE_FIND_ROOT_PATH)\n"
  "find_dependency(OpenLDAP CONFIG REQUIRED NO_CMAKE_FIND_ROOT_PATH)\n"
  "set(CpktPostgres_FOUND TRUE)\n"
  "set(CpktPostgres_VERSION \"${CPKT_POSTGRESQL_VERSION}\")\n"
  "set(_cpkt_postgres_static_system_libraries \"${_cpkt_postgres_static_system_libraries}\")\n"
  "if(NOT TARGET cpkt::postgres)\n"
  "  add_library(cpkt::postgres STATIC IMPORTED)\n"
  "  set_target_properties(cpkt::postgres PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_postgres_prefix}/lib/libcpkt_postgres${_cpkt_static_library_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${_cpkt_postgres_prefix}/include\"\n"
    "    INTERFACE_LINK_LIBRARIES \"\${_cpkt_postgres_prefix}/lib/libpq${_cpkt_static_library_suffix};\${_cpkt_postgres_prefix}/lib/libpq-oauth${_cpkt_static_library_suffix};\${_cpkt_postgres_prefix}/lib/libpgcommon_shlib${_cpkt_static_library_suffix};\${_cpkt_postgres_prefix}/lib/libpgport${_cpkt_static_library_suffix};cpkt::openldap_static;CURL::libcurl;OpenSSL::SSL;OpenSSL::Crypto;ZLIB::ZLIB;\${CMAKE_DL_LIBS};Threads::Threads;\${_cpkt_postgres_static_system_libraries}\"\n"
  "  )\n"
  "endif()\n"
  "if(NOT TARGET cpkt::postgres_shared)\n"
  "  add_library(cpkt::postgres_shared SHARED IMPORTED)\n"
  "  set_target_properties(cpkt::postgres_shared PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_postgres_prefix}/lib/libcpkt_postgres${_cpkt_shared_library_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${_cpkt_postgres_prefix}/include\"\n"
    "    INTERFACE_LINK_LIBRARIES \"\${_cpkt_postgres_prefix}/lib/${_cpkt_postgresql_shared_library_name};cpkt::openldap_shared;cpkt::curl_shared;cpkt::openssl_ssl_shared;cpkt::openssl_crypto_shared;cpkt::zlib_shared\"\n"
  "  )\n"
  "endif()\n"
)
cpkt_write_config_version("CpktPostgres" "CpktPostgres" "${CPKT_POSTGRESQL_VERSION}")
cpkt_metadata_file(WRITE "${_stage_root}/lib/pkgconfig/cpkt-postgres.pc"
  "prefix=\${pcfiledir}/../..\n"
  "exec_prefix=\${prefix}\n"
  "libdir=\${prefix}/lib\n"
  "includedir=\${prefix}/include\n"
  "\n"
  "Name: cpkt-postgres\n"
  "Description: C89 PostgreSQL/libpq facade from cpkt\n"
  "Version: ${CPKT_POSTGRESQL_VERSION}\n"
  "Requires.private: libcurl libssl libcrypto zlib ldap\n"
  "Libs: -L\${libdir} -lcpkt_postgres\n"
  "Libs.private: -lpq -lpq-oauth -lpgcommon_shlib -lpgport ${_cpkt_postgres_static_private_pc_libraries}\n"
  "Cflags: -I\${includedir}\n"
)
set(_cpkt_iodbc_static_pc_libraries "-pthread")
if(CPKT_TARGET_ID MATCHES "-linux-")
  string(APPEND _cpkt_iodbc_static_pc_libraries " -ldl")
elseif(CPKT_TARGET_ID STREQUAL "arm64-apple-darwin")
  string(APPEND _cpkt_iodbc_static_pc_libraries " -Wl,-framework,Carbon")
endif()
cpkt_metadata_file(WRITE "${_stage_root}/lib/pkgconfig/libiodbc.pc"
  "prefix=\${pcfiledir}/../..\nlibdir=\${prefix}/lib\nincludedir=\${prefix}/include\n\n"
  "Name: iODBC\nDescription: iODBC driver manager and installer from cpkt\nVersion: ${CPKT_IODBC_VERSION}\n"
  "Libs: -L\${libdir} -liodbc -liodbcinst\nLibs.private: ${_cpkt_iodbc_static_pc_libraries}\nCflags: -I\${includedir}\n")
cpkt_metadata_file(WRITE "${_stage_root}/lib/pkgconfig/cpkt-iodbc.pc"
  "prefix=\${pcfiledir}/../..\nlibdir=\${prefix}/lib\nincludedir=\${prefix}/include\n\n"
  "Name: cpkt-iodbc\nDescription: Full C89 iODBC API from cpkt\nVersion: ${CPKT_IODBC_VERSION}\n"
  "Libs: -L\${libdir} -liodbc -liodbcinst\nLibs.private: ${_cpkt_iodbc_static_pc_libraries}\nCflags: -I\${includedir}\n")
cpkt_metadata_file(WRITE "${_stage_root}/lib/pkgconfig/sqlite3.pc"
  "prefix=\${pcfiledir}/../..\n"
  "exec_prefix=\${prefix}\n"
  "libdir=\${prefix}/lib\n"
  "includedir=\${prefix}/include\n"
  "\n"
  "Name: SQLite\n"
  "Description: SQLite embedded SQL database from cpkt\n"
  "Version: ${CPKT_SQLITE_VERSION}\n"
  "Libs: -L\${libdir} -lsqlite3\n"
  "Libs.private: ${_cpkt_sqlite_static_private_pc_libraries}\n"
  "Cflags: -I\${includedir}\n"
)
cpkt_metadata_file(WRITE "${_stage_root}/lib/pkgconfig/cpkt-sqlite.pc"
  "prefix=\${pcfiledir}/../..\n"
  "exec_prefix=\${prefix}\n"
  "libdir=\${prefix}/lib\n"
  "includedir=\${prefix}/include\n"
  "\n"
  "Name: cpkt-sqlite\n"
  "Description: C89 SQLite facade from cpkt\n"
  "Version: ${CPKT_SQLITE_VERSION}\n"
  "Requires.private: sqlite3\n"
  "Libs: -L\${libdir} -lcpkt_sqlite\n"
  "Cflags: -I\${includedir}\n"
)
