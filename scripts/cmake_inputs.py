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


def invalidate_bootloader_cache(build_dir, sdkconfig):
    """Refresh bootloader configuration and linker layout when SDK settings change."""
    build_dir, sdkconfig = Path(build_dir), Path(sdkconfig)
    if not sdkconfig.is_file():
        return False
    # Version the cache contract: v2 also refreshes SDK-dependent linker scripts.
    fingerprint = b"v2:" + hashlib.sha256(sdkconfig.read_bytes()).hexdigest().encode()
    recorded = build_dir / "bootloader-sdkconfig.sha256"
    cache = build_dir / "bootloader" / "CMakeCache.txt"
    reply_dir = build_dir / "bootloader" / ".cmake" / "api" / "v1" / "reply"
    changed = not recorded.exists() or recorded.read_bytes() != fingerprint
    invalidate = (cache.exists() or reply_dir.exists()) and (changed or not cache.exists())
    if invalidate:
        cache.unlink(missing_ok=True)
        # PlatformIO checks the app cache even for the bootloader. Empty its
        # generated API reply too, so the bootloader code model is regenerated.
        for reply in reply_dir.glob("*.json"):
            reply.unlink()
    if changed:
        for name in ("memory.ld", "sections.ld"):
            (build_dir / name).unlink(missing_ok=True)
        build_dir.mkdir(parents=True, exist_ok=True)
        recorded.write_bytes(fingerprint)
    return invalidate
