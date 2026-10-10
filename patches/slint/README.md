# Embedded renderer optimizations

The hardened renderer implementation and its comparison tests live in the Slint fork.
PubRemote's release pin still selects `mcu-v1.19.3` while the new package builds.

The hardened change is committed to the fork as [`4bba41bd5`](https://github.com/techfoundrynz/slint/commit/4bba41bd5d9a386bcf2b41d07b9cacc24c563c53), tagged `mcu-v1.19.4`.
The [MCU package workflow](https://github.com/techfoundrynz/slint/actions/runs/38017877001) builds and publishes its release assets after validation.

## Behavior and memory

The direct bitmap scanline path avoids generic unit-step sampling without allocating a cache.
Unsupported sampling, formats, scaling, and rotations retain the generic path.

The added heap caches are opt-in through Rust's `software-renderer-caches` feature or CMake's `SLINT_FEATURE_SOFTWARE_RENDERER_CACHES` option.
The fork's MCU packaging workflow and `scripts/use_local_slint.py` explicitly enable them.
Desktop defaults leave them disabled.

The bitmap layout cache retains at most 24 entries and 8 KiB of accounted payload.
It keys complete font requests, resolved fonts, text, scale, dimensions, alignment, wrapping, elision, and line limits.
Masks retain at most 64 KiB of pixels, with a 16 KiB limit per text item.
Ordered groups preserve overlapping glyph blend order.
Color, opacity, translation, rotation, and clipping are applied during each draw.
Long labels, styled text, vector fonts, SDF fonts, and unsupported geometry retain the existing paths.
Component destruction, window changes, and bitmap font registration clear the text cache.

RGB565 big-endian palettes use at most eight 256-color tables, approximately 4.2 KiB.
Spans shorter than 32 pixels and nonuniform backgrounds use ordinary blending.
New keys require a second observation within an eight-key admission window before allocating a table.
Corner geometry uses at most four tables, approximately 4.2 KiB, and computes only requested rows.
Eviction reuses table storage and resets row validity.
These thread-local tables persist until thread exit.
Freestanding storage follows Slint's existing single-threaded contract.

Large mask/scratch buffers and cache vectors use fallible reservations.
Failure retains ordinary glyph rendering or uncached calculations.
Masks retain their Vec allocation instead of copying pixels into a second reference-counted allocation.
Small reference-count headers still use the ordinary, infallible allocator.
Allocator overhead, construction scratch space, and scene references to evicted masks are additional to retained budgets.
These changes do not guarantee a total heap ceiling or PSRAM placement.

## Reproduce and validate

Use a fresh destination and run from PubRemote's root in PowerShell:

```powershell
git clone --branch mcu-v1.19.4 https://github.com/techfoundrynz/slint.git .pio/slint/source
$env:CARGO_TARGET_DIR = Join-Path (Get-Location) '.pio/cargo_target/host-text-cache'
cargo +stable test --release --manifest-path .pio/slint/source/tests/software-renderer-cache/Cargo.toml
cargo +stable test --release --manifest-path .pio/slint/source/tests/software-renderer-cache/Cargo.toml --no-default-features
cargo +stable test --release --manifest-path .pio/slint/source/tests/software-renderer-cache/Cargo.toml --features vector-fonts
cargo +stable test --release --manifest-path .pio/slint/source/tests/software-renderer-cache/Cargo.toml --features sdf-fonts
cargo +stable test --release --manifest-path .pio/slint/source/internal/renderers/software/Cargo.toml --no-default-features --features std,testing --lib
cargo +stable test --release --manifest-path .pio/slint/source/internal/renderers/software/Cargo.toml --no-default-features --features std,testing,embedded-caches --lib
```

The fixture build script explicitly selects bitmap, vector, or SDF resources.
Embedding environment variables are unnecessary.
The cache-disabled bitmap fixture verifies that opt-out leaves the cache empty.
Each font configuration compares 1,440 frames with full and partial repainting across all four rotations.
Cases include multiline labels, changing spacing and line height, negative/fractional positions, and cache cleanup.
The renderer tests cover RGB888, both RGB565 byte orders, premultiplied RGBA, palette admission, lazy corners, bounds, and allocation fallback.
The package also runs the existing arc partial-repaint comparisons.
The MCU workflow requires the CMake feature check, renderer units, and all font fixtures before publishing.

Use a fresh shell with ESP Rust installed for the library build:

```powershell
python scripts/use_local_slint.py --slint-dir .pio/slint/source --env pingumote_esp32s3_touch_amoled_132
platformio run -e pingumote_esp32s3_touch_amoled_132
```

The helper stages the local archive and forces relinking.
Restore the published archive, or clean and rebuild the board environment, after experiments.

## ESP pacing and device benchmarks

Idle-aware pacing is implemented in `firmware/components/slint/src/slint-esp.cpp`.
It registers an idle hook on the UI task's CPU.
Over-budget animation frames omit the extra one-tick delay only when that CPU already executed its idle hook during the current iteration.
If registration fails or no idle execution is observed, the original delay remains.
Under-budget pacing and watchdog configuration stay unchanged.
The production form has no benchmark setter, console switch, or skip counters.
It is separate from the Slint library release and is needed to reproduce the measured delivery rate.

`menu-scroll-benchmark.patch` temporarily enables TEST_MODE=1, continuous menu scrolling, frame/heap console samples, and optional menu/stats transitions.
The original header fade and colored, rounded buttons remain enabled.
For phase logging, temporarily change the backend's `#if SLINT_PERF_LOG` block to `#if 1`.

On the device, reboot, warm up for 22 seconds, and query `render_stats` before and after 90 seconds.
Delivered FPS is the frame-counter delta divided by the device-time delta.
Confirm `menu_active=1 stats_active=0`; the inverse draw-time log is not delivered FPS.
Use `render_soak 1` to alternate menu and live statistics every 30 seconds, and `render_soak 0` to stop.

Flash only the backed-up application partition.
These experiments use active app1 at `0x700000`; the default upload target is unsuitable for that device.
Restore the original application and reverse temporary benchmark changes afterward.

The hardened fade-on image measured 48.27 FPS across two 90-second runs.
See [the performance record](../../docs/render-performance.md#27-renderer-hardening-2026-10-10) for validation and limits.

## Historical experiments

The performance record preserves prototype measurements and review findings.
Superseded renderer patches, the duplicate pacing patch, and discarded appearance experiments have been removed.
The original header fade and button fills remain the preferred configuration.

## Firmware gesture regressions

The firmware harness under `tests/ui-gestures` imports PubRemote's actual Pager, GestureArea, and AppWindow.
It dispatches native pointer events to check reversed page/menu swipes, normal swipes, page boundaries, release coordinates, jitter, and hover handling.
Run from PubRemote's root after preparing the isolated fork above:

```powershell
cargo +stable test --release --manifest-path tests/ui-gestures/Cargo.toml --test ui_gestures
```

A reversal exceeding the gesture's 10-pixel axis threshold cancels release toward the starting page.
Smaller movement is treated as jitter.
A second deliberate reversal allows the original swipe direction to resume.
The release position is applied before snap decisions, including when no final move event is delivered.
