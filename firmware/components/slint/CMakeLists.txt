# Copyright © SixtyFPS GmbH <info@slint.dev>
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-Slint-Royalty-free-2.0 OR LicenseRef-Slint-Software-3.0

idf_component_register(
    SRCS "src/slint-esp.cpp"
    INCLUDE_DIRS "include"
    REQUIRES "esp_lcd" "esp_lcd_touch" "esp_timer")

# Slint is always consumed as a prebuilt release - this project never builds it from
# source, so no Rust toolchain is required. The release pins the library, the generated
# cbindgen headers and the .slint compiler to one version; those three must agree, and
# assembling them separately is how they drift apart.
#
# Which release to use is decided in platformio.ini and nowhere else. Deliberately no
# defaults here: a default would be a second place to bump on every release, and the two
# would silently disagree the first time only one was updated.
#
# TAG is our release name; VERSION is Slint's crate version, which cpack bakes into the
# archive filename. They are not the same string and do not move together - a rebuild of
# the same Slint version gets a new tag. To use a different publisher (e.g. Slint's own
# releases), point REPO/TAG at it, and if it names assets differently override
# SLINT_PREBUILT_ARCHIVE / SLINT_PREBUILT_COMPILER_ASSET rather than editing this file.
set(SLINT_PREBUILT_REPO "" CACHE STRING "GitHub owner/repo publishing the package")
set(SLINT_PREBUILT_TAG "" CACHE STRING "Release tag to download")
set(SLINT_PREBUILT_VERSION "" CACHE STRING "Slint version in that release; part of the archive name")
set(SLINT_PREBUILT_ARCHIVE "" CACHE STRING "Override the archive filename")
set(SLINT_PREBUILT_COMPILER_ASSET "" CACHE STRING "Override the slint-compiler asset name")

foreach (required_setting SLINT_PREBUILT_REPO SLINT_PREBUILT_TAG SLINT_PREBUILT_VERSION)
    if (NOT ${required_setting})
        message(FATAL_ERROR
            "${required_setting} is not set. The Slint release is configured in "
            "platformio.ini under board_build.cmake_extra_args.")
    endif()
endforeach()

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

if (CMAKE_HOST_WIN32)
    set(compiler_name "slint-compiler.exe")
    set(default_compiler_asset "slint-compiler-windows-x86_64.exe")
elseif (CMAKE_HOST_APPLE)
    set(compiler_name "slint-compiler")
    set(default_compiler_asset "slint-compiler-macos-aarch64")
else()
    set(compiler_name "slint-compiler")
    set(default_compiler_asset "slint-compiler-linux-x86_64")
endif()

if (SLINT_PREBUILT_ARCHIVE)
    set(archive_name "${SLINT_PREBUILT_ARCHIVE}")
else()
    set(archive_name "Slint-cpp-${SLINT_PREBUILT_VERSION}-${rust_target}.tar.gz")
endif()
if (SLINT_PREBUILT_COMPILER_ASSET)
    set(compiler_asset "${SLINT_PREBUILT_COMPILER_ASSET}")
else()
    set(compiler_asset "${default_compiler_asset}")
endif()

set(download_directory "${CMAKE_BINARY_DIR}/slint-prebuilt")
set(staged_dir "${download_directory}/current")
set(base_url "https://github.com/${SLINT_PREBUILT_REPO}/releases/download/${SLINT_PREBUILT_TAG}")
file(MAKE_DIRECTORY "${download_directory}")

# prebuild_hook.py is a SCons `pre:` script, so it runs BEFORE this CMakeLists does - it
# cannot discover a version-dependent directory name, it can only agree on a fixed one.
# That is why everything is staged into `current` rather than used where it unpacks.
set(slint_compiler_path "${download_directory}/${compiler_name}")

# Restage from scratch whenever the release changes, so switching versions cannot leave
# headers from the old one behind. That mismatch is invisible until it surfaces as
# missing symbols late in the C++ build.
set(staged_id "${SLINT_PREBUILT_REPO}@${SLINT_PREBUILT_TAG}:${archive_name}")
set(staged_stamp "${staged_dir}/.slint-source")
if (EXISTS "${staged_stamp}")
    file(READ "${staged_stamp}" staged_id_previous)
    if (NOT staged_id_previous STREQUAL staged_id)
        message(STATUS "Slint: release changed (${staged_id_previous} -> ${staged_id}), restaging")
        file(REMOVE_RECURSE "${staged_dir}")
        file(REMOVE "${slint_compiler_path}")
    endif()
