Import("env")
from datetime import datetime
import hashlib

major_version = 0
minor_version = 8
patch_version = 9

def generate_build_id():
    # Get current timestamp
    timestamp = datetime.now().strftime("%Y%m%d")

    env_name = env["PIOENV"]

    # Create version string
    version = f"{major_version}.{minor_version}.{patch_version}"
    
    # Combine version and timestamp for hashing
    content_to_hash = f"{env_name}_{version}_{timestamp}"

    # Create a hash of timestamp for shorter unique ID
    hash_object = hashlib.md5(content_to_hash.encode())
    build_hash = hash_object.hexdigest()[:8]
    
    # Combine timestamp and hash
    build_id = f"{build_hash}"
    return build_id

# Add build ID to environment
hw_type = env["PIOENV"]
build_id = generate_build_id()

env.Append(BUILD_FLAGS=[f'-D HW_TYPE=\\"{hw_type}\\"'])
env.Append(BUILD_FLAGS=[f'-D BUILD_ID=\\"{build_id}\\"'])
env.Append(BUILD_FLAGS=[f'-D VERSION_MAJOR={major_version}'])
env.Append(BUILD_FLAGS=[f'-D VERSION_MINOR={minor_version}'])
env.Append(BUILD_FLAGS=[f'-D VERSION_PATCH={patch_version}'])
# Add slint_cpp prebuilt library to linker flags and include paths for main firmware only (not bootloader)
if "bootloader" not in env.subst("$BUILD_DIR"):
    import os
    import subprocess
    
    env_name = env["PIOENV"]
    slint_prebuilt_bin_dir = os.path.join(".pio", "build", env_name, "slint-prebuilt")
    compiler_name = "slint-compiler.exe" if os.name == 'nt' else "slint-compiler"
    slint_compiler_path = os.path.abspath(os.path.join(slint_prebuilt_bin_dir, compiler_name))
    
    # 1. Check/create placeholder empty files at SCons configuration time so dependency scanning doesn't fail
    h_path = "firmware/src/generated/app-window.h"
    cpp_path = "firmware/src/generated/app-window.cpp"
    if not os.path.exists(h_path) or not os.path.exists(cpp_path):
        os.makedirs(os.path.dirname(h_path), exist_ok=True)
        print("[Slint Compiler] Creating initial placeholder C++ files...")
        with open(h_path, "w") as f:
            f.write("// Slint placeholder\n")
        with open(cpp_path, "w") as f:
            f.write("// Slint placeholder\n")
        import time
        past_time = time.time() - 3600
        os.utime(h_path, (past_time, past_time))
        os.utime(cpp_path, (past_time, past_time))

    # 2. Define the compiler execution function (runs in compilation phase, after CMake runs)
    def compile_slint_files(target, source, env):
        if os.path.exists(slint_compiler_path):
            print(f"[Slint Compiler] Compiling app-window.slint using: {slint_compiler_path}")
            
            # Dynamically calculate embedded font sizes based on active platform build flags
            build_flags = env.get("BUILD_FLAGS", [])
            
            def get_macro_value(macro):
                for flag in build_flags:
                    if isinstance(flag, str):
                        if flag.startswith(f"-D {macro}=") or flag.startswith(f"-D{macro}="):
                            return flag.split("=", 1)[1].strip()
                        elif flag == f"-D {macro}" or flag == f"-D{macro}":
                            return "1"
                return None

            lv_hor_res = 240
            hor_res_str = get_macro_value("HOR_RES")
            ver_res_str = get_macro_value("VER_RES")
            if hor_res_str or ver_res_str:
                try:
                    h_val = int(hor_res_str) if hor_res_str else 0
                    v_val = int(ver_res_str) if ver_res_str else 0
                    if h_val > 0 and v_val > 0:
                        lv_hor_res = min(h_val, v_val)
                    else:
                        lv_hor_res = max(h_val, v_val)
                except ValueError:
                    pass

            scale_font = None
            scale_font_str = get_macro_value("SCALE_FONT")
            if scale_font_str:
                try:
                    scale_font = float(scale_font_str)
                except ValueError:
                    pass

            if scale_font is None:
                scale_font = lv_hor_res / 240.0

            limit_threshold = 125
            limit_threshold_str = get_macro_value("LIMIT_GLYPHS_THRESHOLD")
            if limit_threshold_str:
                try:
                    limit_threshold = int(limit_threshold_str)
                except ValueError:
                    pass

            limit_chars = get_macro_value("LIMIT_GLYPHS_CHARS")
            if limit_chars:
                limit_chars = limit_chars.strip()
                if limit_chars.startswith('"') and limit_chars.endswith('"'):
                    limit_chars = limit_chars[1:-1]
                elif limit_chars.startswith("'") and limit_chars.endswith("'"):
                    limit_chars = limit_chars[1:-1]
                limit_chars = limit_chars.replace('\\"', '"').replace("\\'", "'")
            else:
                limit_chars = "0123456789., "

            base_sizes = [10, 11, 12, 14, 28, 48, 64]
            # int(x + 0.5) matches Slint's Math.round (Python round() is banker's rounding)
            scaled_sizes = sorted(list(set(int(sz * scale_font + 0.5) for sz in base_sizes)))
            # The compiler embeds the full ~85-glyph charset per size by default, which
            # would overflow flash for large sizes. We have patched the Slint compiler to only
            # embed configured characters for font sizes over the configured threshold,
            # making them highly space-efficient. Sizes larger than the 250 cap fall back to runtime scaling.
            scaled_sizes = [sz for sz in scaled_sizes if sz <= 250]
            font_sizes_str = ",".join(str(sz) for sz in scaled_sizes)

            print(f"[Slint Compiler] Detected target parameters: LV_HOR_RES={lv_hor_res}, SCALE_FONT={scale_font}, LIMIT_GLYPHS_THRESHOLD={limit_threshold}, LIMIT_GLYPHS_CHARS='{limit_chars}'")
            print(f"[Slint Compiler] Embedding font sizes: {font_sizes_str}")
            os.environ["SLINT_FONT_SIZES"] = font_sizes_str
            os.environ["SLINT_LIMIT_GLYPHS_THRESHOLD"] = str(limit_threshold)
            os.environ["SLINT_LIMIT_GLYPHS_CHARS"] = limit_chars
            args = [
                slint_compiler_path,
                "firmware/src/slint/app-window.slint",
                "-I", "firmware/src/slint",
                "-I", "firmware/src/slint/ui",
                "--embed-resources", "embed-for-software-renderer",
                "-o", "firmware/src/generated/app-window.h",
                "--cpp-file", "firmware/src/generated/app-window.cpp"
            ]
            result = subprocess.run(args, capture_output=True, text=True)
            if result.returncode == 0:
                print("[Slint Compiler] Compilation successful!")
            else:
                print(f"[Slint Compiler] Compilation failed with exit code: {result.returncode}")
                print(result.stderr)
                raise Exception("Slint compilation failed")
        else:
            print(f"[Slint Compiler] Warning: Host compiler not found yet at: {slint_compiler_path}")
            # Ensure placeholders exist so dependency scanning doesn't fail
            for t in target:
                t_path = str(t)
                if not os.path.exists(t_path):
                    with open(t_path, "w") as f:
                        f.write("// Slint placeholder\n")

    # Register the compiler action to run when any .slint file changes
    import glob
    slint_sources = glob.glob("firmware/src/slint/**/*.slint", recursive=True)
    env.Command(
        [h_path, cpp_path],
        slint_sources,
        compile_slint_files
    )

    slint_prebuilt_dir = os.path.abspath(os.path.join(".pio", "build", env["PIOENV"], "slint-prebuilt", "Slint-cpp-1.16.1-xtensa-esp32s3-none-elf"))
    slint_lib_path = os.path.join(slint_prebuilt_dir, "lib", "libslint_cpp.a")
    slint_include_dir = os.path.join(slint_prebuilt_dir, "include")
    slint_include_slint_dir = os.path.join(slint_include_dir, "slint")
    
    # Use SCons dynamic variable expansion to only link for the main firmware elf target
    env.Append(LINKFLAGS=[f"${{'-Wl,--whole-archive {slint_lib_path.replace(os.sep, '/') } -Wl,--no-whole-archive' if 'bootloader' not in str(TARGETS[0]) else ''}}"])
    env.Append(CPPPATH=[slint_include_dir, slint_include_slint_dir])