foreach(_required
    CPKT_POSTGRESQL_BUILD_DIR
    CPKT_POSTGRESQL_PROBE_LDFLAGS
    CPKT_POSTGRESQL_BASE_LDFLAGS)
  if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
    message(FATAL_ERROR "${_required} is required")
  endif()
endforeach()

set(_makefile "${CPKT_POSTGRESQL_BUILD_DIR}/src/Makefile.global")
if(NOT EXISTS "${_makefile}")
  message(FATAL_ERROR "PostgreSQL build flags are missing: ${_makefile}")
endif()
string(FIND "${CPKT_POSTGRESQL_PROBE_LDFLAGS}"
  "${CPKT_POSTGRESQL_BASE_LDFLAGS} " _base_prefix)
if(NOT _base_prefix EQUAL 0)
  message(FATAL_ERROR "probe linker flags do not extend the base flags")
endif()

file(READ "${_makefile}" _contents)
string(REGEX MATCH "(^|\n)LDFLAGS = [^\n]*" _assignment "${_contents}")
string(FIND "${_assignment}" "${CPKT_POSTGRESQL_PROBE_LDFLAGS}" _probe_index)
if(_probe_index EQUAL -1)
  message(FATAL_ERROR "PostgreSQL build flags did not contain the probe rpaths")
endif()
string(REPLACE "${CPKT_POSTGRESQL_PROBE_LDFLAGS}"
  "${CPKT_POSTGRESQL_BASE_LDFLAGS}" _clean_assignment "${_assignment}")
string(REPLACE "${_assignment}" "${_clean_assignment}" _contents "${_contents}")
string(FIND "${_clean_assignment}" "${CPKT_POSTGRESQL_PROBE_LDFLAGS}" _remaining_index)
if(NOT _remaining_index EQUAL -1)
  message(FATAL_ERROR "PostgreSQL probe rpaths remain in the build flags")
endif()
file(WRITE "${_makefile}" "${_contents}")