endif()

if (NOT EXISTS "${staged_dir}/lib/libslint_cpp.a")
    set(extract_dir "${download_directory}/extracted")
    set(archive "${download_directory}/${archive_name}")

    message(STATUS "Slint: downloading ${archive_name} (${SLINT_PREBUILT_REPO} ${SLINT_PREBUILT_TAG})...")
    file(DOWNLOAD "${base_url}/${archive_name}" "${archive}"
         STATUS download_status TLS_VERIFY ON SHOW_PROGRESS)
    list(GET download_status 0 download_code)
    if (NOT download_code EQUAL 0)
        list(GET download_status 1 download_error)
        file(REMOVE "${archive}")
        message(FATAL_ERROR
            "Failed to download ${archive_name} from ${base_url}: ${download_error}\n"
            "Check SLINT_PREBUILT_REPO / SLINT_PREBUILT_TAG / SLINT_PREBUILT_VERSION.")
    endif()

    file(REMOVE_RECURSE "${extract_dir}")
    file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${extract_dir}")
    file(REMOVE "${archive}")

    # cpack decides the directory name inside the archive, so find it rather than assume
    # it matches the asset name.
    file(GLOB extracted_roots LIST_DIRECTORIES true "${extract_dir}/*")
    set(package_dir "")
    foreach (candidate ${extracted_roots})
        if (EXISTS "${candidate}/lib/libslint_cpp.a")
            set(package_dir "${candidate}")
        endif()
    endforeach()
    if (NOT package_dir)
        message(FATAL_ERROR "Slint: ${archive_name} contains no lib/libslint_cpp.a")
    endif()

    file(COPY "${package_dir}/lib" DESTINATION "${staged_dir}")
    # The package installs headers under include/slint (PUBLIC_HEADER DESTINATION
    # include/slint in api/cpp/CMakeLists.txt). Flatten so slint.h sits at the root of
    # the include path, which is how the sources include it.
    if (EXISTS "${package_dir}/include/slint")
        file(COPY "${package_dir}/include/slint/" DESTINATION "${staged_dir}/include")
    else()
        file(COPY "${package_dir}/include/" DESTINATION "${staged_dir}/include")
    endif()
    file(REMOVE_RECURSE "${extract_dir}")
endif()

# The .slint compiler must come from the same release: it carries the glyph-embedding
# limit that keeps large font sizes inside the flash budget. A stock compiler would
# silently embed the full charset.
if (NOT EXISTS "${slint_compiler_path}")
    message(STATUS "Slint: downloading ${compiler_asset}...")
    file(DOWNLOAD "${base_url}/${compiler_asset}" "${slint_compiler_path}"
         STATUS compiler_status TLS_VERIFY ON SHOW_PROGRESS)
    list(GET compiler_status 0 compiler_code)
    if (NOT compiler_code EQUAL 0)
        list(GET compiler_status 1 compiler_error)
        file(REMOVE "${slint_compiler_path}")
        message(FATAL_ERROR "Failed to download ${compiler_asset} from ${base_url}: ${compiler_error}")
    endif()
    if (NOT CMAKE_HOST_WIN32)
        file(CHMOD "${slint_compiler_path}" PERMISSIONS
             OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE)
    endif()
endif()

file(WRITE "${staged_stamp}" "${staged_id}")

add_library(Slint::Slint STATIC IMPORTED GLOBAL)
set_target_properties(Slint::Slint PROPERTIES
    IMPORTED_LOCATION "${staged_dir}/lib/libslint_cpp.a"
    INTERFACE_INCLUDE_DIRECTORIES "${staged_dir}/include")

target_link_libraries(${COMPONENT_LIB} PUBLIC Slint::Slint)
target_linker_script(${COMPONENT_LIB} INTERFACE "esp-println.x")
