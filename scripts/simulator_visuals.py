"""Render base/current UI with identical fixtures and write a portable visual report."""
import argparse
from collections import Counter
import html
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from urllib.parse import quote

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
PANELS = [("round-466", 466, 466, False), ("round-240", 240, 240, False),
          ("square-410x502", 410, 502, True)]


def render(ui_root, output, log, baseline=False):
    for name, width, height, square in PANELS:
        command = [sys.executable, str(ROOT / "scripts/simulator.py"), "capture",
                   "--ui-root", str(ui_root), "--snapshot-dir", str(output / name),
                   "--width", str(width), "--height", str(height)]
        if square:
            command.append("--square")
        if baseline:
            command.append("--baseline")
        print(f"Rendering {ui_root.name}: {name}", flush=True)
        subprocess.run(command, cwd=ROOT, stdout=log, stderr=log, check=True)


def compare_image(before, after, diff_path, pixel_threshold=2):
    if before is None:
        return {"status": "added", "changed_pixels": None, "percent": None}
    if after is None:
        return {"status": "removed", "changed_pixels": None, "percent": None}
    with Image.open(before) as image:
        left = image.convert("RGBA")
    with Image.open(after) as image:
        right = image.convert("RGBA")
    if left.size != right.size:
        return {"status": "resized", "changed_pixels": None, "percent": None,
                "before_size": left.size, "after_size": right.size}
    delta = ImageChops.difference(left, right)
    channels = delta.split()
    maximum = channels[0]
    for channel in channels[1:]:
        maximum = ImageChops.lighter(maximum, channel)
    mask = maximum.point(lambda value: 255 if value > pixel_threshold else 0)
    changed = mask.histogram()[255]
    # Keep the context legible and mark changed pixels in magenta.
    context = right.convert("L").point(lambda value: int(value * 0.45)).convert("RGB")
    context.paste((255, 55, 135), mask=mask)
    diff_path.parent.mkdir(parents=True, exist_ok=True)
    context.save(diff_path)
    return {"status": "changed" if changed else "identical", "changed_pixels": changed,
            "percent": changed / (left.width * left.height) * 100,
            "before_size": left.size, "after_size": right.size}


