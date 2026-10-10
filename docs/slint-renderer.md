# Embedded renderer optimizations

The hardened renderer implementation and its comparison tests live in the Slint fork.
PubRemote's release pin selects the published `mcu-v1.19.4` package.

The hardened change is committed to the fork as [`4bba41bd5`](https://github.com/techfoundrynz/slint/commit/4bba41bd5d9a386bcf2b41d07b9cacc24c563c53), tagged `mcu-v1.19.4`.
The [MCU package workflow](https://github.com/techfoundrynz/slint/actions/runs/38017877001) passed all checks and published its release assets.

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

## ESP pacing

Idle-aware pacing is implemented in `firmware/components/slint/src/slint-esp.cpp`.
Over-budget animation frames omit the extra one-tick delay only when the UI task's CPU ran its idle hook during the current iteration.
If registration fails or no idle execution is observed, the original delay remains.
Under-budget pacing and watchdog configuration stay unchanged.
This backend change is required to reproduce the recorded delivery rate.
See [performance measurements](render-performance.md#27-renderer-hardening-2026-10-10) for the workload, results, and limits.

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
