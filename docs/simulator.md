# Desktop simulator

The `simulator` PlatformIO environment runs the firmware's actual
`firmware/src/slint/app-window.slint`, fonts, and screen components in a desktop
window. A second window controls simulated telemetry and hardware inputs. No
remote or receiver is required.

## Setup

Install PlatformIO and [Rust using rustup](https://rustup.rs/). The simulator
uses a desktop Rust toolchain, separate from the Xtensa toolchain used for local
Slint firmware builds.

- **Windows:** use the Rust MSVC toolchain and install Visual Studio Build Tools
  with **Desktop development with C++** (including the Windows SDK).
- **macOS:** install Xcode Command Line Tools with `xcode-select --install`.
  Both Apple Silicon and Intel machines use their native Rust toolchain.

Restart your terminal after installing Rust so `cargo` is on `PATH`. The first
build downloads the Slint fork and Rust dependencies and can take several minutes.
Subsequent builds reuse `.pio/simulator/target`.

## Build and run

```sh
pio run -e simulator                  # build only
pio run -e simulator -t simulate      # build and open both windows
pio run -e simulator -t exec          # same as simulate
pio run -e simulator -t simulator-test # headless integration check
```

The environment does not inherit ESP-IDF, board libraries, firmware packaging,
or the firmware pre/post-build scripts. Existing firmware default environments
remain the same. PlatformIO's native builder is replaced with a Cargo builder,
so a separate GCC/MinGW installation is unnecessary.

The Python runner also works without PlatformIO and accepts panel settings:

```sh
python scripts/simulator.py run                         # 466 × 466, round
python scripts/simulator.py run --width 240 --height 240 # 1.28-inch layout
python scripts/simulator.py run --width 410 --height 502 --square
python scripts/simulator.py test
```

Use `python3` on macOS if `python` is unavailable. UI changes are read on each
launch, without rebuilding firmware. Slint is built from the same fork and tag
defined by `SLINT_PREBUILT_REPO` and `SLINT_PREBUILT_TAG` in `platformio.ini`.
The generated Cargo manifest and dependency cache live in `.pio/simulator`.
Dependencies are locked by `simulator/Cargo.lock`. After changing the Slint pin
or simulator dependencies, use `python scripts/simulator.py build --update-lock`
and commit the updated lockfile.

## Browser simulator

The browser version runs the same UI, callbacks, service doubles, and telemetry
as the desktop simulator using WebAssembly. It has no server-side simulator
process; the UI runs entirely in your browser.

Install the stable Rust WASM target, then build and serve it locally:

```sh
rustup target add wasm32-unknown-unknown --toolchain stable
python scripts/simulator.py web-serve
```

Open `http://127.0.0.1:8080`. Use the panel selector to switch between round 466,
round 240, and rectangular 410 × 502 layouts. `--port` selects another local port.
`python scripts/simulator.py web-build` produces a static site in
`.pio/simulator/web-dist`. The runner installs a workspace-local wasm-bindgen
CLI matching the locked Rust library version on its first web build.

The browser draws through WebGL; native visual comparison fixtures use the
software renderer. Both use the pinned Slint fork, the real screens, and bundled
fonts. Renderer differences between browser and native output are not treated
as UI regressions.
The browser compensates the arc fallback paths for WebGL's stroke positioning.
Browser tests check gauge centering while preserving the on-device speed layout.
Simulated telemetry uses the firmware's speed formatting: one decimal below 10,
whole numbers once the reading rounds to 10 or above. Browser tests verify the
rounding boundary and speed/unit clearance with that original layout.
They also verify canvas buffer sizing and touch gestures at 100%, 125% and 200%
pixel density, including the browser's device-pixel ResizeObserver compatibility.
MCU font plans also require dedicated splash glyphs; vector-font previews alone
cannot detect missing embedded letters. Run `python tests/test_slint_fonts.py`
with `SLINT_COMPILER` pointing to the staged fork compiler to verify bitmap data
for all three panel sizes. `python scripts/slint_fonts.py --fetch-compiler` downloads
the pinned compiler and prints its path; CI runs this embedded-glyph check too.

## PR and release previews

Relevant UI/simulator PRs run the **Simulator previews** workflow. It runs native
interaction checks, captures 17 fixed scenarios at each of three panel sizes,
compares them with the PR's base commit, and tests the browser app in Chromium.
An actual touchscreen swipe, telemetry changes, settings navigation, and menu
connection toggling are checked in each browser layout.

The **Publish simulator previews** workflow publishes successful builds to:

- PR: `https://pubmote.com/simulator/pr-<number>/`
- Tag/release: `https://pubmote.com/simulator/releases/<tag>/`
- Visual report: append `report/` to either URL.

A single bot comment on each PR is updated with the simulator link, report link,
revision, and change count. Release notes receive the same links without replacing
the existing notes. Closed PR previews are removed; release previews are retained
and cannot be replaced with different build metadata.
PR comments link to revision-specific directories to avoid stale cached WASM and
UI assets. The PR's base URL redirects to its latest build; only that revision is
retained on Pages.

Tag pushes, published releases, and the existing automated **Publish Release**
workflow all build previews. Automated releases call the simulator workflow
explicitly because events generated by `GITHUB_TOKEN` do not trigger another
workflow. Release builds are queued rather than cancelled by a newer version.
Tags must contain simulator support; older firmware without the Slint
UI/simulator cannot be rebuilt by this workflow. Manual dispatch can rebuild a
supported tag, or produce an artifact-only branch build.

The report provides base/current images, highlighted changed pixels, a wipe
slider, and JSON/Markdown results. Differences exceeding 2/255 in any RGBA
channel count as changed pixels. Visual differences are advisory so intentional
redesigns pass. Compilation, rendering, and interaction failures fail CI.

Base and current are rendered by the same current simulator harness and Slint
pin, with fresh component instances for each fixture. This isolates UI source
changes and avoids dependence on callback order. For tags, the comparison base
is the preceding ancestor tag; the first tag produces a gallery of added scenes.
These reports do not compare two versions of firmware logic or Slint renderers.
Baseline captures tolerate fixture properties absent from older revisions and
omit screens absent from their enum; new screens appear as added in the report.
Current UI captures remain strict, and compilation failures in either revision
still fail the check. Skipped baseline fixtures are recorded in the render log.

Builds run without deployment credentials, including fork PRs. The publishing
workflow executes default-branch code, verifies the PR/tag revision, and copies
only static build artifacts. It skips superseded PR builds. Both the firmware
tool and simulator publisher deploy the complete Pages tree through a shared
queue, preserving `CNAME` and existing simulator previews in `gh-pages`.
The Pages configuration action enables Actions-based Pages publishing.

Hosted publishing becomes active after these workflow files are merged to the
default branch. The first PR introducing them still has downloadable Actions
artifacts. If the repository's `github-pages` environment requires approval,
deployments follow that existing environment rule.

## Run visual and browser checks locally

```sh
python -m pip install -r simulator/requirements-test.txt
python -m playwright install chromium
python tests/test_simulator_visuals.py
python tests/test_pages_publish.py
node --test tests/simulator_publish_test.cjs
python scripts/simulator_visuals.py --base-root /path/to/base-checkout
python scripts/simulator.py web-build
python tests/simulator_browser_test.py --dist .pio/simulator/web-dist --output .pio/simulator/browser-tests
```

Open `.pio/visual-report/report/index.html` to review the native visual diff.
Use `--fail-on-change` to make visual differences return a failing status locally.
The report renderer can use a base checkout without the simulator code: only
`firmware/src/slint` and its referenced assets are read from that checkout.
Capture diagnostics are saved to `.pio/visual-report/render.log`.

## Interaction

Click and drag in the remote window to simulate touch, including the swipe down
from Stats to Menu. Tab / Shift+Tab moves focus and Enter activates buttons.
The control window also lets you jump directly to a screen.

Disable **Animate ride** to use the speed slider. Change connection state,
board/remote battery, RSSI, footpads, charging state, and joystick axes while
the UI is running. The calibration screens receive the joystick inputs and
simulated accelerometer values. The Charge screen is accessible through the
screen selector.

Menu navigation, connection toggling, theme previews, units, board pairing and
deletion, Wi-Fi editing, calibration actions, update actions, and shutdown
confirmation have desktop service doubles. Settings and paired boards are held
only for the current process. Wi-Fi networks and updates are fixtures: the
simulator does not connect to networks or flash firmware.

The Wi-Fi keyboard supports letters, capitals, digits, and symbols. Repeated
clicks on a grouped key within 800 ms cycle its characters; wait before entering
another character from the same key. **Del** deletes a character.

## Boundaries

This simulates the application UI and device services. It does not execute the
ESP-IDF C/C++ firmware, FreeRTOS tasks, actual calibration algorithms, ESP-NOW/BLE
protocols, peripheral drivers, power sequencing, or watchdog behavior. Brightness,
rotation, auto-off, and sound controls can be edited but do not drive host hardware.
Use hardware or the existing C/C++ tests to validate those paths.

Arc gauges use the fork's desktop software renderer, but desktop rendering does
not establish panel performance, DMA behavior, or memory use on the ESP32.

The Arcade list starts empty. For playable Lua games on the desktop, use the
existing `scripts/play_game.py` runtime; the simulator does not embed the firmware
Lua VM.

The headless test compiles both windows, binds and invokes every exported UiState
callback, and checks telemetry, connection toggling, unit conversion, and Wi-Fi
text entry. New
callbacks without a simulator handler fail the check. It also renders each screen
and saves PNGs in `.pio/simulator/snapshots` for visual review.
