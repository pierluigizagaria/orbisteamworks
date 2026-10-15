execute_process(COMMAND "${CMAKE_COMMAND}" -E env "OSW_FIXTURE_SCENARIO=${SCENARIO}"
  "${PROGRAM}" --library "${LIBRARY}" --timeout 0
  RESULT_VARIABLE actual OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 5)
if(NOT actual STREQUAL CODE OR NOT errors MATCHES "ERROR ${MESSAGE}:")
  message(FATAL_ERROR "Expected ${CODE}/${MESSAGE}, got ${actual}: ${output} ${errors}")
endif()
