# Copyright © SixtyFPS GmbH <info@slint.dev>
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-Slint-Royalty-free-2.0 OR LicenseRef-Slint-Software-3.0

idf_component_register(
    SRCS "src/slint-esp.cpp"
    INCLUDE_DIRS "include"
    REQUIRES "esp_lcd" "esp_lcd_touch")

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

if (CONFIG_IDF_TARGET_ARCH_XTENSA)
    set(rust_target "xtensa-${IDF_TARGET}-none-elf")
elseif(CONFIG_IDF_TARGET_ARCH_RISCV)
    if (CONFIG_IDF_TARGET_ESP32C6 OR CONFIG_IDF_TARGET_ESP32C5 OR CONFIG_IDF_TARGET_ESP32H2)
        set(rust_target "riscv32imac-esp-espidf")
    elseif (CONFIG_IDF_TARGET_ESP32P4)
        set(rust_target "riscv32imafc-esp-espidf")
    else ()
        set(rust_target "riscv32imc-esp-espidf")
    endif()
else()
    message(FATAL_ERROR "Architecture currently not supported")
endif()

set(SLINT_FEATURE_FREESTANDING ON)
set(SLINT_FEATURE_RENDERER_SOFTWARE ON)
set(SLINT_LIBRARY_CARGO_FLAGS "-Zbuild-std=core,alloc")
set(DEFAULT_SLINT_EMBED_RESOURCES "embed-for-software-renderer" CACHE STRING "")
set(CMAKE_BUILD_TYPE Release)
set(BUILD_SHARED_LIBS OFF)
set(Rust_CARGO_TARGET ${rust_target})

if (SLINT_ESP_LOCAL_EXAMPLE)
    add_subdirectory(../.. slint_build)
else()
    list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
    # Download the host compiler manually if it is not present, avoiding find_package target collision.
    set(download_directory "${CMAKE_BINARY_DIR}/slint-prebuilt")
    if (CMAKE_HOST_WIN32)
        set(compiler_name "slint-compiler.exe")
        set(platform_suffix "windows-${CMAKE_HOST_SYSTEM_PROCESSOR}")
    else()
        set(compiler_name "slint-compiler")
        set(platform_suffix "${CMAKE_HOST_SYSTEM_NAME}-${CMAKE_HOST_SYSTEM_PROCESSOR}")
    endif()
    
    set(slint_compiler_path "${download_directory}/${compiler_name}")

    if (NOT EXISTS "${slint_compiler_path}")
        set(SLINT_GITHUB_RELEASE "v1.16.1")
        set(prebuilt_archive_filename "slint-compiler-${platform_suffix}.tar.gz")
        set(download_url "https://github.com/slint-ui/slint/releases/download/${SLINT_GITHUB_RELEASE}/${prebuilt_archive_filename}")

        file(MAKE_DIRECTORY "${download_directory}")
        message(STATUS "Downloading pre-built Slint compiler binary from ${download_url}")
        file(DOWNLOAD "${download_url}" "${download_directory}/${prebuilt_archive_filename}" SHOW_PROGRESS)
        file(ARCHIVE_EXTRACT INPUT "${download_directory}/${prebuilt_archive_filename}" DESTINATION "${download_directory}")
        file(REMOVE "${download_directory}/${prebuilt_archive_filename}")
    endif()

    set(SLINT_FEATURE_RENDERER_SOFTWARE ON CACHE BOOL "Enable software renderer" FORCE)
    set(SLINT_FEATURE_SOFTWARE_RENDERER_PATH ON CACHE BOOL "Enable path rendering in software renderer" FORCE)
    set(SLINT_FEATURE_FREESTANDING ON CACHE BOOL "Enable freestanding mode" FORCE)
    
    include(FetchContent)
    FetchContent_Declare(
        Slint
        GIT_REPOSITORY https://github.com/slint-ui/slint
        GIT_TAG v1.16.1
        SOURCE_SUBDIR api/cpp
    )
    FetchContent_MakeAvailable(Slint)

    # Run Cargo build at configuration time to ensure the library file is created before SCons ldgen/compilation.
    message(STATUS "Running Cargo build for Slint C++...")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env 
            "CARGO_PROFILE_RELEASE_LTO=fat"
            "CARGO_PROFILE_RELEASE_CODEGEN_UNITS=1"
            "CARGO_PROFILE_RELEASE_OPT_LEVEL=3"
            cargo +esp build --release --package slint-cpp --target xtensa-esp32s3-none-elf "-Zbuild-std=core,alloc" --no-default-features --features renderer-software,software-renderer-path,freestanding
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/_deps/slint-src"
        RESULT_VARIABLE cargo_result
    )
    if (NOT cargo_result EQUAL 0)
        message(FATAL_ERROR "Cargo build for Slint C++ failed!")
    endif()

    # Copy the compiled library to where CMake/ldgen expects it
    file(COPY "${CMAKE_BINARY_DIR}/_deps/slint-src/target/xtensa-esp32s3-none-elf/release/libslint_cpp.a"
         DESTINATION "${CMAKE_BINARY_DIR}/_deps/slint-build")

    # Copy the compiled library to where prebuild_hook.py / SCons links it
    file(COPY "${CMAKE_BINARY_DIR}/_deps/slint-src/target/xtensa-esp32s3-none-elf/release/libslint_cpp.a"
         DESTINATION "${CMAKE_BINARY_DIR}/slint-prebuilt/Slint-cpp-1.16.1-xtensa-esp32s3-none-elf/lib")

    # Copy the generated slint_internal.h header to the prebuilt include directory
    file(GLOB_RECURSE generated_internal_h "${CMAKE_BINARY_DIR}/_deps/slint-src/target/*/slint_internal.h")
    if (generated_internal_h)
        list(GET generated_internal_h 0 single_internal_h)
        get_filename_component(internal_h_dir "${single_internal_h}" DIRECTORY)
        file(COPY "${internal_h_dir}"
             DESTINATION "${CMAKE_BINARY_DIR}/slint-prebuilt/Slint-cpp-1.16.1-xtensa-esp32s3-none-elf/include")
        message(STATUS "Copied generated slint_internal.h from ${internal_h_dir}")
    else()
        message(WARNING "Could not find generated slint_internal.h!")
    endif()
endif()

target_link_libraries(${COMPONENT_LIB} PUBLIC Slint::Slint)
target_linker_script(${COMPONENT_LIB} INTERFACE "esp-println.x")
