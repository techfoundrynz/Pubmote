cmake_minimum_required(VERSION 3.16)
project(firmware_host_tests C CXX)

set(IDF_PATH "$ENV{IDF_PATH}" CACHE PATH "ESP-IDF source directory")
set(CJSON_DIR "${CMAKE_SOURCE_DIR}/../managed_components/espressif__cjson/cJSON" CACHE PATH "cJSON source directory")
if(NOT EXISTS "${CJSON_DIR}/cJSON.c" AND EXISTS "${IDF_PATH}/components/json/cJSON/cJSON.c")
  set(CJSON_DIR "${IDF_PATH}/components/json/cJSON")
endif()
if(NOT EXISTS "${CJSON_DIR}/cJSON.c" OR NOT EXISTS "${IDF_PATH}/components/console/split_argv.c")
  message(FATAL_ERROR "Set IDF_PATH to ESP-IDF and CJSON_DIR to cJSON (or build firmware first to fetch it)")
endif()

# Stage the sources beside empty shadow/ headers; quoted includes search their own directory first.
set(STAGED "${CMAKE_BINARY_DIR}/staged")
set(FIRMWARE_REMOTE "${CMAKE_SOURCE_DIR}/../firmware/src/remote")
foreach(src input_settings.c settings_api.c settings_console.c sleep_timer.c)
  configure_file("${FIRMWARE_REMOTE}/${src}" "${STAGED}/remote/${src}" COPYONLY)
endforeach()
foreach(stub config.h remote/settings.h remote/settings_store.h remote/adc.h remote/remoteinputs.h remote/display.h remote/led.h ui/device_preferences.h)
  configure_file("${CMAKE_SOURCE_DIR}/shadow/${stub}" "${STAGED}/${stub}" COPYONLY)
endforeach()

add_executable(settings_console_test
  settings_console_test.c
  "${CJSON_DIR}/cJSON.c"
  "${IDF_PATH}/components/console/split_argv.c"
)
target_include_directories(settings_console_test PRIVATE "${STAGED}" "${FIRMWARE_REMOTE}" "${CMAKE_SOURCE_DIR}/../firmware/modules/config/include" "${CJSON_DIR}" "${CMAKE_SOURCE_DIR}/../firmware/modules/settings/include")
set_target_properties(settings_console_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
if(MSVC)
  target_compile_definitions(settings_console_test PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_options(settings_console_test PRIVATE /UNDEBUG)
else()
  target_compile_options(settings_console_test PRIVATE -UNDEBUG -Wall -Wextra)
  target_link_libraries(settings_console_test PRIVATE m)
endif()

enable_testing()
add_test(NAME settings_console COMMAND settings_console_test)

add_executable(sleep_timer_test sleep_timer_test.c)
target_include_directories(sleep_timer_test PRIVATE "${STAGED}" "${FIRMWARE_REMOTE}" stubs)
set_target_properties(sleep_timer_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
if(MSVC)
  target_compile_options(sleep_timer_test PRIVATE /UNDEBUG)
else()
  target_compile_options(sleep_timer_test PRIVATE -UNDEBUG -Wall -Wextra)
endif()
add_test(NAME sleep_timer COMMAND sleep_timer_test)

find_package(Threads REQUIRED)
add_executable(state_test state_test.cpp
  ../firmware/modules/telemetry/src/stats.c
  ../firmware/modules/settings/src/settings_state.c)
target_include_directories(state_test PRIVATE state_stubs
  ../firmware/modules/settings/include ../firmware/modules/telemetry/include ../firmware/modules/protocol/include ../firmware/modules/config/include
  ../firmware/modules/support/include)
target_link_libraries(state_test PRIVATE Threads::Threads)
set_target_properties(state_test PROPERTIES C_STANDARD 11 CXX_STANDARD 17)
if(MSVC)
  target_compile_options(state_test PRIVATE /UNDEBUG)
else()
  target_compile_options(state_test PRIVATE -UNDEBUG -Wall -Wextra)
  target_link_libraries(state_test PRIVATE m)
endif()
add_test(NAME state COMMAND state_test)
set_tests_properties(state PROPERTIES TIMEOUT 30)

# Compile the real packet handler with only lifecycle/time/device I/O stubbed.
configure_file("${FIRMWARE_REMOTE}/commands.c" "${STAGED}/commands/commands.c" COPYONLY)
foreach(stub commands.h connection.h powermanagement.h settings.h time.h)
  file(WRITE "${STAGED}/commands/${stub}" "#pragma once\n")
endforeach()
add_executable(commands_test commands_test.c
  ../firmware/modules/telemetry/src/stats.c
  ../firmware/modules/settings/src/settings_state.c
  ../firmware/modules/protocol/src/protocol.c)
target_include_directories(commands_test PRIVATE "${STAGED}" state_stubs
  ../firmware/modules/settings/include ../firmware/modules/telemetry/include ../firmware/modules/protocol/include ../firmware/modules/config/include
  ../firmware/modules/support/include)
target_link_libraries(commands_test PRIVATE Threads::Threads)
set_target_properties(commands_test PROPERTIES C_STANDARD 11)
if(MSVC)
  target_compile_options(commands_test PRIVATE /UNDEBUG)
else()
  target_compile_options(commands_test PRIVATE -UNDEBUG -Wall -Wextra)
  target_link_libraries(commands_test PRIVATE m)
endif()
add_test(NAME commands COMMAND commands_test)

add_executable(protocol_test protocol_test.c ../firmware/modules/protocol/src/protocol.c)
target_include_directories(protocol_test PRIVATE ../firmware/modules/protocol/include ../firmware/modules/config/include)
set_target_properties(protocol_test PROPERTIES C_STANDARD 11)
if(MSVC)
  target_compile_options(protocol_test PRIVATE /UNDEBUG)
else()
  target_compile_options(protocol_test PRIVATE -UNDEBUG -Wall -Wextra)
  target_link_libraries(protocol_test PRIVATE m)
endif()
add_test(NAME protocol COMMAND protocol_test)

configure_file("${FIRMWARE_REMOTE}/pairing.c" "${STAGED}/pairing/pairing.c" COPYONLY)
foreach(stub pairing.h config.h connection.h esp_log.h remote/receiver.h settings.h)
  file(WRITE "${STAGED}/pairing/${stub}" "#pragma once\n")
endforeach()
add_executable(pairing_test pairing_test.c
  ../firmware/modules/settings/src/settings_state.c
  ../firmware/modules/protocol/src/protocol.c)
target_include_directories(pairing_test PRIVATE "${STAGED}" state_stubs
  ../firmware/modules/settings/include ../firmware/modules/config/include
  ../firmware/modules/protocol/include ../firmware/src)
target_link_libraries(pairing_test PRIVATE Threads::Threads)
set_target_properties(pairing_test PROPERTIES C_STANDARD 11)
if(MSVC)
  target_compile_options(pairing_test PRIVATE /UNDEBUG)
else()
  target_compile_options(pairing_test PRIVATE -UNDEBUG -Wall -Wextra)
  target_link_libraries(pairing_test PRIVATE m)
endif()
add_test(NAME pairing COMMAND pairing_test)
