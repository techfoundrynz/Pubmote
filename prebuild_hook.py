Import("env")

from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def write_if_changed(path, content):
    """Keep timestamps stable when generation produces identical bytes."""
    path = Path(path)
    if path.exists() and path.read_bytes() == content:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as output:
        output.write(content)
        temporary = output.name
    os.replace(temporary, path)


project_dir = Path(env.subst("$PROJECT_DIR"))
sys.path.insert(0, str(project_dir / "scripts"))
from slint_codegen import split_resource_declarations
build_dir = Path(env.subst("$BUILD_DIR")).resolve()
generated_dir = build_dir / "slint_generated"
generated_dir.mkdir(parents=True, exist_ok=True)

# Only metadata consumers rebuild when the date or firmware version changes.
version = env.GetProjectOption("custom_firmware_version")
major, minor, patch = version.split(".")
build_id = hashlib.md5(f"{env['PIOENV']}_{version}_{datetime.now():%Y%m%d}".encode()).hexdigest()[:8]
metadata = (
    '#pragma once\n'
    f'#define HW_TYPE "{env["PIOENV"]}"\n'
    f'#define BUILD_ID "{build_id}"\n'
    f'#define VERSION_MAJOR {major}\n'
    f'#define VERSION_MINOR {minor}\n'
    f'#define VERSION_PATCH {patch}\n'
)
write_if_changed(build_dir / "build_metadata.h", metadata.encode())
env.AppendUnique(CPPPATH=[str(build_dir)])

# CMake and Slint must agree on the number and names of generated sources.
cpp_count = int(env.GetProjectOption("custom_slint_cpp_files", "8"))
if not 1 <= cpp_count <= 32:
    raise ValueError("custom_slint_cpp_files must be between 1 and 32")
cmake_args = env.BoardConfig().get("build.cmake_extra_args", "")
env.BoardConfig().update("build.cmake_extra_args", f"{cmake_args} -DSLINT_CPP_FILES={cpp_count}")

compiler_name = "slint-compiler.exe" if os.name == "nt" else "slint-compiler"
compiler = build_dir / "slint-prebuilt" / compiler_name
output_names = ["app-window.h"] + [f"app-window-{i}.cpp" for i in range(cpp_count)]
outputs = [generated_dir / name for name in output_names] + [generated_dir / "app-window-resources.h"]
placeholder = b"// Slint placeholder; generated during the build.\n"


def is_placeholder(path):
    return (path.exists() and path.stat().st_size <= len(placeholder) + 1
            and path.read_bytes().replace(b"\r\n", b"\n") == placeholder)


needs_generation = any(
    not output.exists() or is_placeholder(output)
    for output in outputs
)
# CMake needs the .cpp paths to exist while configuring. Headers must never be
# placeholders: the persistent SCons signature database survives a clean and
# would otherwise treat recreated placeholders as up-to-date derived targets.
for output in outputs:
    if output.suffix == ".cpp" and not output.exists():
        output.write_bytes(placeholder)
    elif output.suffix == ".h" and is_placeholder(output):
        output.unlink()


def macro_value(name, default=None):
    for flag in env.get("BUILD_FLAGS", []):
        if not isinstance(flag, str):
            continue
        compact = flag.replace("-D ", "-D", 1)
        if compact.startswith(f"-D{name}="):
            return compact.split("=", 1)[1].strip()
    return default


width = int(macro_value("HOR_RES", "240"))
height = int(macro_value("VER_RES", "240"))
scale = float(macro_value("SCALE_FONT", str(min(width, height) / 240.0)))
font_sizes = sorted({int(size * scale + 0.5) for size in [10, 11, 12, 14, 28, 48, 64]})
font_settings = {
    "SLINT_FONT_SIZES": ",".join(str(size) for size in font_sizes if size <= 250),
    "SLINT_LIMIT_GLYPHS_THRESHOLD": macro_value("LIMIT_GLYPHS_THRESHOLD", "125"),
    "SLINT_LIMIT_GLYPHS_CHARS": macro_value("LIMIT_GLYPHS_CHARS", "0123456789., ").replace('\\"', '"').replace("\\'", "'").strip("\"'"),
}


def compile_slint_files(target, source, env):
    if not compiler.is_file():
        raise RuntimeError(f"Slint compiler was not staged by CMake: {compiler}")
    print(f"[Slint Compiler] Compiling app-window.slint into {cpp_count} C++ files")
    # Generate off to the side, then preserve unchanged outputs. Bare output
    # filenames keep generated includes independent of the build directory.
    with tempfile.TemporaryDirectory(dir=generated_dir) as temporary:
        args = [str(compiler), str(project_dir / "firmware/src/slint/app-window.slint"),
                "-I", str(project_dir / "firmware/src/slint"),
                "-I", str(project_dir / "firmware/src/slint/ui"),
                "--embed-resources", "embed-for-software-renderer", "-o", output_names[0]]
        for name in output_names[1:]:
            args.extend(["--cpp-file", name])
        result = subprocess.run(args, cwd=temporary, env={**os.environ, **font_settings},
                                capture_output=True, text=True, encoding="utf-8")
        if result.returncode:
            raise RuntimeError(f"Slint compilation failed:\n{result.stderr}")
        public, private = split_resource_declarations((Path(temporary) / output_names[0]).read_text(encoding="utf-8"))
        write_if_changed(outputs[0], public.encode())
        write_if_changed(outputs[-1], private.encode())
        for name, output in zip(output_names[1:], outputs[1:-1]):
            content = (Path(temporary) / name).read_text(encoding="utf-8")
            include = '#include "app-window.h"'
            if content.count(include) != 1:
                raise RuntimeError(f"Unexpected Slint include layout in {name}")
            content = content.replace(include, include + '\n#include "app-window-resources.h"', 1)
            write_if_changed(output, content.encode())


# Track assets, compiler upgrades and font settings as well as .slint sources.
sources = sorted((project_dir / "firmware/src/slint").rglob("*.slint"))
sources += sorted(path for path in (project_dir / "firmware/assets").rglob("*") if path.is_file())
generated_nodes = env.Command(
    [str(path) for path in outputs],
    [str(path) for path in sources] + [str(compiler), str(project_dir / "prebuild_hook.py"),
                                     str(project_dir / "scripts/slint_codegen.py"),
                                     env.Value(json.dumps(font_settings, sort_keys=True))],
    env.VerboseAction(compile_slint_files, "Generating Slint UI"),
)
if needs_generation:
    env.AlwaysBuild(generated_nodes)

slint_dir = build_dir / "slint-prebuilt" / "current"
slint_lib = (slint_dir / "lib/libslint_cpp.a").as_posix()
env.Append(LINKFLAGS=[f"${{'-Wl,--whole-archive {slint_lib} -Wl,--no-whole-archive' if 'bootloader' not in str(TARGETS[0]) else ''}}"])
env.AppendUnique(CPPPATH=[str(slint_dir / "include"), str(slint_dir / "include/slint")])
