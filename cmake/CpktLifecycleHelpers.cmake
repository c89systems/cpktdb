include_guard(GLOBAL)
# The collection is the installed skill or this checkout's exact vendored copy.
# Borrowing another checkout's sources through an override is outside the
# lifecycle interface; source reconstruction selects its own copied helpers.
if(NOT CPKT_LIFECYCLE_SCRIPTS_DIR)
  if(DEFINED ENV{CPKT_LIFECYCLE_SCRIPTS_DIR} AND NOT "$ENV{CPKT_LIFECYCLE_SCRIPTS_DIR}" STREQUAL "")
    set(CPKT_LIFECYCLE_SCRIPTS_DIR "$ENV{CPKT_LIFECYCLE_SCRIPTS_DIR}")
  else()
    get_filename_component(CPKT_LIFECYCLE_SCRIPTS_DIR "${CMAKE_CURRENT_LIST_DIR}/../scripts" ABSOLUTE)
  endif()
endif()
foreach(helper require-host-bash cpkt-archive-cache cpkt-toolchains cpkt-aflpp cpkt-afl-runtime)
  if(NOT EXISTS "${CPKT_LIFECYCLE_SCRIPTS_DIR}/${helper}.sh")
    message(FATAL_ERROR "Incomplete declared lifecycle helper collection: ${CPKT_LIFECYCLE_SCRIPTS_DIR}")
  endif()
endforeach()
set(CPKT_LIFECYCLE_SCRIPTS_DIR "${CPKT_LIFECYCLE_SCRIPTS_DIR}" CACHE PATH "Declared installed or vendored lifecycle helpers" FORCE)
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES CPKT_LIFECYCLE_SCRIPTS_DIR CPKT_TARGET_ARCH CPKT_TARGET_OS CPKT_TARGET_LIBC)
