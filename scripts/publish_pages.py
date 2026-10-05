"""Assemble and publish Pages content while preserving simulator previews.

Used only by trusted deployment jobs, never by the unprivileged PR build.
"""
import argparse
import os
import re
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tempfile


def destination_path(site, destination):
    relative = PurePosixPath(destination)
    if relative.is_absolute() or not relative.parts or any(part in (".", "..", ".git") for part in relative.parts):
        raise ValueError("Destination must be a relative Pages directory")
    target = (site / str(relative)).resolve()
    if not target.is_relative_to(site.resolve()) or target == site.resolve():
        raise ValueError("Destination escapes the Pages tree")
    return target


def validate_source(source):
    allowed = {".html", ".js", ".css", ".json", ".png", ".svg", ".wasm", ".slint", ".ttf", ".md", ".ts", ".txt", ".ico", ".woff", ".woff2", ".map", ".webmanifest", ".vescpkg"}
    total = 0
    for entry in source.rglob("*"):
        if entry.is_symlink() or not entry.resolve().is_relative_to(source.resolve()) or ".git" in entry.relative_to(source).parts:
            raise ValueError("Unsafe file in Pages source")
        if entry.is_file():
            if entry.suffix.lower() not in allowed and entry.name not in ("CNAME", ".nojekyll"):
                raise ValueError(f"Unexpected static file: {entry.name}")
            total += entry.stat().st_size
    if total > 1024 * 1024 * 1024:
        raise ValueError("Pages source exceeds 1 GiB")


def assemble(site, source=None, destination=None, remove=False, immutable=False):
    site.mkdir(parents=True, exist_ok=True)
    if source:
        validate_source(source)
    if destination:
        target = destination_path(site, destination)
        parts = PurePosixPath(destination).parts
        pr_parent = None
        if len(parts) == 3 and parts[0] == "simulator" and re.fullmatch(r"pr-\d+", parts[1]) and re.fullmatch(r"[a-f0-9]{40}", parts[2]):
            # Revision paths prevent cached WASM/source files showing an old PR.
            # Keep only the latest revision so open PRs do not grow the site forever.
            pr_parent = destination_path(site, "/".join(parts[:2]))
            if pr_parent.exists():
                shutil.rmtree(pr_parent)
        if immutable and target.exists():
            # A rerun is harmless; a moved tag must not replace a release preview.
            if (target / "build.json").read_bytes() != (source / "build.json").read_bytes():
                raise ValueError("Release preview already exists with different metadata")
            return
        if target.exists():
            shutil.rmtree(target)
        if not remove:
            shutil.copytree(source, target)
            if pr_parent:
                (pr_parent / "index.html").write_text(
                    f'<!doctype html><meta charset="utf-8"><meta http-equiv="refresh" content="0;url=./{parts[2]}/"><title>Simulator preview</title><a href="./{parts[2]}/">Open latest simulator preview</a>', encoding="utf-8")
    else:
        # The firmware tool owns the site root. Previews and custom-domain config
        # survive its deployments, including removed/renamed root asset files.
        for entry in site.iterdir():
            if entry.name not in (".git", "simulator", "CNAME", ".nojekyll"):
                if not entry.resolve().is_relative_to(site.resolve()):
                    raise ValueError("Existing Pages file escapes the site tree")
                if entry.is_dir():
                    shutil.rmtree(entry)
                else:
                    entry.unlink()
        for entry in source.iterdir():
            if entry.name in (".git", "simulator"):
                continue
            target = site / entry.name
            if entry.is_dir():
                shutil.copytree(entry, target, dirs_exist_ok=True)
            else:
                shutil.copy2(entry, target)
    (site / ".nojekyll").touch()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path)
    parser.add_argument("--destination")
    parser.add_argument("--remove", action="store_true")
    parser.add_argument("--immutable", action="store_true")
    args = parser.parse_args()
    if not args.remove and (not args.source or not args.source.is_dir()):
        parser.error("--source must be a built static directory")
    if args.remove and not args.destination:
        parser.error("--remove requires a destination")
    # This script only pushes when the deployment workflow explicitly calls it.
    site = Path(tempfile.mkdtemp(prefix="pubmote-pages-", dir=os.environ.get("RUNNER_TEMP")))
    subprocess.run(["git", "fetch", "origin", "gh-pages:refs/remotes/origin/gh-pages", "--depth=1"], check=True)
    subprocess.run(["git", "worktree", "add", "--detach", str(site), "origin/gh-pages"], check=True)
    assemble(site, args.source.resolve() if args.source else None, args.destination, args.remove, args.immutable)
    for name, value in [("user.name", "github-actions[bot]"), ("user.email", "41898282+github-actions[bot]@users.noreply.github.com")]:
        subprocess.run(["git", "-C", str(site), "config", name, value], check=True)
    subprocess.run(["git", "-C", str(site), "add", "--all"], check=True)
    if subprocess.run(["git", "-C", str(site), "diff", "--cached", "--quiet"]).returncode:
        subprocess.run(["git", "-C", str(site), "commit", "-m", "Update Pages content"], check=True)
        subprocess.run(["git", "-C", str(site), "push", "origin", "HEAD:gh-pages"], check=True)
    if os.environ.get("GITHUB_OUTPUT"):
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as file:
            file.write(f"site={site.as_posix()}\n")
    print(f"Assembled Pages site: {site}")


if __name__ == "__main__":
    main()
