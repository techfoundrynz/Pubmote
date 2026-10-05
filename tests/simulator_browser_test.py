"""Interaction checks against the built WASM UI, served below a PR-like URL."""
import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
from io import BytesIO
from pathlib import Path
import threading

from playwright.sync_api import sync_playwright
from PIL import Image


def check_round_stats(page):
    """Check rendered geometry, including the browser's arc fallback path."""
    image = Image.open(BytesIO(page.locator("#remote").screenshot())).convert("RGB")
    width, height = image.size
    row = [x for x in range(width) if max(image.getpixel((x, height // 2))) > 40]
    assert abs((row[0] + row[-1]) / 2 - (width - 1) / 2) <= 2, "Gauge is shifted/clipped"
    radius = (width - 34 * width / 240) / 2 - 4 * width / 240 - 1
    for y in range(int(height * .40), int(height * .64)):
        for x in range(width):
            rgb = image.getpixel((x, y))
            if min(rgb) >= 110 and max(rgb) - min(rgb) <= 3:
                assert (x - width / 2) ** 2 + (y - height / 2) ** 2 < radius ** 2, "Speed readout overlaps gauge"


class QuietHandler(SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dist", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    # Real deployment paths have a prefix. Test URL resolution under that prefix.
    handler = partial(QuietHandler, directory=str(args.dist.resolve().parent))
    server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    results = []
    try:
        with sync_playwright() as playwright:
            browser = playwright.chromium.launch(args=["--enable-unsafe-swiftshader"])
            panels = ["466,466,0", "240,240,0", "410,502,1"]
            for panel, density in [(panel, density) for panel in panels for density in [1, 1.25, 2]]:
                filename = f"{panel.replace(',', '-')}-dpr{density}"
                page = browser.new_page(viewport={"width": 1280, "height": 1000}, device_scale_factor=density)
                errors = []
                page.on("pageerror", lambda error: errors.append(str(error)))
                page.goto(f"http://127.0.0.1:{server.server_port}/{args.dist.name}/?panel={panel}")
                page.wait_for_function("document.body.dataset.ready || document.body.dataset.error", timeout=90000)
                assert page.locator("body").get_attribute("data-ready") == "true", page.locator("#status").inner_text()
                page.wait_for_function("window.simulator.state('screen') === 'stats'")
                # Check the backing buffer, not just CSS dimensions. A mismatch
                # crops both the real UI and touchscreen coordinates on Retina.
                page.wait_for_function("""() => ['remote', 'controls'].every(id => {
                    const canvas = document.getElementById(id), rect = canvas.getBoundingClientRect();
                    return canvas.width === Math.round(rect.width * devicePixelRatio)
                        && canvas.height === Math.round(rect.height * devicePixelRatio);
                })""")
                page.evaluate("simulator.control('animate', false); simulator.control('speed', 40)")
                page.wait_for_function("simulator.state('speed') === '40'")
                if panel.endswith(",0"):
                    for speed, expected in [(0, "0.0"), (8.8, "8.8"), (9.94, "9.9"), (9.95, "10"), (28.8, "29"), (60, "60")]:
                        page.evaluate("speed => simulator.control('speed', speed)", speed)
                        assert page.evaluate("simulator.state('speed')") == expected
                        page.wait_for_timeout(150)
                        check_round_stats(page)
                    page.evaluate("simulator.control('speed', 40)")
                    page.locator("#remote").screenshot(path=str(args.output / f"stats-{filename}.png"))
                assert page.evaluate("simulator.state('is-connected')") is True
                page.evaluate("simulator.control('connected', false)")
                page.wait_for_function("simulator.state('speed') === '0.0'")
                assert page.evaluate("simulator.state('is-connected')") is False
                page.evaluate("simulator.control('connected', true)")
                page.wait_for_function("simulator.state('speed') === '40'")
                # Exercise an actual touchscreen drag, rather than only test hooks.
                bounds = page.locator("#remote").bounding_box()
                x = bounds["x"] + bounds["width"] / 2
                y = bounds["y"] + bounds["height"] * 0.25
                page.mouse.move(x, y)
                page.mouse.down()
                page.mouse.move(x, y + bounds["height"] * 0.5, steps=15)
                page.mouse.up()
                page.wait_for_function("simulator.state('screen') === 'menu'")
                # Invoke the same callbacks the actual menu buttons call.
                page.evaluate("simulator.invoke('open-settings')")
                page.wait_for_function("simulator.state('screen') === 'settings'")
                page.evaluate("simulator.invoke('settings-save')")
                page.wait_for_function("simulator.state('screen') === 'menu'")
                page.evaluate("simulator.invoke('menu-connect')")
                page.wait_for_function("simulator.state('is-connected') === false")
                page.screenshot(path=str(args.output / f"{filename}.png"), full_page=True)
                assert not errors, errors
                checks = ["load", "DPI buffer sizing", "telemetry", "connection", "touch swipe", "settings", "menu disconnect"]
                if panel.endswith(",0"):
                    checks += ["gauge centering", "firmware speed formatting", "speed/unit clearance"]
                results.append({"panel": panel, "density": density, "status": "passed", "checks": checks})
                page.close()
            if (args.dist / "report/index.html").exists():
                page = browser.new_page(viewport={"width": 1280, "height": 1000})
                page.goto(f"http://127.0.0.1:{server.server_port}/{args.dist.name}/report/")
                total = page.locator("article").count()
                assert total > 0
                expected = page.locator('article:not([data-status="identical"])').count()
                page.locator("#changed-only").check()
                assert page.locator("article:not([hidden])").count() == expected
                page.locator("#changed-only").uncheck()
                first = page.locator("details").first
                first.locator("summary").click()
                first.locator("input").focus()
                page.keyboard.press("ArrowRight")
                assert first.locator(".wipe").evaluate("element => element.style.getPropertyValue('--cut')") == "51%"
                page.screenshot(path=str(args.output / "report.png"))
                results.append({"panel": "report", "status": "passed", "checks": ["changed filter", "wipe slider"]})
                page.close()
            browser.close()
        print("Browser simulator passed: three panels at 100%, 125% and 200% density, gauge centering and touch interactions")
    finally:
        server.shutdown()
        server.server_close()
        (args.output / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
