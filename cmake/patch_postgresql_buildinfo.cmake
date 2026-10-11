if(NOT DEFINED CPKT_POSTGRESQL_SOURCE_DIR OR CPKT_POSTGRESQL_SOURCE_DIR STREQUAL "")
  message(FATAL_ERROR "CPKT_POSTGRESQL_SOURCE_DIR is required")
endif()

set(config_info "${CPKT_POSTGRESQL_SOURCE_DIR}/src/common/config_info.c")
if(NOT EXISTS "${config_info}")
  message(FATAL_ERROR "PostgreSQL build metadata input is missing: ${config_info}")
endif()

file(READ "${config_info}" config_info_contents)
string(REPLACE
  "configdata[i].setting = pstrdup(CONFIGURE_ARGS);"
  "configdata[i].setting = pstrdup(_(\"not recorded\"));"
  config_info_contents "${config_info_contents}")
foreach(metadata_macro IN ITEMS
    VAL_CC
    VAL_CPPFLAGS
    VAL_CFLAGS
    VAL_CFLAGS_SL
    VAL_LDFLAGS
    VAL_LDFLAGS_EX
    VAL_LDFLAGS_SL
    VAL_LIBS)
  string(REPLACE
    "pstrdup(${metadata_macro})"
    "pstrdup(_(\"not recorded\"))"
    config_info_contents "${config_info_contents}")
endforeach()
if(config_info_contents MATCHES "pstrdup\\((CONFIGURE_ARGS|VAL_[A-Z_]+)\\)")
  message(FATAL_ERROR "PostgreSQL build metadata patch did not apply")
endif()
file(WRITE "${config_info}" "${config_info_contents}")
