#!/usr/bin/env python3
"""Build libslint_cpp.a from a local Slint checkout and stage it into a PlatformIO build.

Normally the Slint library comes from a GitHub release, so trying a renderer change means
commit -> tag -> wait for CI -> bump the tag in platformio.ini. That is about ten minutes a
turn. This builds the same library locally and drops it over the staged copy, which
firmware/components/slint/CMakeLists.txt then reuses because it only downloads when
lib/libslint_cpp.a is missing. About a minute a turn instead.

    python firmware/tools/use_local_slint.py                 # default env
    python firmware/tools/use_local_slint.py --env <name>    # a specific one
    python firmware/tools/use_local_slint.py --no-build      # stage an existing build

Caveats:
  - The staged headers and slint-compiler still come from the release. That is fine for
    changing renderer internals, and wrong the moment the C++ API or the .slint language
    changes - cut a real release for those.
  - A `pio run -t clean`, or changing SLINT_PREBUILT_TAG, restages from the release and
    silently discards what this staged. Re-run it afterwards.
"""
import argparse
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SLINT = Path("C:/Repos/slint")
TARGET = "xtensa-esp32s3-none-elf"
# Must match SLINT_MCU_FEATURES in the fork's mcu_prebuilt.yaml.
FEATURES = "freestanding,renderer-software,software-renderer-path"
DEFAULT_ENV = "pingumote_esp32s3_touch_amoled_132"


def build() -> Path:
    cmd = [
        "cargo", "+esp", "build", "--release", "-p", "slint-cpp",
        "--target", TARGET, "-Zbuild-std=core,alloc",
        "--no-default-features", "--features", FEATURES,
    ]
    print(f"building: {' '.join(cmd)}")
    if subprocess.run(cmd, cwd=SLINT).returncode != 0:
        sys.exit("cargo build failed")
    lib = SLINT / "target" / TARGET / "release" / "libslint_cpp.a"
    if not lib.is_file():
        sys.exit(f"expected {lib} to exist after a successful build")
    return lib


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--env", default=DEFAULT_ENV)
    ap.add_argument("--no-build", action="store_true")
    args = ap.parse_args()

    lib = (SLINT / "target" / TARGET / "release" / "libslint_cpp.a") if args.no_build else build()
    if not lib.is_file():
        sys.exit(f"{lib} does not exist; run without --no-build")

    dest = REPO / ".pio" / "build" / args.env / "slint-prebuilt" / "current" / "lib" / "libslint_cpp.a"
    if not dest.parent.is_dir():
        sys.exit(f"{dest.parent} does not exist - build the env once so the release is staged first")

    shutil.copyfile(lib, dest)
    print(f"staged {lib.stat().st_size} bytes -> {dest}")
    print("now run: pio run -e", args.env)


main()
