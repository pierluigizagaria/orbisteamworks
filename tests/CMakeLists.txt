add_executable(osw-contract-tests token_tests.cpp)
target_link_libraries(osw-contract-tests PRIVATE osw_spike_common)
add_test(NAME token-contract COMMAND osw-contract-tests)
add_executable(osw-api-tests api_tests.cpp)
target_include_directories(osw-api-tests PRIVATE ../src)
target_link_libraries(osw-api-tests PRIVATE orbisteamworks)
add_test(NAME api-ownership COMMAND osw-api-tests)
add_library(osw-c-header OBJECT api_c_compile.c)
target_include_directories(osw-c-header PRIVATE ../include)
add_executable(osw-platform-tests platform_tests.cpp)
target_include_directories(osw-platform-tests PRIVATE ../src)
target_link_libraries(osw-platform-tests PRIVATE orbisteamworks nlohmann_json::nlohmann_json)
add_test(NAME platform-contracts COMMAND osw-platform-tests)

# These fixtures are our own code, never Steam SDK files. They are only passed
# through --library by offline tests; production has no simulated fallback.
add_library(osw-test-api SHARED ../src/steam/testing/fake_api.cpp)
target_include_directories(osw-test-api PRIVATE ../src)
add_library(osw-test-missing-symbol SHARED missing_symbol.cpp)
function(failure_case name program library scenario code message)
  add_test(NAME ${name} COMMAND ${CMAKE_COMMAND}
    -DPROGRAM=$<TARGET_FILE:${program}> -DLIBRARY=${library}
    -DSCENARIO=${scenario} -DCODE=${code} -DMESSAGE=${message}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/failure.cmake)
endfunction()
failure_case(missing-library osw-auth-client "${CMAKE_CURRENT_BINARY_DIR}/absent-library" none 10 LIBRARY_MISSING)
failure_case(missing-symbol osw-auth-client "$<TARGET_FILE:osw-test-missing-symbol>" none 11 SYMBOL_MISSING)
failure_case(client-unavailable osw-auth-client "$<TARGET_FILE:osw-test-api>" no-client 12 CLIENT_UNAVAILABLE)
failure_case(client-signed-out osw-auth-client "$<TARGET_FILE:osw-test-api>" signed-out 13 SIGNED_OUT)
failure_case(server-init-failure osw-auth-server "$<TARGET_FILE:osw-test-api>" init-failure 14 SERVER_INIT_FAILED)
failure_case(server-logon-failure osw-auth-server "$<TARGET_FILE:osw-test-api>" logon-failure 15 SERVER_LOGON_FAILED)
failure_case(server-logon-timeout osw-auth-server "$<TARGET_FILE:osw-test-api>" timeout 16 TIMEOUT)
failure_case(ticket-timeout osw-auth-client "$<TARGET_FILE:osw-test-api>" timeout 16 TIMEOUT)
failure_case(ticket-rejected osw-auth-client "$<TARGET_FILE:osw-test-api>" rejected 17 TICKET_FAILED)
failure_case(callback-abi osw-auth-client "$<TARGET_FILE:osw-test-api>" bad-callback 22 CALLBACK_ABI_MISMATCH)
foreach(scenario shutdown-pending shutdown-ready)
  add_test(NAME ${scenario} COMMAND ${CMAKE_COMMAND}
    -DPROGRAM=$<TARGET_FILE:osw-auth-client> -DLIBRARY=$<TARGET_FILE:osw-test-api>
    -DSCENARIO=${scenario} -DTOKEN=${CMAKE_CURRENT_BINARY_DIR}/${scenario}.txt
    -P ${CMAKE_CURRENT_SOURCE_DIR}/shutdown.cmake)
endforeach()

add_executable(osw-runtime-error-tests runtime_errors.cpp)
target_include_directories(osw-runtime-error-tests PRIVATE ../src)
target_link_libraries(osw-runtime-error-tests PRIVATE orbisteamworks)
function(runtime_error_case name library scenario operation reason)
  add_test(NAME runtime-${name} COMMAND ${CMAKE_COMMAND} -E env
    "OSW_FIXTURE_SCENARIO=${scenario}" $<TARGET_FILE:osw-runtime-error-tests>
    "${library}" ${operation} ${reason})
endfunction()
runtime_error_case(missing-library "${CMAKE_CURRENT_BINARY_DIR}/absent-library" none startup 10)
runtime_error_case(missing-symbol "$<TARGET_FILE:osw-test-missing-symbol>" none startup 11)
runtime_error_case(client-unavailable "$<TARGET_FILE:osw-test-api>" no-client startup 12)
runtime_error_case(signed-out "$<TARGET_FILE:osw-test-api>" signed-out startup 13)
runtime_error_case(social-unavailable "$<TARGET_FILE:osw-test-api>" missing-social startup 11)
runtime_error_case(presence-failure "$<TARGET_FILE:osw-test-api>" presence-failure startup 20)
runtime_error_case(friend-count "$<TARGET_FILE:osw-test-api>" bad-friend-count startup 22)
runtime_error_case(member-count "$<TARGET_FILE:osw-test-api>" bad-member-count members 22)
runtime_error_case(ticket-buffer "$<TARGET_FILE:osw-test-api>" bad-ticket ticket 17)
runtime_error_case(ticket-zero "$<TARGET_FILE:osw-test-api>" zero-ticket ticket 17)
runtime_error_case(callback-abi "$<TARGET_FILE:osw-test-api>" bad-callback callback 22)
if(WIN32)
  runtime_error_case(invalid-path "$<TARGET_FILE:osw-test-api>" none invalid-path 20)
endif()

if(OSW_BUILD_SHARED)
  # A C host that loads the plugin at runtime; it must not link the library.
  add_executable(osw-shared-load-test shared_load.c)
  target_include_directories(osw-shared-load-test PRIVATE ../include)
  target_link_libraries(osw-shared-load-test PRIVATE ${CMAKE_DL_LIBS})
  add_dependencies(osw-shared-load-test orbisteamworks_shared)
  add_test(NAME shared-load COMMAND osw-shared-load-test
    $<TARGET_FILE:orbisteamworks_shared> "${CMAKE_CURRENT_BINARY_DIR}/absent-library")
endif()
