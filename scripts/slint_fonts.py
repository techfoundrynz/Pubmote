"""MCU font sizes and the characters retained at each size."""


def font_settings(scale):
    def pixels(size):
        return int(size * scale + 0.5)

    sizes = [10, 11, 12, 14, 28, 48, 54, 64]
    small = "/".join(str(pixels(size)) for size in sizes[:5])
    digits = "0123456789.,- "
    # The wordmark needs letters at its own size. Other large text is numeric.
    plan = (
        f"Saira Thin SemiBold={small}/{pixels(48)}:{digits}"
        f"/{pixels(54)}:pubmote /{pixels(64)}:{digits};"
        f"JetBrains Mono Medium={small};"
        f"lucide=@own/{pixels(14)}"
    )
    return {
        "SLINT_FONT_SIZES": ",".join(str(size) for size in sorted({pixels(s) for s in sizes}) if size <= 250),
        "SLINT_FONT_PLAN": plan,
    }


if __name__ == "__main__":
    import argparse
    import os
    from pathlib import Path
    import platform
    from sync_slint_lsp import read_prebuilt_pin, download

    parser = argparse.ArgumentParser(description="Fetch the pinned compiler for bitmap font tests")
    parser.add_argument("--fetch-compiler", action="store_true", required=True)
    parser.parse_args()
    repo, tag, _ = read_prebuilt_pin()
    host = {"Windows": "windows-x86_64.exe", "Linux": "linux-x86_64", "Darwin": "macos-aarch64"}[platform.system()]
    path = Path(__file__).resolve().parents[1] / ".pio" / "font-tests" / tag / ("slint-compiler.exe" if os.name == "nt" else "slint-compiler")
    if not path.exists():
        download(f"https://github.com/{repo}/releases/download/{tag}/slint-compiler-{host}", path)
    print(f"SLINT_COMPILER={path}")
    if os.environ.get("GITHUB_ENV"):
        with open(os.environ["GITHUB_ENV"], "a", encoding="utf-8") as output:
            output.write(f"SLINT_COMPILER={path}\n")