def build_report(baseline, current, output, label="Visual comparison", pixel_threshold=2):
    if output.resolve().is_relative_to(baseline.resolve()) or output.resolve().is_relative_to(current.resolve()):
        raise ValueError("Report output must differ from the image directories")
    output.mkdir(parents=True, exist_ok=True)
    baseline_files = {p.relative_to(baseline).as_posix(): p for p in baseline.rglob("*.png")} if baseline.exists() else {}
    current_files = {p.relative_to(current).as_posix(): p for p in current.rglob("*.png")}
    if not current_files:
        raise ValueError("No current screenshots found")
    rows = []
    cards = []
    for name in sorted(baseline_files.keys() | current_files.keys()):
        before, after = baseline_files.get(name), current_files.get(name)
        for source, folder in [(before, "before"), (after, "after")]:
            if source:
                dest = output / folder / name
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, dest)
        result = compare_image(before, after, output / "diff" / name, pixel_threshold)
        row = {"name": name, **result}
        rows.append(row)
        images = []
        for folder, caption, source in [("before", "Base", before), ("after", "Current", after),
                                        ("diff", "Changed pixels", before and after and result["status"] != "resized")]:
            if source:
                url = f"{folder}/{quote(name)}"
                images.append(f'<figure><figcaption>{caption}</figcaption><a href="{url}"><img loading="lazy" src="{url}" alt="{caption}: {html.escape(name, quote=True)}"></a></figure>')
        metric = f'{result["percent"]:.3f}% pixels changed' if result["percent"] is not None else result["status"]
        wipe = ""
        if before and after and result["status"] != "resized":
            wipe = f'''<details><summary>Interactive wipe comparison</summary><div class="wipe" style="--cut:50%"><img src="before/{quote(name)}" alt="Base"><img class="overlay" src="after/{quote(name)}" alt="Current"></div>
<label>Base ← → Current <input class="slider" type="range" min="0" max="100" value="50" aria-label="Reveal current screenshot"></label></details>'''
        cards.append(f'<article data-status="{result["status"]}"><h2>{html.escape(name)} <span class="badge {result["status"]}">{metric}</span></h2><div class="images">{"".join(images)}</div>{wipe}</article>')
    counts = Counter(row["status"] for row in rows)
    summary = {"label": label, "total": len(rows), "counts": dict(counts),
               "pixel_threshold": pixel_threshold, "screens": rows}
    (output / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    page = f'''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>{html.escape(label)}</title>
<style>body{{font:15px system-ui;background:#10151e;color:#dfe7f5;margin:24px}}a{{color:#70bfff}}header{{position:sticky;top:0;background:#10151ef0;padding:8px 0;z-index:1}}h1{{margin:0}}h2{{font-size:18px}}article{{border-top:1px solid #293447;padding:16px 0}}.images{{display:flex;gap:20px;flex-wrap:wrap}}figure{{margin:0}}figcaption{{margin:0 0 8px;color:#a3b0c5}}img{{max-width:100%;height:auto;display:block}}figure img{{max-height:420px;object-fit:contain;object-position:left top}}.badge{{font-size:13px;font-weight:normal;margin-left:12px;color:#a3b0c5}}.changed,.resized{{color:#ffabcf}}details{{margin-top:16px}}summary{{cursor:pointer;color:#70bfff}}.wipe{{position:relative;width:max-content;max-width:100%;margin:12px 0}}.overlay{{position:absolute;top:0;left:0;clip-path:inset(0 0 0 var(--cut))}}input{{vertical-align:middle}}[hidden]{{display:none}}</style>
<header><h1>{html.escape(label)}</h1><p>{len(rows)} scenarios · {counts['changed']} changed · {counts['identical']} identical · {counts['added']} added · {counts['removed']} removed · {counts['resized']} resized</p><label><input id="changed-only" type="checkbox"> Show changes only</label> · <a href="../">Open simulator</a> · <a href="summary.json">JSON results</a></header>
<p>Base and current use identical fixtures and the current Slint renderer. Pink marks pixels differing by more than {pixel_threshold}/255 in any RGBA channel. Visual changes are advisory.</p>{"".join(cards)}
<script>document.querySelector('#changed-only').addEventListener('change',e=>document.querySelectorAll('article').forEach(a=>a.hidden=e.target.checked&&a.dataset.status==='identical'));document.querySelectorAll('.slider').forEach(s=>s.addEventListener('input',()=>s.closest('details').querySelector('.wipe').style.setProperty('--cut',s.value+'%')));</script></html>'''
    (output / "index.html").write_text(page, encoding="utf-8")
    markdown = [f"## {label}", "", f"{len(rows)} scenarios: " + ", ".join(f"{value} {key}" for key, value in sorted(counts.items())), "",
                "Visual differences are advisory. Rendering and browser interaction errors fail the check.", "",
                "| Screen | Result | Changed pixels |", "| --- | --- | --- |"]
    for row in rows:
        metric = f'{row["percent"]:.3f}%' if row["percent"] is not None else "—"
        markdown.append(f'| {row["name"]} | {row["status"]} | {metric} |')
    (output / "summary.md").write_text("\n".join(markdown) + "\n", encoding="utf-8")
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-root", type=Path, help="Base checkout; omit for a first-release gallery")
    parser.add_argument("--output", type=Path, default=ROOT / ".pio/visual-report")
    parser.add_argument("--baseline", type=Path, help="Existing baseline screenshots; skips rendering")
    parser.add_argument("--current", type=Path, help="Existing current screenshots; skips rendering")
    parser.add_argument("--label", default="Simulator visual comparison")
    parser.add_argument("--pixel-threshold", type=int, default=2)
    parser.add_argument("--fail-on-change", action="store_true")
    args = parser.parse_args()
    if not 0 <= args.pixel_threshold <= 255:
        parser.error("pixel threshold must be between 0 and 255")
    if bool(args.baseline) != bool(args.current):
        parser.error("--baseline and --current must be supplied together")
    args.output.mkdir(parents=True, exist_ok=True)
    baseline, current = args.baseline, args.current
    if current is None:
        baseline, current = args.output / "captures/base", args.output / "captures/current"
        # These are generated capture directories, never the source checkouts.
        for folder in (baseline, current):
            resolved = folder.resolve()
            if not resolved.is_relative_to((args.output / "captures").resolve()):
                raise ValueError("Capture directory escapes the report output")
            if resolved.exists():
                shutil.rmtree(resolved)
        with (args.output / "render.log").open("w", encoding="utf-8") as log:
            render(ROOT, current, log)
            if args.base_root and (args.base_root / "firmware/src/slint/app-window.slint").exists():
                render(args.base_root.resolve(), baseline, log, baseline=True)
    summary = build_report(baseline, current, args.output / "report", args.label, args.pixel_threshold)
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as file:
            file.write((args.output / "report/summary.md").read_text(encoding="utf-8"))
    print(json.dumps(summary["counts"], sort_keys=True))
    print(f"Report: {args.output / 'report/index.html'}")
    return 1 if args.fail_on_change and any(row["status"] != "identical" for row in summary["screens"]) else 0


if __name__ == "__main__":
    raise SystemExit(main())
