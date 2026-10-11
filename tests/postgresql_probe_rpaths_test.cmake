if(NOT DEFINED CPKT_SOURCE_DIR OR NOT DEFINED CPKT_TEST_BINARY_DIR)
  message(FATAL_ERROR "CPKT_SOURCE_DIR and CPKT_TEST_BINARY_DIR are required")
endif()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef _suffix)
set(_work_dir "${CPKT_TEST_BINARY_DIR}/postgresql-probe-rpaths-${_suffix}")
file(MAKE_DIRECTORY "${_work_dir}/src")
set(_makefile "${_work_dir}/src/Makefile.global")
set(_base "-L/bundle/lib -Wl,-rpath,@loader_path")
set(_probe "${_base} -Wl,-rpath,/private/probe/lib")
file(WRITE "${_makefile}"
  "LDFLAGS = \$(LDFLAGS_INTERNAL) -isysroot \$(PG_SYSROOT) ${_probe} -Wl,--as-needed\n")

execute_process(
  COMMAND "${CMAKE_COMMAND}"
    "-DCPKT_POSTGRESQL_BUILD_DIR=${_work_dir}"
    "-DCPKT_POSTGRESQL_BASE_LDFLAGS=${_base}"
    "-DCPKT_POSTGRESQL_PROBE_LDFLAGS=${_probe}"
    -P "${CPKT_SOURCE_DIR}/cmake/remove_postgresql_probe_rpaths.cmake"
  RESULT_VARIABLE _result ERROR_VARIABLE _error)
if(NOT _result EQUAL 0)
  message(FATAL_ERROR "probe rpath cleanup failed: ${_error}")
endif()
file(READ "${_makefile}" _actual)
if(NOT _actual STREQUAL
    "LDFLAGS = \$(LDFLAGS_INTERNAL) -isysroot \$(PG_SYSROOT) ${_base} -Wl,--as-needed\n")
  message(FATAL_ERROR "probe rpaths leaked into PostgreSQL build flags: ${_actual}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}"
    "-DCPKT_POSTGRESQL_BUILD_DIR=${_work_dir}"
    "-DCPKT_POSTGRESQL_BASE_LDFLAGS=${_base}"
    "-DCPKT_POSTGRESQL_PROBE_LDFLAGS=${_probe}"
    -P "${CPKT_SOURCE_DIR}/cmake/remove_postgresql_probe_rpaths.cmake"
  RESULT_VARIABLE _invalid_result OUTPUT_QUIET ERROR_QUIET)
if(_invalid_result EQUAL 0)
  message(FATAL_ERROR "cleanup accepted missing probe rpaths")
endif()

file(REMOVE_RECURSE "${_work_dir}")
