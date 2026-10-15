file(REMOVE "${TOKEN}" "${TOKEN}.tmp")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "OSW_FIXTURE_SCENARIO=${SCENARIO}"
  "${PROGRAM}" --library "${LIBRARY}" --token "${TOKEN}" --timeout 1
  RESULT_VARIABLE actual OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 5)
if(SCENARIO STREQUAL "shutdown-pending")
  if(NOT actual STREQUAL "24" OR NOT errors MATCHES "ERROR STEAM_SHUTDOWN:" OR EXISTS "${TOKEN}")
    message(FATAL_ERROR "Pending ticket was not cancelled on shutdown: ${actual}: ${output} ${errors}")
  endif()
else()
  if(NOT actual STREQUAL "0" OR NOT EXISTS "${TOKEN}" OR
     NOT output MATCHES "Steam requested shutdown; stopping client cleanly")
    message(FATAL_ERROR "Ready client did not stop on shutdown: ${actual}: ${output} ${errors}")
  endif()
endif()
if(NOT output MATCHES "Fixture ticket cancelled[\r\n]+Fixture client shutdown")
  message(FATAL_ERROR "Ticket must be cancelled before client shutdown: ${output}")
endif()
file(REMOVE "${TOKEN}")
