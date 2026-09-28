cmake_minimum_required(VERSION 3.16)
project(firmware_host_tests C)

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
foreach(stub config.h remote/settings.h remote/remoteinputs.h)
  configure_file("${CMAKE_SOURCE_DIR}/shadow/${stub}" "${STAGED}/${stub}" COPYONLY)
endforeach()

add_executable(settings_console_test
  settings_console_test.c
  "${CJSON_DIR}/cJSON.c"
  "${IDF_PATH}/components/console/split_argv.c"
)
target_include_directories(settings_console_test PRIVATE "${STAGED}" "${FIRMWARE_REMOTE}" "${CJSON_DIR}")
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
