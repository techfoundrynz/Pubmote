"""Bundle the tested web app, report, and source metadata for static deployment."""
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    dist = ROOT / ".pio/simulator/web-dist"
    report = ROOT / ".pio/visual-report/report"
    shutil.copytree(report, dist / "report", dirs_exist_ok=True)
    shutil.copy2(ROOT / ".pio/simulator/browser-tests/results.json", dist / "browser-tests.json")
    event = json.loads(Path(os.environ["GITHUB_EVENT_PATH"]).read_text(encoding="utf-8"))
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    tag = os.environ.get("SIMULATOR_TAG", "")
    if not tag and os.environ.get("GITHUB_REF", "").startswith("refs/tags/"):
        tag = os.environ["GITHUB_REF"][len("refs/tags/"):]
    pr = event.get("pull_request", {}).get("number")
    kind = "tag" if tag else "pr" if pr else "manual"
    label = f"Release {tag}" if tag else f"PR #{pr}" if pr else "Manual build"
    metadata = {"kind": kind, "tag": tag, "pr": pr, "label": label, "commit": commit,
                "base": os.environ.get("SIMULATOR_BASE_SHA", "")}
    (dist / "build.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
    print(json.dumps(metadata, indent=2))


if __name__ == "__main__":
    main()
