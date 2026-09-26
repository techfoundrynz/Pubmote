"""Time a PlatformIO build and retain its log and compilation counts."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("label")
    parser.add_argument("--environment", default="pingumote_esp32s3_touch_amoled_132")
    parser.add_argument("--jobs", type=int)
    parser.add_argument("--touch-source", help="Append a comment for this build, then restore exact contents")
    parser.add_argument("--replace", nargs=2, metavar=("OLD", "NEW"), help="Make a real source edit instead of appending a comment")
    parser.add_argument("--target")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = root / ".pio" / "benchmarks"
    output.mkdir(parents=True, exist_ok=True)
    pio = shutil.which("pio") or shutil.which("platformio")
    if not pio:
        pio = str(Path.home() / ".platformio" / "penv" / "Scripts" / "platformio.exe")
    command = [pio, "run", "-e", args.environment]
    if args.jobs:
        command += ["-j", str(args.jobs)]
    if args.target:
        command += ["-t", args.target]
    source = root / args.touch_source if args.touch_source else None
    original = source.read_bytes() if source else None
    modified = None
    try:
        if source:
            if args.replace:
                old, new = (value.encode() for value in args.replace)
                if original.count(old) != 1:
                    raise ValueError("Benchmark replacement must match exactly once")
                modified = original.replace(old, new)
            else:
                modified = original + b"\n// Build benchmark: dependency invalidation probe.\n"
            source.write_bytes(modified)
        started = time.perf_counter()
        log_path = output / (args.label + ".log")
        events = []
        with log_path.open("w", encoding="utf-8") as log:
            process = subprocess.Popen(command, cwd=root, stdout=subprocess.PIPE,
                                       stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
            for line in process.stdout:
                log.write(line)
                log.flush()
                if line.startswith(("Compiling ", "Linking ", "Building ", "Creating ZIP", "[Slint Compiler]", "Checking size")):
                    events.append({"seconds": round(time.perf_counter() - started, 3), "action": line.strip()})
            returncode = process.wait()
        elapsed = time.perf_counter() - started
        log_text = log_path.read_text(encoding="utf-8", errors="replace")
        summary = {
            "label": args.label, "seconds": round(elapsed, 3),
            "exit_code": returncode, "command": command,
            "source": args.touch_source,
            "compiled": sum(line.startswith("Compiling ") for line in log_text.splitlines()),
            "retrieved": sum(line.startswith("Retrieved ") for line in log_text.splitlines()),
            "slint_generated": "[Slint Compiler] Compiling" in log_text,
            "packaged": "Creating ZIP" in log_text,
        }
        (output / (args.label + ".json")).write_text(json.dumps(summary, indent=2) + "\n")
        (output / (args.label + ".events.json")).write_text(json.dumps(events, indent=2) + "\n")
        print(json.dumps(summary, indent=2), flush=True)
        print("\n".join(log_text.splitlines()[-15:]), flush=True)
        return returncode
    finally:
        if source and modified is not None:
            if source.read_bytes() != modified:
                backup = output / (args.label + ".source-backup")
                backup.write_bytes(original)
                raise RuntimeError(f"Source changed during benchmark; not overwriting it. Original saved to {backup}")
            source.write_bytes(original)


if __name__ == "__main__":
    raise SystemExit(main())
