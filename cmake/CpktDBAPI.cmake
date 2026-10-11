find_program(CPKT_API_CLANG NAMES clang NO_CMAKE_FIND_ROOT_PATH REQUIRED)
file(GLOB contracts CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/tests/contracts/db_*.json")
set(catalogs "${CMAKE_SOURCE_DIR}/cmake/exports/cpkt_postgres.txt" "${CMAKE_SOURCE_DIR}/cmake/exports/cpkt_sqlite.txt")
set(data "${CMAKE_BINARY_DIR}/api")
set(outputs "")
foreach(provider postgres sqlite)
  if(provider STREQUAL "postgres")
    set(native_include "${CPKT_EXTERNAL_ROOT}/postgresql/install/include")
    set(source_include "${CPKT_DEPENDENCY_BUILD_ROOT}/postgresql/src/src/include")
  else()
    set(native_include "${CPKT_EXTERNAL_ROOT}/sqlite/install/include")
    set(source_include "")
  endif()
  foreach(kind native facade)
    set(arguments "")
    set(include "${native_include}")
    if(kind STREQUAL "facade")
      set(arguments --facade)
      set(include "${CMAKE_SOURCE_DIR}/include")
    endif()
    set(output "${data}/${CPKT_TARGET_ID}/${kind}-${provider}.json")
    file(GLOB_RECURSE headers CONFIGURE_DEPENDS LIST_DIRECTORIES FALSE "${include}/*.h")
    add_custom_command(OUTPUT "${output}"
      COMMAND "${CPKT_HOST_PYTHON_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/db_api_inventory.py"
        --target "${CPKT_TARGET_ID}" --provider "${provider}" ${arguments}
        --cc "${CMAKE_C_COMPILER}" --clang "${CPKT_API_CLANG}"
        --include "${include}" --source-include "${source_include}" --output "${output}"
      DEPENDS ${headers} "${CMAKE_SOURCE_DIR}/tools/db_api_inventory.py"
        "${CMAKE_SOURCE_DIR}/tools/generated_output_paths.py" VERBATIM)
    list(APPEND outputs "${output}")
  endforeach()
  set(probe "${data}/${provider}-probe.c")
  set(native_probe "${data}/${provider}-probe-native.c")
  add_custom_command(OUTPUT "${probe}" "${native_probe}"
    COMMAND "${CPKT_HOST_PYTHON_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/generate_db_probes.py"
      --contracts "${CMAKE_SOURCE_DIR}/tests/contracts" --target "${CPKT_TARGET_ID}"
      --provider "${provider}" --output "${probe}"
    DEPENDS ${contracts} "${CMAKE_SOURCE_DIR}/tools/generate_db_probes.py"
      "${CMAKE_SOURCE_DIR}/tools/generated_output_paths.py" VERBATIM)
  add_library(cpkt_db_${provider}_probe OBJECT "${probe}")
  target_include_directories(cpkt_db_${provider}_probe PRIVATE "${CMAKE_SOURCE_DIR}/include" "${native_include}" "${source_include}")
  target_compile_options(cpkt_db_${provider}_probe PRIVATE -std=c89 -pedantic-errors -Werror)
  add_library(cpkt_db_${provider}_native_probe OBJECT "${native_probe}")
  target_include_directories(cpkt_db_${provider}_native_probe PRIVATE "${CMAKE_SOURCE_DIR}/include" "${native_include}")
  if(source_include)
    target_include_directories(cpkt_db_${provider}_native_probe PRIVATE "${source_include}")
  endif()
  target_compile_options(cpkt_db_${provider}_native_probe PRIVATE -std=gnu99 -Werror)
  if(provider STREQUAL "sqlite")
    target_compile_definitions(cpkt_db_sqlite_native_probe PRIVATE SQLITE_ENABLE_SESSION=1 SQLITE_ENABLE_PREUPDATE_HOOK=1 SQLITE_ENABLE_NORMALIZE=1 SQLITE_ENABLE_CARRAY=1)
  endif()
endforeach()
add_custom_target(cpkt_db_api_data ALL DEPENDS ${outputs})
cpkt_group_add_test(NAME db_api_contract COMMAND "${CMAKE_COMMAND}"
  "-DCPKT_SOURCE=${CMAKE_SOURCE_DIR}" "-DCPKT_TARGET=${CPKT_TARGET_ID}" "-DCPKT_DATA=${data}"
  -P "${CMAKE_SOURCE_DIR}/tests/db_api_contract_test.cmake")
set_property(GLOBAL PROPERTY CPKT_TEST_INPUTS_db_api_contract
  "${outputs};${contracts};${catalogs};${CMAKE_SOURCE_DIR}/tests/db_api_contract_test.cmake")
if(CPKT_REGISTER_HOST_TESTS)
  cpkt_group_add_test(NAME db_contract_mutations COMMAND "${CMAKE_COMMAND}"
    "-DCPKT_SOURCE=${CMAKE_SOURCE_DIR}" "-DCPKT_BINARY=${CMAKE_BINARY_DIR}"
    "-DCPKT_TARGET=${CPKT_TARGET_ID}" -P "${CMAKE_SOURCE_DIR}/tests/db_contract_mutation_test.cmake")
  set_property(GLOBAL PROPERTY CPKT_TEST_INPUTS_db_contract_mutations
    "${outputs};${contracts};${catalogs};${CMAKE_SOURCE_DIR}/tests/db_api_contract_test.cmake;${CMAKE_SOURCE_DIR}/tests/db_contract_mutation_test.cmake")
  set_tests_properties(db_contract_mutations PROPERTIES LABELS host-lifecycle TIMEOUT 90)
endif()
