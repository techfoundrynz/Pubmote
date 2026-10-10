"""Invalidate PlatformIO's CMake cache when project build definitions change."""
import hashlib
from pathlib import Path
import re

CACHE_VARIABLE = "PUBREMOTE_CMAKE_INPUT_HASH"


def cmake_input_fingerprint(project_dir, extra_args):
    project_dir = Path(project_dir)
    inputs = {project_dir / "CMakeLists.txt"}
    for directory in ("firmware/src", "firmware/modules", "firmware/components"):
        inputs.update(path for path in (project_dir / directory).rglob("*")
                      if path.is_file() and (path.name in {"CMakeLists.txt", "idf_component.yml"}
                                             or path.suffix == ".cmake"))
    digest = hashlib.sha256(extra_args.encode())
    for path in sorted(inputs):
        digest.update(b"\0" + path.relative_to(project_dir).as_posix().encode() + b"\0")
        digest.update(path.read_bytes())
    return digest.hexdigest()


def invalidate_cmake_cache(build_dir, fingerprint):
    cache = Path(build_dir) / "CMakeCache.txt"
    if not cache.is_file():
        return False  # PlatformIO already configures a fresh build.
    recorded = re.search(r"^" + CACHE_VARIABLE + r":[^=\n]+=([^\r\n]*)$",
                         cache.read_text(encoding="utf-8"), re.MULTILINE)
    if recorded and recorded[1] == fingerprint:
        return False
    cache.unlink()
    return True
