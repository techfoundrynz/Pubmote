"""Build/run the desktop simulator. Slint's pin comes from platformio.ini.

Also usable without PlatformIO: python scripts/simulator.py run
"""
import argparse
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from sync_slint_lsp import read_prebuilt_pin


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("build", "run", "test", "capture", "web-build", "web-serve"), nargs="?", default="run")
    parser.add_argument("--width", type=int, default=466)
    parser.add_argument("--height", type=int, default=466)
    parser.add_argument("--square", action="store_true")
    parser.add_argument("--ui-root", type=Path, help="UI checkout to render with the current simulator harness")
    parser.add_argument("--snapshot-dir", type=Path, help="Capture output directory")
    parser.add_argument("--port", type=int, default=8080, help="Local web server port")
    parser.add_argument("--update-lock", action="store_true", help="Update simulator/Cargo.lock after dependency or Slint pin changes")
    parser.add_argument("--baseline", action="store_true", help="Capture older UI with missing fixture properties/screens")
    args = parser.parse_args()
    if args.baseline and args.command != "capture":
        parser.error("--baseline is only available for capture")
    if not 120 <= args.width <= 1200 or not 120 <= args.height <= 1200:
        parser.error("panel dimensions must be between 120 and 1200")
    cargo = shutil.which("cargo")
    if not cargo:
        parser.error("Install Rust (rustup) and restart your terminal so cargo is on PATH.")
    repo, tag, _ = read_prebuilt_pin()
    project = ROOT / ".pio" / "simulator"
    project.mkdir(parents=True, exist_ok=True)
    dependency = f'git = {json.dumps("https://github.com/" + repo)}, tag = {json.dumps(tag)}'
    manifest = f'''[package]
name = "pubremote-simulator"
version = "0.1.0"
edition = "2024"

[[bin]]
name = "pubremote-simulator"
path = {json.dumps((ROOT / "simulator" / "main.rs").as_posix())}

[lib]
name = "pubremote_web"
path = {json.dumps((ROOT / "simulator" / "web.rs").as_posix())}
crate-type = ["cdylib"]

[dependencies]
slint = {{ {dependency}, default-features = false, features = ["std", "compat-1-2", "unstable-fontique-011"] }}
slint-interpreter = {{ {dependency}, default-features = false, features = ["std", "compat-1-2"] }}
web-time = "1.1"

[target.'cfg(not(target_arch = "wasm32"))'.dependencies]
slint-interpreter = {{ {dependency}, default-features = false, features = ["backend-winit", "renderer-software"] }}
i-slint-backend-testing = {{ {dependency}, features = ["renderer-software"] }}
spin_on = "0.1"
png = "0.18"

[target.'cfg(target_arch = "wasm32")'.dependencies]
slint-interpreter = {{ {dependency}, default-features = false, features = ["backend-winit", "renderer-femtovg"] }}
i-slint-backend-winit = {{ {dependency}, default-features = false, features = ["renderer-femtovg"] }}
wasm-bindgen = "0.2"
wasm-bindgen-futures = "0.4"
js-sys = "0.3"
console_error_panic_hook = "0.1"
web-sys = {{ version = "0.3", features = ["Window", "Document", "HtmlCanvasElement", "Response"] }}
'''
    path = project / "Cargo.toml"
    if not path.exists() or path.read_text(encoding="utf-8") != manifest:
        path.write_text(manifest, encoding="utf-8")
    tracked_lock = ROOT / "simulator/Cargo.lock"
    if tracked_lock.exists():
        shutil.copy2(tracked_lock, project / "Cargo.lock")
    environ = os.environ.copy()
    environ["CARGO_TARGET_DIR"] = str(project / "target")
    if args.command.startswith("web-"):
        return build_web(cargo, path, project, environ, args)
    command = [cargo, "build" if args.command == "build" else "run", "--manifest-path", str(path)]
    command += ["--bin", "pubremote-simulator"]
    if not args.update_lock:
        command += ["--locked"]
    if args.command != "build":
        command += ["--", "--root", str(ROOT), "--width", str(args.width), "--height", str(args.height)]
        if args.square:
            command += ["--square"]
        if args.command == "test":
            command += ["--smoke-test"]
        if args.command == "capture":
            command += ["--capture"]
            if args.baseline:
                command += ["--baseline"]
        if args.ui_root:
            command += ["--ui-root", str(args.ui_root.resolve())]
        if args.snapshot_dir:
            command += ["--snapshot-dir", str(args.snapshot_dir.resolve())]
    environ.setdefault("SLINT_BACKEND", "winit-software")
    result = subprocess.call(command, cwd=ROOT, env=environ)
    if result == 0 and args.update_lock:
        shutil.copy2(project / "Cargo.lock", tracked_lock)
    return result


def build_web(cargo, manifest, project, environ, args):
    # Desktop users may have an ESP toolchain as their default. WASM uses stable.
    command = [cargo, "+stable", "build", "--manifest-path", str(manifest),
               "--lib", "--release", "--target", "wasm32-unknown-unknown"]
    if not args.update_lock:
        command += ["--locked"]
    subprocess.check_call(command, cwd=ROOT, env=environ)
    if args.update_lock:
        shutil.copy2(project / "Cargo.lock", ROOT / "simulator/Cargo.lock")
    lock = (project / "Cargo.lock").read_text(encoding="utf-8")
    version = re.search(r'name = "wasm-bindgen"\nversion = "([^"]+)"', lock).group(1)
    cli_root = ROOT / ".pio" / "wasm-tools"
    bindgen = cli_root / "bin" / ("wasm-bindgen.exe" if os.name == "nt" else "wasm-bindgen")
    if not bindgen.exists() or subprocess.check_output([str(bindgen), "--version"], text=True).strip() != f"wasm-bindgen {version}":
        tool_env = environ.copy()
        tool_env["CARGO_TARGET_DIR"] = str(ROOT / ".pio" / "wasm-cli-target")
        subprocess.check_call([cargo, "+stable", "install", "wasm-bindgen-cli", "--version", version,
                               "--locked", "--root", str(cli_root)], env=tool_env)
    dist = project / "web-dist"
    if not dist.resolve().is_relative_to(project.resolve()) or dist.resolve() == project.resolve():
        raise ValueError("Web output escapes the simulator build directory")
    if dist.exists():
        shutil.rmtree(dist)
    dist.mkdir(parents=True, exist_ok=True)
    subprocess.check_call([str(bindgen), "--target", "web", "--out-dir", str(dist / "pkg"),
                           str(project / "target/wasm32-unknown-unknown/release/pubremote_web.wasm")])
    shutil.copytree(ROOT / "simulator" / "web", dist, dirs_exist_ok=True)
    shutil.copy2(ROOT / "simulator/controls.slint", dist / "controls.slint")
    shutil.copytree(ROOT / "firmware/src/slint", dist / "firmware/src/slint", dirs_exist_ok=True)
    report = ROOT / ".pio/visual-report/report"
    if report.exists():
        shutil.copytree(report, dist / "report", dirs_exist_ok=True)
    print(f"Browser simulator built: {dist}")
    if args.command == "web-serve":
        print(f"Open http://127.0.0.1:{args.port}")
        return subprocess.call([sys.executable, "-m", "http.server", str(args.port),
                                "--bind", "127.0.0.1", "--directory", str(dist)])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
