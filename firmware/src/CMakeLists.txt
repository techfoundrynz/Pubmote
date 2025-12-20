# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/firmware/src/*.*)

# Include the sources from the sibling lvgl_editor directory
include(${CMAKE_CURRENT_LIST_DIR}/../lvgl_editor/file_list_gen.cmake)

# Register the component with both src sources and lvgl_editor sources
idf_component_register(
    SRCS 
        ${app_sources}
        ${PROJECT_SOURCES}
    INCLUDE_DIRS 
        "."
        "../lvgl_editor"
    REQUIRES 
        lvgl
)
