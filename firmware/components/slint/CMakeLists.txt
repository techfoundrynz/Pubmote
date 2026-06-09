# Copyright © SixtyFPS GmbH <info@slint.dev>
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-Slint-Royalty-free-2.0 OR LicenseRef-Slint-Software-3.0

idf_component_register(
    SRCS "src/slint-esp.cpp"
    INCLUDE_DIRS "include"
    REQUIRES "esp_lcd" "esp_lcd_touch" "esp_timer")

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
    # Define where the compiler and prebuilt files should go for SCons
    set(download_directory "${CMAKE_BINARY_DIR}/slint-prebuilt")
    if (CMAKE_HOST_WIN32)
        set(compiler_name "slint-compiler.exe")
    else()
        set(compiler_name "slint-compiler")
    endif()
    
    set(slint_compiler_path "${download_directory}/${compiler_name}")
    file(MAKE_DIRECTORY "${download_directory}")

    set(SLINT_FEATURE_RENDERER_SOFTWARE ON CACHE BOOL "Enable software renderer" FORCE)
    set(SLINT_FEATURE_SOFTWARE_RENDERER_PATH ON CACHE BOOL "Enable path rendering in software renderer" FORCE)
    set(SLINT_FEATURE_FREESTANDING ON CACHE BOOL "Enable freestanding mode" FORCE)
    
    include(FetchContent)
    FetchContent_Declare(
        Slint
        GIT_REPOSITORY "https://github.com/techfoundrynz/slint.git"
        GIT_TAG "feat/mcu-arcs"
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
            rustup run esp cargo build --release --package slint-cpp --target xtensa-esp32s3-none-elf "-Zbuild-std=core,alloc" --no-default-features --features renderer-software,software-renderer-path,freestanding
        WORKING_DIRECTORY "${slint_SOURCE_DIR}"
        RESULT_VARIABLE cargo_result
    )
    if (NOT cargo_result EQUAL 0)
        message(FATAL_ERROR "Cargo build for Slint C++ failed!")
    endif()

    # Run Cargo build for the host compiler
    message(STATUS "Running Cargo build for Slint compiler...")
    execute_process(
        COMMAND cargo build --release --bin slint-compiler
        WORKING_DIRECTORY "${slint_SOURCE_DIR}"
        RESULT_VARIABLE compiler_cargo_result
    )
    if (NOT compiler_cargo_result EQUAL 0)
        message(FATAL_ERROR "Cargo build for Slint compiler failed!")
    endif()

    # Copy the built compiler to slint_compiler_path
    if (CMAKE_HOST_WIN32)
        set(compiled_compiler_name "slint-compiler.exe")
    else()
        set(compiled_compiler_name "slint-compiler")
    endif()
    file(COPY "${slint_SOURCE_DIR}/target/release/${compiled_compiler_name}"
         DESTINATION "${download_directory}")
         
    # Copy the compiled library to where CMake/ldgen expects it
    file(COPY "${slint_SOURCE_DIR}/target/xtensa-esp32s3-none-elf/release/libslint_cpp.a"
         DESTINATION "${CMAKE_BINARY_DIR}/_deps/slint-build")

    # Copy the compiled library to where prebuild_hook.py / SCons links it
    file(COPY "${slint_SOURCE_DIR}/target/xtensa-esp32s3-none-elf/release/libslint_cpp.a"
         DESTINATION "${CMAKE_BINARY_DIR}/slint-prebuilt/Slint-cpp-1.16.1-xtensa-esp32s3-none-elf/lib")

    # Copy the main Slint headers to the prebuilt include directory
    file(COPY "${slint_SOURCE_DIR}/api/cpp/include/"
         DESTINATION "${CMAKE_BINARY_DIR}/slint-prebuilt/Slint-cpp-1.16.1-xtensa-esp32s3-none-elf/include")

    # Copy the generated slint_internal.h header to the prebuilt include directory
    file(GLOB_RECURSE generated_internal_h "${slint_SOURCE_DIR}/target/xtensa-esp32s3-none-elf/release/build/slint-cpp-*/out/generated_include/private/slint_internal.h")
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


# Trigger rebuild for trig optimization

# Trigger rebuild for early return test
# Trigger rebuild for fixed-point math

# Trigger CMake reconfigure
