file(GLOB_RECURSE app_sources "${CMAKE_CURRENT_SOURCE_DIR}/*.c" "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

set(SLINT_CPP_FILES 8 CACHE STRING "Number of generated Slint C++ translation units")
math(EXPR slint_last_cpp "${SLINT_CPP_FILES} - 1")
set(slint_sources "")
foreach(index RANGE 0 ${slint_last_cpp})
    list(APPEND slint_sources "${CMAKE_BINARY_DIR}/slint_generated/app-window-${index}.cpp")
endforeach()

idf_component_register(SRCS ${app_sources} ${slint_sources} INCLUDE_DIRS "${CMAKE_BINARY_DIR}")
# Retain line/function information without enormous generated variable debug
# tables. Handwritten source files keep their normal debug settings.
option(SLINT_FULL_DEBUG "Emit full debug information for generated Slint code" OFF)
if(NOT SLINT_FULL_DEBUG)
    set_source_files_properties(${slint_sources} PROPERTIES COMPILE_OPTIONS "-g1")
endif()
