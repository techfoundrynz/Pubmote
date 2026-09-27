# Render performance: state, findings, and what to do next

Target hardware: `pingumote_esp32s3_touch_amoled_132` — ESP32-S3 @ 240 MHz, 466×466 CO5300 AMOLED
over QSPI @ 80 MHz, 8 MB octal PSRAM, Slint software renderer via `render_by_line`.

The initial sections record the 2026-08-15 investigation and include hypotheses later corrected by subsequent sessions.
Read the dated updates before treating an earlier conclusion as current.

**Latest findings (2026-09-27, second pass):** executing instructions from PSRAM
reached **54.62 and 57.34 delivered FPS**, against fresh flash-execution baselines
of **34.43 and 36.26 FPS**, with the same published renderer and `TEST_MODE=1`.
The user confirmed normal visuals and behavior. See [§18](#18-execute-instructions-from-psram-2026-09-27)
for the memory cost, measurements, and validation limits.

The follow-up in [§19](#19-follow-up-memory-and-dma-comparisons-2026-09-27)
retains earlier DMA submission for another **0.71 FPS** with profiling disabled
(57.27 to 57.98 FPS). Read-only data in PSRAM had negligible benefit and was
rejected; the proposed 64-byte instruction-cache line is unsupported.

The preceding renderer investigation reached **34.97 delivered FPS** with the published `mcu-v1.19.2` renderer, up from **28.53 FPS** initially.
The preceding local build measured 34.91 FPS.
The successful changes are DMA buffer packing, empty-center arc rejection, scanline membership reuse, and opaque-cover caching.
Slint changes are published in `mcu-v1.19.2`, and PubRemote builds successfully from the downloaded release.
The published-release firmware is installed and measured with continuous profiling disabled.
See [§17](#17-stats-screen-fps-results-and-release-2026-09-27) for controlled measurements, build discoveries, tests, limitations, and release status.

---

## 1. Where things stand

| Case | Frame time | FPS |
|---|---|---|
| Quiet / low-dirty screens (9–20% dirty) | 16.7–24.3 ms | **42–58** |
| Menu scroll (100% dirty) | 36.2 ms | **~27** |

Before this work: 18–22 fps across the board. The remaining shortfall is menu scroll, which misses
a 30 fps target by roughly 3 ms.

**Caveat that matters: a real menu scroll was never measured.** Every menu figure comes from
forcing a fully dirty frame with `RepaintBufferType::NewBuffer`. The analytical case for that being
representative is in §6. Before doing any of the work in §4, **scroll the menu and read `dirty %`
off the on-screen FPS overlay.** If it reads ~18% like every other screen rather than ~100%, the
target is already met and this document's premise is wrong.

---

## 2. The frame budget, fully attributed

Menu screen, fully dirty, 36.2 ms total. No unexplained time remains:

```
prepare 15.7   ├─ setup  1.2   (dirty-region computation; skipped entirely under NewBuffer)
               ├─ walk  14.4   (render_component_items — the item-tree traversal)
               │    ├─ text      5.6  over 13 draw_text calls (of which shaping only 1.4)
               │    └─ non-text  8.8
               └─ scene  0.7   (Scene::new, including both sorts)
render  14.2   (per-line rasterisation)
wait     3.6   (blocked on panel DMA)
other    2.3   (of which drawcall 2.1 — 22 × esp_lcd_panel_draw_bitmap @ ~95 µs)
```

Item counts during that frame: **71 visited, 38 drawn**.

Low-dirty frames split differently — `setup` rises to ~6.5 ms because `compute_dirty_regions`
actually runs, and `walk` falls to ~6.2 ms because `filter_item` culls most items.

### The key number

14.4 ms of walk over 71 items is **~200 µs (≈48,000 cycles) per item**. A plain `Rectangle` should
cost single-digit microseconds. That is not an algorithmic defect — see §3.

---

## 3. The hardware floor

The walk touches roughly 24 KB of distinct code per item against a 32 KB instruction cache, and
that code is fetched from flash. **Flash runs in DIO mode at 80 MHz — 2-bit, ~20 MB/s.** The
renderer is instruction-fetch starved.

This cannot be improved on this board:

- **QIO** (4-bit, would roughly double fetch bandwidth): unavailable. The octal PSRAM
  (`CONFIG_SPIRAM_MODE_OCT`) claims the extra data lines.
- **120 MHz flash**: rejected by ESP-IDF at compile time —
  `static assertion failed: "FLASH and PSRAM Mode configuration are not supported"`.
- **Instruction cache**: already at its 32 KB maximum (`CONFIG_ESP32S3_INSTRUCTION_CACHE_32KB`).

Corroborating evidence: setting `opt-level = "s"` on `i-slint-core` removed ~26 KB of code and
bought **0.5 ms**. Code size converts to frame time on this target at roughly 20 µs per KB removed,
which is why size-reduction levers are worth considering but cannot close a 3 ms gap on their own —
stripping the *entire* panic machinery would be 1–2 ms.

**Implication:** the fix is not to make the walk faster. It is to stop doing the walk.

---

## 4. The remaining path: mutation-time invalidation

Slint re-walks the whole item tree every frame. During a scroll, every item under the Flickable
moves, nothing is culled, and all 71 items are re-visited and re-emitted as scene items. LVGL hit
40+ fps on comparable UI because it invalidates at mutation time and has no equivalent per-frame
traversal.

Two variants, smaller first.

### 4a. Subtree pruning (bounded, ~1.3–3.3 ms)

> Shipped, then blanked the screen after pairing. See §14 for the coordinate-space bug and the
> fix; the sketch below is left as written.

`render_item_children` (`internal/core/item_rendering.rs:205-232`) always recurses into children;
only *drawing* is filtered by `filter_item`. Items scrolled out of the Flickable viewport are still
walked. Measured headroom: 33 of 71 visited items are not drawn.

Sketch:
1. Add `subtree_rect: LogicalRect` to `PartialRenderingCachedData`
   (`internal/core/partial_renderer.rs:159-169`), computed post-order as the union of an item's own
   **geometry** and its children's `subtree_rect`s.
2. Add `PartialRenderer::filter_subtree(&ItemRc) -> bool` testing that rect, transformed to screen
   space, against the dirty region.
3. In `render_item_children`, when `do_draw` is false **and** `filter_subtree` is false, skip the
   recursion at line 231.
4. Keep the existing unconditional-descend cases exactly as they are: `clips_children`, `BoxShadow`,
   `Transform`, `Opacity`, `Layer`.

**Traps, both found by adversarial review:**
- Six item types deliberately zero their size in `bounding_rect` — `Empty` (`items.rs:306-314`),
  `TouchArea`, `FocusScope`, the swipe handlers, `DragArea`/`DropArea`. Union over `bounding_rect`
  would under-cover and prune visible content. Use `geometry`.
- The subtree rect must include the fork's arc invalidation slack (`Path::arc_dirty_rect`,
  `ARC_BAND_SLACK`) or a swept arc gets clipped away.

**This is verifiable without looking at the panel** — which is what makes it the right first step.
Phase marks 9/10 already count items visited and drawn (§5). After the change, `visited` must fall
while **`drawn` stays at 38**. If `drawn` drops, a visible item was pruned and the change is wrong.

Honest expectation: this lands menu scroll at **28–30 fps**. On the line at best, not comfortably
past it.

### 4b. Scene caching for pure translations (the real fix, larger)

A Flickable translates all its children by a common delta. The scene items emitted for that subtree
are identical to the previous frame except for an offset. Cache the emitted `SceneItem`s per subtree
along with the transform they were built under, and on a frame where only a uniform translation
changed, re-emit them with the offset instead of re-walking.

This is the change that actually removes the per-frame walk. It is also a redesign of the
partial-rendering path, and its failure mode is items rendering stale or in the wrong place. It
needs someone watching the panel while it is developed.

---

## 5. Instrumentation that already exists

The fork carries a diagnostic hook, `slint_esp_phase_mark(u32)`, implemented firmware-side in
`slint-esp.cpp` and called from the renderer. It compiles to nothing off-Xtensa
(`#[cfg(target_arch = "xtensa")]`).

| Phase | Meaning |
|---|---|
| 0–4 | `prepare_scene` boundaries → `setup`, `walk`, `scene` |
| 5/6 | brackets a text shaping pass (`ShapeBuffer::new`) |
| 7/8 | brackets `draw_text` in the software renderer |
| 9/10 | counts items visited / items drawn in the walk |

Tags `mcu-v1.18.12` … `mcu-v1.18.15` and `mcu-v1.18.21` carry these. **`mcu-v1.18.11` is the clean
shipping point** if you want them gone.

The 60-frame perf log prints the full split; the on-screen FPS overlay shows `p`/`r`/`f` and
`dirty %` without needing a serial monitor (which holds `firmware.elf` open for the exception
decoder and blocks relinking).

---

## 6. Measured and eliminated — do not re-try these

Each was implemented or configured and measured on hardware.

| Candidate | Measured | Verdict |
|---|---|---|
| Text-shaping cache | shaping is 1.4 ms of a 14.4 ms walk | Was about to be written; refuted. Each Text shapes ~once, not 3×. |
| `Scene::new` counting sort | whole `scene` phase is 0.7 ms | Sort is less than that. Estimate of 0.8–2 ms was wrong. |
| Address-window consolidation | 2.1 ms (22 calls × 95 µs) | Real, but leaves 34.1 ms ≈ 29 fps. Needs driver surgery. |
| Subtree pruning | 33 of 71 items culled | 1.3–3.3 ms → 28–30 fps. Best remaining option; see §4a. |
| Direct framebuffer (`render()` into PSRAM) | menu 18 → **10 fps** | Rasterising into a 466-stride PSRAM buffer costs more than the ~15 ms Scene saving. Also loses the fork's analytic arcs (they exist only in `PrepareScene`), and the stock path rasteriser draws black seams across a thick stroke. Kept behind `USE_DIRECT_FB`, default off. |
| Fat LTO | 16 bytes of archive change, no CI time change | Silently inert: `api/cpp` declares `crate-type = ["lib", "cdylib", "staticlib"]` and rustc cannot fat-LTO a unit that also emits an rlib. Would need the staticlib split out. |
| Incremental prepare (geometry tracker) | refuted 3/3 adversarially | The win only exists on *static* frames, and this device never draws one — `needs_redraw` is only set by `request_redraw()`, and e.g. `speed-display.slint` centres a label via `x: (parent.width - self.width)/2`, so every telemetry update dirties real geometry. Also desynchronises dirty flags (`mark_dependencies_dirty` short-circuits on an already-dirty dependent) and can freeze the UI mid-fling. |
| 120 MHz flash | compile-time rejection | Incompatible with octal PSRAM. |
| `panic_immediate_abort` | 3 failed CI builds | Became a real panic strategy in this toolchain; profile setting also rejected. Projected 1–2 ms anyway. |

### Shipped and kept

- **Arc invalidation slack** — the big correctness fix. Invalidation granted 1 logical px while the
  renderer paints up to **2.8 px** (ring) and **5 px** (round cap) outside the exact geometry:
  `software/lib.rs:2443-2447` truncates the arc centre and both radii into `i16`, anti-aliasing
  reaches half a pixel past every span end, and caps are discs on the same truncated centre line.
  `ARC_BAND_SLACK` is now 6, floor pinned at 5 by `bounds_cover_what_the_renderer_actually_paints`.
  This defeated four previous attempts and stayed invisible to five test suites because every
  fixture used an **integer centre and integer radii**, which makes all of those errors identically
  zero. *Never test arc invalidation with integral geometry.*
- **`ARC_GRID` 3 → 7** — once the slack is correct the grid is purely an area dial. A cell went from
  11% of the panel to 2%; stats-page dirty fell from 27% to 5–9%. `pending` is `u64` for 49 cells.
- **Renderer hot paths** — opaque fills write 32-bit pairs (the default `blend_slice` lowers to a
  non-unrolled 16-bit store loop on Xtensa); the AlphaMap glyph blend hoists loop-invariant colour
  channels; rectangles that paint nothing return early.
- **`Rgb565BigEndianPixel`** — renders straight into panel byte order, removing a 3.9 ms full-frame
  byte swap. Net only ~0.3 ms (the BE blend costs more on Xtensa, which has no byte-swap
  instruction, and the freed CPU exposes DMA wait) but it hands 3.9 ms of CPU back to other tasks.
- **`opt-level = "s"` on `i-slint-core`** — 0.5 ms; see §3.
- **Config**: icache 16 → 32 KB, data cache lines 32 → 64 B,
  `SPIRAM_MALLOC_ALWAYSINTERNAL` 2048 → 8192 (a `SceneItem` is 16 bytes, so the scene array crosses
  into PSRAM at 129 items and `Scene::next_line` reshuffles it once per scanline).
- **Removed 812 µs/frame** of measurement overhead — `esp_timer_get_time()` was being called twice
  per scanline (932 calls on a full-screen frame). `t_render` is now derived from one pair.

---

## 7. How to measure a real menu scroll (six failed attempts)

Getting a genuine scroll measurement without a finger on the panel defeated six harness designs.
Recorded so nobody burns the same afternoon:

1. **Writing `UiState.scroll_offset_y`** — does nothing. `menu.slint` feeds it into `viewport-y`
   only at `init`; the live binding runs the other way (`changed viewport-y => scroll-offset-y`).
2. **Posting a closure per poll from the display task** — `invoke_from_event_loop` at 30 ms
   intervals costs ~7.5 ms/frame and swamps the measurement.
3. **`slint::Timer` created during boot** — **boot-loops the device.** `set_screen` runs
   `on_screen_changed`, which tears down the previous screen's properties; at ~2 s that races the
   initial setup. A 20 s delay avoids it.
4. **Nested timers** — boot-looped the device again.
5. **Synthetic pointer events via closures** — never reached the Flickable.
6. **Injecting synthetic coordinates into the touch-read path in `slint-esp.cpp`** — this is the
   right shape (runs on the UI thread, uses the identical dispatch path, **no crash**), but still
   produced no scroll: `dirty` stayed at 0.2%, `drawn` 1–2 of 71 visited.

The menu *is* scrollable — `AppButton` is `38px * Theme.scale` ≈ 73.8 px and six of eleven buttons
are unconditional, so content is ~576 px in a 466 px viewport even with every conditional absent.

**Just read `dirty %` off the overlay while scrolling.** Ten seconds, and it either validates the
27 fps figure or invalidates this document's premise.

---

## 8. Working notes

- The device is dual-core and affinity is already correct: `slint_event_loop` is pinned to core 1
  at priority 20; `connection`, `receiver`, `transmitter`, `thumbstick`, `slint_input` are on core 0;
  unpinned tasks sit at priority 2–5 and only run when the UI blocks on `trans_sem` during DMA.
- `sdkconfig.pingumote_esp32s3_touch_amoled_132` is tracked per-env — edit it directly, not
  `platformio.ini`.
- After changing `SLINT_PREBUILT_TAG`, the first build fails with
  `Couldn't find target config target-PubRemote.elf-<hash>.json`. Delete
  `.pio/build/<env>/.cmake` and rebuild.
- `PLATFORMIO_BUILD_FLAGS` **replaces** `[common].build_flags` rather than appending — pass the full
  set (`TX_RATE_MS`, `INPUT_RATE_MS`, `SHOW_FPS`, `SLINT_PERF_LOG`) or perf logging silently
  disappears.
- Measure A/B comparisons with the remote **untouched**. A touched run reversed the sign of the
  big-endian pixel comparison and produced a wrong conclusion.

---

## 9. Rewrite: making Slint fast on this class of target

§4 sketches the minimum change. This section is the fuller picture — what a Slint renderer designed
for a flash-XIP MCU would do differently, ordered by measured payoff.

### 9.0 Why the current architecture is expensive here

Slint's software renderer is *stateless between frames*. Every frame it:

1. walks the entire item tree to compute the dirty region (`compute_dirty_regions`),
2. walks it **again** to emit scene items (`render_component_items`),
3. sorts the scene and rasterises it per line.

On a desktop that is cheap — the tree is in L2, the code is in L1i. On this board the code is in
flash behind a 2-bit 80 MHz bus and a 32 KB instruction cache, and the walk touches ~24 KB of
distinct code per item. Measured: **~200 µs per item**, ~14.4 ms for 71 items, and that cost is paid
*whether or not anything changed*.

The architecture assumes traversal is free. Here it is the dominant term.

### 9.1 Retained display list with mutation-time invalidation — the core change

**Est. 10–13 ms of a 36 ms frame. This is the one that matters.**

Replace "rebuild the scene every frame" with "keep the scene, patch what changed".

- Give each item a slot holding the `SceneItem`s it emitted last frame, plus the transform and clip
  they were emitted under.
- On property mutation, mark that slot stale and push the item's **old** screen rect to a dirty list.
  Slint already has the hook: `PartialRenderingCachedData.tracker`
  (`internal/core/partial_renderer.rs:159-169`) is a per-item `PropertyTracker`.
- A frame becomes: drain the dirty list, re-emit only stale slots, splice into the retained scene,
  rasterise the union of old and new rects.

Cost goes from `O(items)` to `O(changed items)`. A telemetry update touching three labels stops
costing a 71-item walk.

**The known trap, and it already sank one attempt.** Geometry changes do *not* dirty the rendering
tracker: `filter_item` reads geometry under `evaluate_no_tracking` (`partial_renderer.rs:674`) and
`render()` receives `size` as a plain argument (`item_rendering.rs:205-224`). An earlier attempt to
gate work on that tracker broke menu scrolling. A retained list needs its **own** tracker over the
geometry expression, and two further hazards apply:

- Clearing the geometry tracker mid-frame while the redraw tracker is cleared at end-of-frame
  desynchronises them. `mark_dependencies_dirty` short-circuits on an already-dirty dependent
  (`properties.rs:826-843`), so once they diverge, later mutations stop reaching `request_redraw()`
  and the UI freezes mid-fling. Clear both at the same point.
- `compute_dirty_regions` returns `SkipChildren` when a clip goes empty
  (`partial_renderer.rs:539-541, 572`). A tracker whose dependency set is rebuilt per walk ends up
  with an **incomplete** set for pruned subtrees while still being marked clean.

### 9.2 Translation-only fast path — the scroll case

**Est. 8–12 ms during a scroll specifically.**

A Flickable translates every child by a common delta. With 9.1 in place this is a small addition: if
a subtree's slots are valid and the only change is a uniform translation, re-emit the cached
`SceneItem`s with an offset instead of re-walking. Scrolling stops being the worst case and becomes
close to the cheapest.

Without 9.1 this is not implementable — there is nothing cached to offset.

### 9.3 Merge the two traversals

**Est. 3–5 ms** (measured: `setup` 6.5 ms + `walk` 6.4 ms in partial mode — two full walks).

`compute_dirty_regions` and `render_component_items` visit the same tree, evaluate the same geometry,
and run back to back. `filter_item` even re-reads `item_rc.geometry()` that
`CachedItemBoundingBoxAndTransform::new` computed moments earlier (`partial_renderer.rs:670-694`).

Either merge them into one pass that computes the dirty region and emits scene items together,
deferring the emit decision until the region is known; or keep two passes but have the second consume
the first's cached geometry rather than re-deriving it. The second is much smaller and worth doing on
its own.

**Trap:** six item types deliberately zero their size in `bounding_rect` — `Empty`
(`items.rs:306-314`), `TouchArea`, `FocusScope`, the swipe handlers, `DragArea`/`DropArea`. Anything
consuming a cached rect must not confuse `bounding_rect` with `geometry`.

### 9.4 Shrink the walk's code footprint

**Est. 2–4 ms, and it compounds with everything above.**

Measured conversion rate on this target: **~20 µs of frame time per KB of code removed**
(`opt-level = "s"` on `i-slint-core` → 26 KB → 0.5 ms). The walk touching ~24 KB per item is the
whole problem, so make the hot path small and contiguous.

- **Devirtualise the item dispatch.** Every item is a vtable call into generated C++ (`item_geometry`
  is a large `switch`, `visit_children` another). A flat, data-oriented pass over an array of
  `{kind, geometry, flags}` would touch a fraction of the code.
- **Split hot from cold.** `#[inline(never)]` + `#[cold]` on the rare arms (BoxShadow, Transform,
  Opacity, Layer, DragArea) so the common Rectangle/Text path stays resident.
- **Stop monomorphising the walk** over renderer types where a `dyn` call would keep one copy.
- **Fix LTO.** `lto = "fat"` is silently dropped today because `api/cpp` declares
  `crate-type = ["lib", "cdylib", "staticlib"]` and rustc cannot fat-LTO a unit that also emits an
  rlib. Splitting the staticlib into its own package would let cross-crate inlining happen. Worth
  1–3 ms by the conversion rate above, and it is pure build configuration.

### 9.5 Arena-allocate the per-frame scene buffers

**Est. 0.5–1 ms.**

`PrepareScene { ..Default::default() }` (`software/lib.rs:1542`) allocates `items`, `vectors` and
`state_stack` fresh every frame, climbing the capacity ladder with a malloc/free/memcpy at each step
— roughly 14–16 heap operations and ~6 KB of realloc copies per frame, at 1–3 µs per ESP-IDF malloc.
Hold them in `SoftwareRenderer` as `RefCell`s, `mem::take` at frame start, `clear()` (which keeps
capacity) and hand back at the end. At steady state prepare then performs zero allocations, and the
buffers stop drifting in and out of PSRAM.

### 9.6 Text: shape once per frame

**Est. 0.4–1.2 ms. Lower priority than it looks.**

Measured: shaping is **1.4 ms of a 14.4 ms walk** — 10%, not the bulk. The
three-shaping-passes-per-Text claim did not hold on this device (13 `draw_text` calls, 21 shape
passes). Still worth memoising the `ShapeBuffer` for the duration of a frame, keyed on (font, pixel
size, letter spacing, string), and giving `ShapeBuffer::new` a `glyphs.reserve(text.len())` so the
4→8→16→32 ladder collapses to one allocation. Treat it as cleanup, not as a performance fix.

### 9.7 Panel-side scrolling (speculative)

**Est. would make menu scroll nearly free, if the panel supports it.**

Many display controllers implement hardware vertical scroll (`VSCRDEF` 0x33 / `VSCSAD` 0x37). If the
CO5300 does, a scroll becomes: move the GRAM start address, repaint only the newly exposed strip.
Dirty area collapses from ~100% to a few percent.

Not implemented in `firmware/src/display/sh8601/display_driver_sh8601.c` and not verified against the
datasheet. Caveat: it scrolls the *whole* panel, so anything meant to stay fixed — the FPS overlay —
would scroll with it.

### 9.8 Sequencing and realistic totals

| # | Change | Est. gain | Risk | Verifiable without eyes on the panel? |
|---|---|---|---|---|
| 9.4-LTO | Split the staticlib package, enable fat LTO | 1–3 ms | low | yes — flash size and frame time |
| 9.5 | Arena-allocate scene buffers | 0.5–1 ms | low | yes — allocation counter in a host test |
| 9.3 | Stop re-deriving geometry in `filter_item` | 1–2 ms | medium | partly — pixel-compare host tests |
| §4a | Subtree pruning | 1.3–3.3 ms | medium | **yes** — `drawn` must hold at 38 while `visited` falls |
| 9.1 | Retained display list | 10–13 ms | high | partly — same counters, plus host pixel tests |
| 9.2 | Translation fast path | 8–12 ms on scroll | high | partly |

The first four are incremental and together plausibly reach ~30–32 fps on menu scroll — enough to
clear the target without touching the architecture. **Do those first.** They are also individually
revertible, which matters on a target where a bad frame is only visible on the panel.

9.1 and 9.2 are the real answer and would put menu scroll comfortably past 60 fps, but they are a
redesign of the partial-rendering path with silent visual corruption as the failure mode. They want a
dedicated session with the panel in view.

### 9.9 Verification strategy — non-negotiable on this target

Every change above needs a check that is not "it looks fine", because nobody is watching 466×466
pixels at 30 Hz.

1. **The `visited`/`drawn` invariant.** Phase marks 9/10 count both. Any pruning or caching change
   must leave `drawn` **unchanged** while `visited` falls. A drop in `drawn` means a visible item was
   lost — this catches the entire "item silently vanished" class numerically, which is what makes
   §4a safe to attempt without watching the screen.
2. **Host pixel tests.** Render the same scene twice through `render_by_line` and compare buffers
   before and after the change. Ordering bugs show up as z-fighting, cache bugs as stale pixels.
3. **Allocation counters** under `#[cfg(test)]` for 9.5 — the second frame must allocate zero.
4. **Never test arc invalidation with integral geometry.** Integer centres and radii zero out every
   `i16` truncation error; that is how a 1 px slack bug survived five test suites and four fix
   attempts. Use fractional centres near `.999`.
5. **Measure A/B untouched.** A finger on the panel reversed the sign of the big-endian pixel
   comparison once already and produced a wrong conclusion.

---

## 10. Session update — corrections to everything above

Several conclusions in §1-§6 were measured while **`test_mode_task` was silently failing to
start**: the UI task's 48KB stack had starved internal RAM, so `xTaskCreate` for test mode
returned failure and the screens being measured had no simulated telemetry. Those figures were
idle screens. Reducing the stack fixed it as a side effect, and the numbers changed materially.

### What is true now (measured, test mode running)

| configuration | dirty | draw calls | frame |
|---|---|---|---|
| 3x3 cell grid | 36-45% | 56-69 | 45.7-49.8 ms |
| 7x7 cell grid | 26-28% | 144-155 | 46-53 ms |
| exact swept band | 10.6-25% | 51-99 | 33.3-45.8 ms |
| + CASET caching | 12-22% | 51-98 | **33.7-43.4 ms** |

Phase split at the worst case: `prepare 16.6-18.2 / render 12.9-17.2 / other 3.7-7.7`.

### The cell grid is gone

It was a workaround for the 1px-slack bug, not a design. With `ARC_BAND_SLACK` sized correctly
the exact swept band - LVGL's `inv_arc_area` approach - stands on its own and wins on both axes
at once: fewer dirty pixels *and* fewer DMA calls than a fine grid, because one contiguous band
beats a scatter of cells.

The 7x7 experiment is a cautionary tale worth keeping: it cut dirty area from 42% to 28% and was
**net zero**, because tripling the rect count tripled the per-chunk address-window setups at
~89us each. Dirty area is not the only currency; rect fragmentation is the other.

### Draw-call cost

`esp_lcd_sh8601.c` now caches the last CASET/RASET window and skips whichever axis has not moved.
Consecutive chunks of one flush usually share an x range, so this removed one of the two blocking
param transactions per call: **89us -> 73us**. RASET changes every chunk and cannot be skipped.

### The stack was 5x oversized

`uxTaskGetStackHighWaterMark` on the UI task: **~10KB peak unrotated, ~28KB rotated**. The 48KB
was sized for the zeno path rasteriser's single ~19KB frame, which is only reached when rotation
makes `draw_path_as_arc` bail. It is now chosen from the rotation setting - 24KB unrotated, 48KB
rotated - read once at init immediately above the task creation, so the two cannot disagree.

**Rotated screens run at ~107ms/frame (9fps)** for the same reason. Supporting rotation in the
analytic arc is small and would remove both problems: a circle is rotation-invariant, so it needs
only the centre transformed through `RotationInfo` and `orientation.angle()` added to the start
angle. `Transform::transformed` and `RenderingRotation::angle()` already exist. That would also
collapse the conditional stack to a flat 24KB.

### The arc artifact: UNRESOLVED, and narrowing is currently disabled

Symptom: an arc whose value climbs shows gaps - segments that were never painted - which
repair themselves when the arc later sweeps back over them. Worse as the frame rate drops.
Present on both dials.

**Status: `ARC_NO_NARROWING = true` in `items/path.rs`.** Every arc change invalidates the whole
element. That is always correct and costs a lot: **100% dirty every frame, ~62ms, 16fps**, against
29.5-39.8ms (25-34fps) with narrowing. Turning it back on is a one-line change.

**Eliminated by experiment.** Every one of these was implemented, measured on hardware, and
found not to be the cause:

| Hypothesis | How it was killed |
|---|---|
| Grid pitch too fine | Artifacts at 3x3, 7x7 *and* exact bands |
| Band coverage too small | 30px slack - 6x the measured need - changed nothing |
| Band in the wrong place | Emitted band and repainted region compared on device: identical for the speed dial |
| Updates lost between frames | End-angle continuity traced across consecutive frames: unbroken, every `now` is the next `before` |
| Debt cleared without painting | Gated discharge on the region actually being marked (`arc_debt_marked`); no change |
| A dropped paint | Carried the previous band forward one frame; no change |
| Cap clipped on degenerate rows | Real bug, fixed - cap pixels outside the ring run could not be painted; no change to the artifact |
| Invented pixel at an odd x edge | Real issue, fixed - the firmware replicated a neighbour into the even-alignment slack, writing outside the dirty range. Panel turns out to accept odd x, so the widening is gone. "A little better", not fixed |
| Item offset counted twice | **Real bug, fixed** - the band was translated by `geometry.origin` and then marked with a transform already carrying it. Only affected the inset dial, whose region sat 31px from its arc |
| Analytic rasteriser mis-drawing when clipped | `clipped_drawing_matches_full_width` proves it paints identically clipped or not |
| Occlusion culling / region overflow | `is_guaranteed_opaque` is false for arcs; `add_box` merges to a superset, never drops |

**What is left.** Narrowing measures correct on every axis that can be measured, and the panel
still disagrees. The untested gap is between "the region is correct" and "those pixels reach the
panel": whether `render_by_line` issues a callback for every line of the region. A line inside the
region that never gets one keeps whatever the panel already had, which is indistinguishable from a
leftover fragment. A firmware check comparing distinct callback lines against the region's row span
was written but never run - that is the next step.

**Two bisections did all the real work** and are worth repeating in any similar hunt: *full
invalidation is clean* (the fault is in narrowing) and *30px of slack changes nothing* (it is not
coverage). Together they eliminated the entire space that seven fixes had been aimed at. When a
narrowing change appears to cause an artifact, first ask whether it is a defect that narrowing
merely **reveals**.

### Superseded: earlier analysis of this artifact

### The arc artifact: the odd pixel at a dirty region's edge

**Root cause (2026-08-15).** Not invalidation, and not the arc rasteriser either. The panel wants
an even x window, so `slint-esp.cpp` widens each span and invents the odd pixel by replicating its
neighbour:

```cpp
row[span_offset - 1] = row[span_offset];   // outside the rendered span
row[span_end]        = row[span_end - 1];
```

That pixel lies **outside the dirty range**, so nothing ever repaints it. Where a region boundary
cuts through drawn content it erases one pixel or extends one, and the error persists until some
later region happens to cover it - reported as gaps at the arc's sweep tip that eventually repair
themselves.

**Why it masqueraded as an invalidation bug for an entire session.** With the whole element dirty
the boundary sits at the element edge, off the arc, so the invented pixel is invisible. Narrowing
puts boundaries *through* the arc. Grid pitch, band shape and slack size were therefore all
irrelevant - the fault is at the *edge*, wherever it falls.

Fixed in the renderer by handing out dirty rects with even horizontal bounds, so it draws every
pixel it hands over and the firmware has no slack to invent. Costs at most two pixels per rect.

**Eliminated by experiment before finding it** - seven attempts, all aimed at the wrong half:
3x3 grid, 7x7 grid, exact swept bands, repaint-debt gating (`arc_debt_marked`), previous-band
carry-forward, band slack at 6px *and* 30px, and the `draw_arc_line` cap clip on the degenerate
rows. Also cleared along the way: the narrow-span clipping and its caller's
`extra_left_clip`/`range_buffer` derivation, occlusion culling (`is_guaranteed_opaque` is false for
arcs), and `DirtyRegion` overflow (merges to a superset, never drops).

**The lesson worth keeping.** Two bisections cornered it and both were worth more than any
hypothesis: *full invalidation is clean* (so the fault is in narrowing) and *30px of slack changes
nothing* (so it is not coverage). Together those eliminated the entire space I had been searching.
When a narrowing change appears to cause an artifact, test whether it is a defect that narrowing
merely **reveals** - and suspect anything that writes outside the range it was handed.

### Superseded: earlier analysis of this artifact

### The arc artifact: a drawing bug, not an invalidation bug

**Root cause found.** On the two rows where the arc's boundary ray is horizontal, `draw_arc_line`
abandons the half-plane clip and tests each pixel against the wedge directly. That loop iterated
only the ring's radial runs and applied the run clip *before* the cap test:

```rust
if px < run.0 || px > run.1 { continue; }   // ring clip
if inside(dx) || cap_covers(dx) { ... }     // cap tested after it
```

A cap pixel lying outside the ring's radial run on that row was therefore unpaintable. Those rows
exist exactly where the sweep tip passes **3 and 9 o'clock**, which matches every report: a nick at
the tip, clustering at 8-10 and 2-4 o'clock, repairing itself when the arc later sweeps back.

Fixed by sweeping the ring runs *and* the cap discs, painting a pixel that is inside a run **and**
the wedge, **or** inside a cap. The run clip now gates only the ring test.

**Why it looked like an invalidation bug for a whole session, which is the lesson worth keeping:**
the missing pixels are invisible while the dirty region is the whole element, because that area is
repainted from the background every frame regardless. Narrowing is what exposes them. That
misdirection consumed four fixes aimed at coverage - 3x3 grid, 7x7 grid, exact swept bands, repaint
debt gating - plus band slack at both 6px and 30px, none of which could have worked.

The bisection that cornered it: full invalidation clean, 30px slack still broken. Coverage size and
shape were both eliminated by experiment before the drawing path was even suspected. **When a
narrowing change appears to cause an artifact, test whether the artifact is a drawing defect that
narrowing merely reveals.**

Eliminated by experiment along the way, for the record: grid pitch (3x3 and 7x7), exact swept bands,
repaint-debt gating (`arc_debt_marked`), band slack at 6px and 30px, `draw_arc_line`'s narrow-span
clipping, the caller's `extra_left_clip`/`range_buffer` derivation, occlusion culling
(`is_guaranteed_opaque` returns false for arcs), and `DirtyRegion` overflow (merges to a superset,
never drops a rect).

### Prior hypotheses (superseded)

Diagnostic switches in the fork: `ARC_NO_NARROWING` in `items/path.rs`, and the phase marks behind
`slint_esp_phase_mark`. `mcu-v1.18.11` remains the clean shipping point; the cap fix is
`mcu-v1.18.28`.

## 11. Corrected cost model (measured 2026-08-15, hardware, remote untouched)

Everything in sections 1-8 modelled the frame as `fixed + k * dirty%`, fitted mostly on menu and
slider frames. That model understates the dial screens badly, because it was never fitted on one.
Measured with `SLINT_PERF_LOG=1` on the pingumote 466x466, sitting on a dial screen with
`ARC_NO_NARROWING = true`, 60-frame averages, device untouched:

| phase | us | scales with dirty area? |
|---|---|---|
| prepare | 21,200 | no |
| render | 31,000 | **yes** |
| copy | 18 | yes |
| wait_transmit | 560 | yes |
| other | 1,740 | partly |
| drawcall | 1,490 (22 calls) | yes |
| **total** | **54,600** | **18.3 fps** |

Companion line: `Dirty [60f]: rects=1.0 avg area=217156 px (100.0%)` - every frame, the whole panel.

Two corrections to the earlier model:

1. **Render, not prepare, dominates this screen.** Section 4 records render at ~10.8ms for a
   100%-dirty frame. That was a *menu* frame. With two dials on screen it is 31ms, because the
   arcs are re-rasterised over all 217k pixels rather than over a sliver. The rasteriser is not
   slow - 143ns/pixel is overdraw across a full frame, and `draw_arc_line` does 2-4 sqrt per row
   and `blend_slice` for span interiors. It is being asked to do ~30x the necessary work.
2. **Narrowing is worth ~28ms/frame on this screen, i.e. 18fps against ~40fps.** Since prepare is
   dirty-independent at 21ms and render is ~31ms at 100% dirty, a 10%-dirty frame costs roughly
   `21 + 3 = 24ms`. Every other optimisation in this document is rounding error against that.
   The 21ms prepare floor caps this screen at ~47fps regardless.

**The arc artifact is therefore not a side quest - it is the whole remaining performance problem.**
`ARC_NO_NARROWING = true` is not a neutral fallback; it costs more than everything sections 5-9
recovered, combined.

### Refuted this session

- **Instruction cache line-size attempt.** The historical attempt described a change from
  32B to 64B and measured 3375ms/60f versus 3367ms/60f. Subsequent inspection found that
  ESP-IDF 5.5's ESP32-S3 Kconfig supports only 16B and 32B instruction-cache lines;
  64B is a data-cache option. That earlier result is not evidence from a valid 64B
  instruction-cache configuration. See section 19.
- **`render_by_line` skipping lines of the dirty region.** Recorded in section 10 as the untested
  next step. Refuted from source: `Scene::recompute_ranges` advances `current_line` past lines
  whose `current_line_ranges` is empty, and a line is only empty when it is outside the region.
  Skipped lines are lines nothing asked to have painted. Correct as written.
- **`DirtyRegion` dropping a rect on overflow.** Refuted from source: `add_box` past `MAX_COUNT = 3`
  picks the cheapest merge and `union`s into it. The region is always a superset, never lossy.

### Live finding: multi-rect regions degrade the firmware chunker

`render_window_frame_by_line` iterates `for r in &scene.current_line_ranges`, so a single scanline
issues **one `process_line` callback per region rect**, all with the same `line_y`. The chunk
accumulator in `slint-esp.cpp` keys on `line_y == chunk_start_y + lines_in_chunk` plus matching x
bounds, so two ranges on one line force a flush between them, and the following line's first range
forces another. With a 3-rect region overlapping in y, chunking degenerates towards one drawcall
per rect per line.

This is a performance cliff, not a correctness bug - every range is still flushed, and the traced
buffer/DMA handoff stays balanced. It does not bite today because full invalidation produces
`rects=1.0`. **It will bite the moment narrowing is re-enabled**, and at ~73us per drawcall it can
claw back a meaningful share of the narrowing win. Fix before re-enabling: accumulate per-rect
chunks independently, or coalesce a line's ranges into one span when the gap between them is
smaller than the drawcall cost.

## 12. The arc artifact: root cause found (2026-08-16)

**The panel requires every draw-window coordinate to be even, and the narrowed region stopped
providing that.** `firmware/components/esp_lcd_sh8601/README.md` states the requirement, CO5300
shares that driver, and the pre-Slint firmware carried an LVGL `rounder_cb` (`display.c:136`,
registered for SH8601 and CO5300 but never GC9A01) that rounded x1/y1 down to even and x2/y2 up
to odd. The Slint flush path never aligned y at all.

It went unnoticed for as long as it did because **full invalidation satisfies the requirement by
accident**: the flush starts at row 0 and advances by `slint_chunk_lines`, which `display.cpp`
forces even, so every window came out even. A narrowed region starts on whatever row the damage
begins, which is odd half the time. That is why full invalidation looked clean, why band slack
made no difference at 6px or 30px, and why every renderer-side fix failed - the renderer was
never wrong.

### How it was finally cornered

Two things that had defeated ten previous hypotheses:

1. **Reproduce on the host.** `api/rs/slint/tests/arc_partial_repaint.rs` renders a dial screen
   twice in lockstep - once through the partial renderer reusing its buffer, once with a full
   repaint - both via `render_by_line` because that is the path the firmware runs, and diffs the
   buffers every frame. Iteration went from a ~10 minute release-and-flash cycle to 4 seconds.
2. **Ask whether the pixel was offered.** The test records every `(line, range)` the renderer
   asks for, so a divergent pixel can be classified: never offered means the invalidation missed
   it, offered means the invalidation was right and the drawing was wrong. It came back
   `offered: true`, which eliminated the entire invalidation-bug family in one measurement.

Across 40 configurations - `Rgb565Pixel` and `Rgb565BigEndianPixel`, all four rotations, a smooth
climb and four stall sizes - the largest difference anywhere is **one step in a 5/6/5 channel**.
The renderer agrees with a full repaint. Since the host cannot reproduce it and the device can,
the fault had to be in the flush, which is what pointed at the window coordinates.

### Why the fix is in the renderer, not the driver

The first attempt rounded the window outward in `slint-esp.cpp` and filled the added row by
replicating its neighbour. That removed the missing segments and immediately produced a new
artifact trailing every area that updated: a replicated row is not what the renderer drew, and at
the edge of a dirty band it lies outside the region, so nothing ever repaints it.

The fix is `even_aligned` in `software/lib.rs`, which rounds the dirty region out to even
coordinates **before the scene is rendered**, so the renderer genuinely paints the added row and
column. It is applied after the rotation, because the alignment the panel needs is on the
coordinates the draw window is expressed in. A previous partial attempt snapped x only, and did
it before the rotation, so on a rotated screen it aligned the wrong axis.

`arc_partial_repaint` now asserts the invariant on both axes and both assertions fail if the
rounding is removed - checked, because five earlier suites passed vacuously on this same bug.

## 13. Where the frame time went (2026-08-16)

`ARC_NO_NARROWING` is back off. Measured on the dial screen, 60-frame averages:

| | before | after |
|---|---|---|
| frame | 54.6ms (18.3 fps) | 21.7-32.4ms (**30.8-46.0 fps**) |
| prepare | 21.2ms | 12.1-14.9ms |
| render | 31.0ms | 7.3-13.4ms |
| drawcalls | 22 / 1.5ms | 11-18 / 1.0-1.6ms |
| dirty | 100% | 7.9-18.8% |

Two firmware changes beyond the invalidation itself:

**Per-rectangle chunk accumulators.** `render_by_line` issues a callback per region rectangle per
line, all with the same `line_y`, so rectangles overlapping vertically interleave. A single
accumulator keyed on "same x range, next line" is broken by every switch: 93 draw calls for 283
callbacks, and windows one line tall whose odd row bounds the panel rejects. One accumulator per
rectangle made every chunk contiguous - **93 draw calls became 14-20, and 6.2ms became 1.6ms** -
and is also what keeps windows even, since a chunk now inherits the region's alignment.

**Pipelined transfers.** `trans_sem` is a counting semaphore and the flush keeps several chunk
transfers outstanding, blocking only when the specific buffer it needs is still being read.

### Refuted, with numbers

- **Merging dirty rectangles that overlap vertically.** The theory was sound - render was 15.5ms
  over 284 callbacks, ~55us each for ~200 pixels, so callback count sets the cost. In practice the
  1.3x growth gate rarely fired where it would have helped: dirty area rose from 20.5% to 29.0%
  while callbacks did not drop (233-298, unchanged), and frames went from 25-38fps to 23-32fps.
  Reverted. **Callback count is the right target; merging whole rectangles is the wrong lever.**
- **Instruction cache line 32B to 64B** (section 11) - no effect, reverted.

### What is left

`prepare` (12-15ms) and `render` (7-13ms) dominate, and `render` is still ~50us per line callback
against ~200 painted pixels, because each callback re-walks the items active on that line and
re-runs the `first_cover` scan. Making that per-callback work cheaper - caching `first_cover` per
line, or hoisting the active-item scan - is the next real lever, and unlike merging it does not
trade away area.

## 14. Subtree pruning blanked the screen, and the coordinate-space bug behind it (2026-08-16)

§4a proposed subtree pruning and it shipped, but as written it **permanently blanked the screen
after pairing completed**. Confirmed by bisecting on hardware: pruning on = black, pruning off =
correct. Arc invalidation narrowing was exonerated in the same bisect.

The defect is a coordinate-space mismatch. `set_subtree_bounds` stored the bounds in the
**parent's** space, because they were unioned from `item_geometry`, which carries the item's own
origin. `subtree_can_paint` then transformed those bounds by the **current** transform. While an
item sits still the two agree, so it looks correct. The moment the item moves - a screen sliding
between grid cells is exactly that - the cached bounds describe where it *was* while the
transform describes where it *is*, and the test is performed at neither position.

What turned a one-frame glitch into a permanent one: pruning returns early **without walking the
subtree**, so the bounds are never recomputed. One bad frame latches the subtree off forever.

Fixed by keeping the bounds in the item's **own** coordinate space and placing them at the
item's current origin when they are tested, which is why `filter_item` is now read *before* the
prune check rather than after. A wrong prune now requires the subtree to need repainting for a
reason the dirty region does not reflect: when a child moves or grows, its old rect lies inside
the cached bounds and inside the dirty region, so the parent is walked and the bounds refresh.

**A host test is not enough here.** `api/rs/slint/tests/conditional_screen_swap.rs` models
exactly this - conditional children destroyed and re-created, sliding, diffed against a full
repaint - and it **passed with the bug present**, both through `render()` and through
`render_by_line`, with and without nested subtrees. The device found it; the test did not. Treat
the host suites as regression guards, not as proof.

### Bisecting this without the CI loop

The Slint library normally comes from a GitHub release, so trying one renderer change meant
commit → tag → CI → bump the tag: about ten minutes a turn, which is why the wrong theory
survived as long as it did. `scripts/use_local_slint.py` builds `libslint_cpp.a` from the
local checkout and drops it over the staged copy, which `firmware/components/slint/CMakeLists.txt`
reuses because it only downloads when the file is missing. **About 77 seconds a turn** - 32s for
Slint, 45s for the firmware.

One trap, and it invalidated a result before it was noticed: **PlatformIO does not relink when
only that archive changes.** It decides the firmware is up to date, skips the build in ~6s, and
the next upload silently reflashes the *previous* binary. Touching a source file does not help.
Deleting `firmware.elf` and `firmware.bin` does, and the script now does it. Verify every bisect
flash by checking that `firmware.elf` is newer than the staged `.a`.

The headers and `slint-compiler` still come from the release, so this is only valid while the C++
API and the .slint language are unchanged - cut a real release for those.


## 15. Pack narrow chunks into the existing DMA allocation (2026-09-27)

Measured on the connected `pingumote_esp32s3_touch_amoled_132`, firmware 0.9.15,
Slint `mcu-v1.19.1`, `TEST_MODE=1`, main stats screen. No clocks, telemetry rates,
UI features, framebuffer format, or DMA allocations changed.

The flush path packed each row at the dirty rectangle's width but always flushed
at the full-screen chunk height (14 rows on this target). Narrow rectangles thus
left most of the 13,048-byte buffer unused. Each accumulator now calculates its
row capacity from that same allocation and its span width, rounded down to an
even number. Full-width chunks retain their existing height. The DMA ownership
waits, draw-window alignment, and rendering callbacks are unchanged.

Both builds used temporary `SLINT_PERF_LOG=1`, with the first two 60-frame batches
excluded. Baseline: 18 batches; candidate: 19 batches. The device was untouched
on the same screen during each capture.

| Metric | Baseline | Packed chunks |
|---|---:|---:|
| Mean render-frame duration | 33.32 ms | 31.47 ms |
| Reciprocal of render-frame duration | 30.01 fps | 31.78 fps |
| Delivered FPS, from serial timestamps between batches | 28.53 | 29.92 |
| Draw calls/frame (mean of logged integer counts) | 18.22 | 6.42 |
| Draw-call CPU time/frame | 1.505 ms | 0.668 ms |
| Dirty area | 16.17% | 15.78% |

The observed delivered-FPS gain is about 4.8%. These are live test-mode samples,
not a deterministic replay; slightly different dirty areas and renderer timings
mean the whole frame-time difference cannot be attributed solely to batching.
The reduction in display transactions is the directly attributable improvement.

Validation: release build succeeded; exhaustive even-width/even-height arithmetic
checks covered square and rectangular dimensions including rotated widths, and
confirmed allocation bounds and even chunk heights. The user checked the stats
screen, menu scrolling, and return navigation and reported normal visuals and
touch response. Captures reported no transfer timeouts. Full-screen timing and
other physical hardware models were not separately benchmarked. Temporary timing
logging was removed for the final firmware. Local capture logs and the original
firmware backup are under `.pio/fps-check/` (untracked).


## 16. Initial further experiments (2026-09-27)

**Superseded in part:** the later controlled investigation fixed the local static-library build.
The arc and scanline optimizations now show measurable gains.
See [§17](#17-stats-screen-fps-results-and-release-2026-09-27) for current recommendations.

Continued on the same device and main stats screen with test mode enabled, using
40-50 second serial captures and excluding the first two 60-frame batches.
Delivered FPS includes scheduling time between frames. The accepted packed-chunk
baseline from section 15 was 29.92 FPS.

| Candidate | Delivered FPS | Result |
|---|---:|---|
| Skip rebuilding unchanged scanline item lists, local renderer with release-style LTO/codegen settings | 27.02 | Slower overall |
| Same scanline change, default local build settings | 25.88 | Slower overall |
| Above plus conservative rejection of arc spans inside the hollow center | 29.85 | No net gain over the shipped renderer |
| Shipped renderer, enable switch tables only in generated UI C++ | 30.05 | Within live-run variation |
| Above plus `-Os` for generated UI C++ | 29.62 | Smaller image, no FPS gain |

All candidates were rejected at this stage. The scanline and arc changes passed 25 software
renderer unit tests and both climbing/stalling arc partial-vs-full pixel tests.
The broader `partial_renderer` integration suite could not link because its Skia
library requires MSVC symbols unavailable in this environment. Host test builds
also exhausted disk space; stale rebuildable `.rlib`/`.rmeta` files in the verified
Slint `target/release/deps` directory were removed before retrying.

The local renderer builds increased scene preparation time even though that code
was unchanged, so these measurements do not isolate the scanline change's own
cost. The empty-center check reduced rasterization time, but that benefit did not
outweigh the local build's preparation cost. A future investigation should compare
against an unmodified renderer built with the identical toolchain before drawing
conclusions about that optimization alone.

Switch-table flags were verified in `compile_commands.json` on exactly the eight
generated UI files, with no IRAM functions among them. This scoped use follows
[Espressif's memory guidance](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32s3/api-guides/memory-types.html).
The `-Os` variant saved about 271 KB of flash and left static RAM unchanged, but
was rejected because the task requires an FPS improvement without regressions.

Restored the original renderer archive, generated-UI compiler options, and local
Slint source files; removed temporary perf logging. The exact backed-up firmware
from section 15 was flashed back to the device, keeping test mode enabled. The
accepted chunk-packing improvement remains. Logs, summaries, and an experimental
renderer patch are retained locally under `.pio/fps-check/` (untracked).


## 17. Stats-screen FPS results and release (2026-09-27)

### Result

The installed published-release build delivered **34.97 FPS over 90.13 seconds**, closely matching the 34.91 FPS local candidate.
That is an observed 22.6% gain over the original profiled baseline; it is not a guaranteed 35 FPS minimum.
The figures below retain the local-build experiments to isolate the changes.

The locally built candidate delivered **34.91 FPS over 90.17 seconds with continuous profiling disabled**.
The original profiled baseline delivered **28.53 FPS**; the observed end-to-end improvement is **22.4%**.
The best comparable profiled candidate delivered **34.57 FPS**, a **21.2%** improvement over that baseline.
The target was 35 delivered FPS: this is close, but neither run establishes a sustained 35 FPS minimum.
The final counter increased from 269 to 3,417 frames between device timestamps 13,882,201 and 104,047,731 microseconds.
The baseline and final measurements use different instrumentation, so the profiled comparison is the cleaner estimate of optimization benefit.
The investigation initially left changes uncommitted.
At the user's subsequent request, the Slint changes were committed as `a9970a043d1bf7e843d9af1b85595467954fc3ca` and tagged `mcu-v1.19.2`.
PubRemote now selects that published-release tag.
All release jobs passed, and the normal PlatformIO download and firmware build succeeded.

Measurements used the connected Pingumote ESP32-S3 AMOLED 1.32-inch remote, firmware 0.9.15, on the main stats screen with `TEST_MODE=1`.
Resolution remains 466×466, RGB565BE, with the existing 80 MHz QSPI clock.
No telemetry updates, animations, visual quality, input features, task priorities, or watchdog behavior were removed or reduced.

### What actually helped

| Change | Before → after delivered FPS | Recommendation |
|---|---:|---|
| Pack narrow dirty rows into the full existing DMA buffer capacity | 28.53 → 29.92 | Keep |
| Reject arc spans wholly inside the empty dial center | 27.18 → 30.86, controlled local builds | Keep |
| Restore original generated C++ optimization after detecting stale cached flags | 30.86 → 32.43 | Keep original `-O2` settings |
| Reuse active scanline item membership until an item starts or ends | 32.43 → 33.89 | Keep |
| Cache opaque-cover decisions until membership or dirty x-ranges change | 33.89 → 34.57 | Keep, with the included pixel tests |

The rows are sequential experiments with different baselines; their gains must not be added together.
The arc comparison used identical local Rust settings and identical cached C++ flags on both sides.
The later scanline and cover comparisons used the original C++ flags.

DMA packing reduced logged draw calls from 18.22 to 6.42 per frame and draw-call CPU time from 1.505 to 0.668 ms.
It uses the same allocations, pixel data, transfer waits, and panel alignment.
Narrow rectangles can occupy more rows per transfer without exceeding the existing byte capacity.

The arc check rejects only spans safely inside the hole and outside both round-cap bounds.
A one-pixel margin preserves fractional coverage and antialiasing.
It avoids square roots and later rasterization work for these invisible spans.

Scanline reuse retains the existing item order until the next item's top or bottom boundary.
Dirty-region skips still update membership when necessary.
Opaque-cover caching also expires at dirty-range boundaries and resets every frame.
Neither optimization introduces a heap cache or another framebuffer.

### Controlled measurements

FPS is calculated from actual frame batches divided by elapsed device uptime.
The reciprocal of CPU frame time excludes scheduling gaps and overstates delivered FPS.
The first two 60-frame batches were excluded from each roughly 60-second capture.

| Local renderer variant | Retained batches | Mean frame work | Delivered FPS |
|---|---:|---:|---:|
| Unmodified renderer, corrected static-only build, cached C++ `-Os` flags | 22 | 34.826 ms | 27.18 |
| Empty-center rejection, otherwise identical | 25 | 30.432 ms | 30.86 |
| Above with original C++ `-O2` flags restored | 26 | 28.858 ms | 32.43 |
| Above with Rust core optimization level 3 | 26 | 29.119 ms | 32.24 |
| Arc rejection plus scanline membership reuse | 27 | 27.538 ms | 33.89 |
| Above plus opaque-cover cache | 28 | 26.949 ms | 34.57 |
| Above plus angular-wedge rejection | 27 | 28.268 ms | 33.03 |

These are live test-mode samples, not deterministic replay or randomized repeated trials.
Changing frame cadence also changes sampled animation values and dirty regions.
The small cover-cache gain has less certainty than the larger arc and transfer gains.
The measurements establish the best observed combination, not a guarantee of 35 FPS on every screen or frame.

### Rejected experiments and build discoveries

- Angular-wedge rejection passed correctness tests but reduced measured FPS to 33.03; removed.
- Rust core optimization level 3 added about 59 KB of flash and was slightly slower; removed.
- Generated C++ switch tables measured 30.05 FPS against the earlier 29.92 baseline, within live-run variation; removed.
- Adding generated C++ `-Os` measured 29.62 FPS and saved about 271 KB of flash; removed because it did not improve FPS.
- Early local renderer builds measured 25.88–29.85 FPS and initially obscured useful renderer changes.

The local helper previously emitted multiple Rust library formats with `cargo build`.
Matching LTO flags alone did not reproduce the release's static-only optimization.
The helper now uses `cargo rustc --crate-type staticlib`, fat LTO, one codegen unit, and disabled incremental compilation.
This matches the release archive's two-object structure, though local toolchain output is not asserted to be byte-identical to the release.

Restoring a CMake file with its old timestamp initially left stale `-Os` flags in generated compile commands.
CMake was explicitly regenerated, and effective generated UI commands were checked for the original `-O2` flags.
Changing the staged archive also requires forcing a relink; the helper removes only the generated firmware ELF and BIN for this purpose.

### Validation and limits

- Release firmware builds succeeded and flashed application images passed the uploader's data-hash verification.
- All 26 software-renderer unit tests passed for the retained changes.
- Both climbing and stalling arc partial-repaint comparisons passed.
- A new independent line-rendering versus full-frame-rendering test passed across all four rotations and moving, clipped, translucent, and conditional rectangles.
- Deterministic randomized scene tests checked active membership against geometry across skipped dirty bands.
- Fractional arc tests checked more than 100,000 culled spans against full-width rendering, including round caps and translucent colors.
- DMA capacity arithmetic was checked across even dimensions and rotations.
- Rust formatting checks passed.
- The user confirmed normal visuals, menu scrolling, return navigation, and touch after the first DMA improvement.
- A final physical check of the additional renderer changes was requested after the final measurement; response pending.

The broad `partial_renderer` integration suite could not link because its Skia library needs MSVC symbols unavailable locally.
The targeted independent coverage test and arc tests did run successfully.
An exploratory full-versus-clipped fractional arc test exposed an existing one-step RGB565 rounding discrepancy, also reproduced with culling disabled.
The retained regression test validates culling decisions directly and does not hide a newly introduced pixel difference.

Other hardware models, battery endurance, real radio traffic, and every settings/sleep transition were not benchmarked.
No observed functional regression is not a proof that all possible behavior is unchanged.

### Implications and files to keep

Keep the DMA packing in `firmware/components/slint/src/slint-esp.cpp` and the `mcu-v1.19.2` pin in `platformio.ini`.
The three Slint renderer changes and their tests are committed on `feat/mcu-minimal` in `techfoundrynz/slint`.
The standalone patch was removed after committing; the tagged source is the source of truth.
The corrected `scripts/use_local_slint.py` remains available for future local renderer experiments.

No panic, error, or transfer-timeout messages were found in the final candidate captures.

The locally built candidate uses 107,428 bytes of static RAM and 5,917,023 bytes of application flash according to PlatformIO.
This is static allocation reporting, not peak runtime heap usage.
DMA allocation sizes are unchanged.
There is a small amount of additional bookkeeping: accumulator capacities, one scene boundary, and three optional cover indices on the rendering stack.

A read-only `render_stats` console command is available only in test mode.
It reads the existing frame counter and microsecond uptime; it adds no continuous frame-loop sampling or screen overlay.
Its command registration has a small test-mode code and memory cost.
It can be retained for repeatable measurements or removed separately when the investigation is finished.
Continuous `SLINT_PERF_LOG` and the FPS overlay are disabled in the final build.
The user's original test-mode setting remains enabled.

The initial measurement used a locally staged renderer.
The subsequent `mcu-v1.19.2` release contains these changes, and `platformio.ini` selects that tag.
Normal builds now download its library, headers, and compiler without the local Slint checkout.
The local helper remains an optional development tool; it is not part of the normal build.

### Reproduce or revert

Normal builds use the published release selected by `platformio.ini`:

```powershell
& C:/Users/slims/.platformio/penv/Scripts/python.exe -m platformio run -e pingumote_esp32s3_touch_amoled_132
& C:/Users/slims/.platformio/penv/Scripts/python.exe scripts/sync_slint_lsp.py
```

No local Slint checkout or Rust toolchain is required for that path.
For renderer development, use the source at `mcu-v1.19.2` and explicitly run `scripts/use_local_slint.py` before the firmware build.
The earlier local measurements used `+esp`, rustc 1.95.0-nightly, LLVM 21.1.3.
The local helper expects `C:/Repos/slint` and the staged release headers/compiler.
It is not invoked by a normal firmware build.

For normal-operation FPS, send `render_stats` twice, with the screen unchanged, and calculate:
`(frames_after - frames_before) * 1,000,000 / (time_us_after - time_us_before)`.
A frame-counter reading is approximate to one in-flight frame at each endpoint.
Long intervals make that uncertainty negligible.

Local backups and evidence are under `.pio/fps-check/` and are not committed:

- `original.bin`: original firmware backup.
- `packed-final.bin`: previously user-validated DMA-only firmware.
- `cover-final.bin`: best renderer combination with continuous profiling disabled.
- `release-renderer.a`: shipped renderer archive, available for rollback and relinking.
- `controlled-results.json`, the named serial logs, build logs, and test logs: measurement evidence.

Flashing only the application at offset `0x10000` preserves settings and filesystem partitions.
To revert only the renderer, restore the released archive and force a firmware relink, keeping the DMA packing if desired.
The staged release archive SHA-256 is `E3A53D3AEF6AE2F72F8847277DCE46B32C6C7CC934259AEBA77EE7D17AEE39B1`.

Locally built candidate firmware SHA-256: `ED7D3CCE2F519ACFA333199F9DC2AC53CD8A90F8828DBE8BF2EC3428585DE63D`.
Locally built candidate renderer archive SHA-256: `AABCF7300510F1E785D945AF19C1F6F0B9C2E07D0F01B2BC1E800B0530B58B74`.

### Published-release switchover

At the user's request, the retained Slint implementation and tests were committed and pushed to `techfoundrynz/slint`, branch `feat/mcu-minimal`.
Commit: [`a9970a043`](https://github.com/techfoundrynz/slint/commit/a9970a043d1bf7e843d9af1b85595467954fc3ca).
Release tag: [`mcu-v1.19.2`](https://github.com/techfoundrynz/slint/releases/tag/mcu-v1.19.2).
The package's internal Slint version remains `1.19.0`; the MCU release tag and crate version are separate.
PubRemote's release pin is changed in `platformio.ini`; PubRemote changes remain uncommitted.
The unrelated local Android `.gradle` directory was excluded from the Slint commit.

The ESP32 CI package was built successfully, with library SHA-256 `9b3f779889a038678172ec38bbb831c44e7167e45c6729bea6e67d31732bde4b`.
It differs from the earlier local build, so the previous FPS measurement is not automatically attributed to the published package.
All four build jobs and the release publication job passed.
The normal PlatformIO build downloaded and staged the release; `.slint-source` names `techfoundrynz/slint@mcu-v1.19.2`.
The staged library hash matches the CI package above and differs from the previous local override.
The firmware build passed with 107,428 bytes static RAM and 5,932,011 bytes application flash.
The editor language server was downloaded from the same release and configured in `.vscode/settings.json`.
Reload the VS Code window to activate it; the old binary remains while the existing editor process holds it open.
The application was flashed successfully with uploader hash verification, preserving settings and filesystem partitions.
With profiling disabled, the frame counter increased from 274 to 3,426 between uptime readings 13,870,105 and 104,003,253 microseconds.
That is **34.9705 delivered FPS**, closely matching the earlier local build; this single live run does not establish a meaningful additional speedup.
No panic, error, assertion-failure, or transfer-timeout messages appeared in the capture.
The device remains in test mode with the published-release firmware installed.
Firmware SHA-256: `EF22CD81F29BD8564CBFFCFEE61D60E9939305285E5827B83F2866CBAD52DCD1`.
Evidence: `.pio/fps-check/published-build.log`, `published-lsp-sync.log`, `published-upload.log`, `published-counter.log`, and `published-final.bin`.

## 18. Execute instructions from PSRAM (2026-09-27)

The next investigation uses firmware 0.9.18 and the same published `mcu-v1.19.2`
renderer. Both sides of each FPS comparison use `TEST_MODE=1`, with the stats
screen untouched, profiling disabled, and delivered FPS calculated from
`render_stats` frame and uptime deltas. The normal-use build is separate from
these test images.

The candidate enables `CONFIG_SPIRAM_FETCH_INSTRUCTIONS` only for
`pingumote_esp32s3_touch_amoled_132`. ESP-IDF copies the instruction section from
flash to octal PSRAM at startup. Read-only data remains in flash. This addresses
the instruction-fetch bottleneck described in section 3 without changing the
renderer, image quality, dirty regions, update rates, animations, input handling,
task priorities, or hardware clocks.

The renderer archive remains byte-identical to the published release:
`9b3f779889a038678172ec38bbb831c44e7167e45c6729bea6e67d31732bde4b`.
The first 90-second comparison measured **34.429 FPS** from flash and
**54.622 FPS** from PSRAM. This is a live animation workload, not deterministic
frame replay; repeated measurements and physical checks are recorded below.

The cost is 2,752,512 bytes (42 MMU pages) of PSRAM reserved for instructions.
The candidate's first capture reported 5,555,444 bytes free near its start and
5,554,544 bytes near its end, with a 5,505,024-byte largest free block at both
points. Game limits remain 256 KiB for Lua and another 256 KiB for textures;
renderer and other runtime allocations are additional.

The test-only `render_stats` command now also prints heap information on demand.
It adds no per-frame instrumentation and is absent with `TEST_MODE=0`.
A full 16 MiB device backup is stored locally at
`.pio/fps-check/next-device-backup.bin`. Test flashing writes only the active
application at `0x10000`, preserving settings, OTA selection, and filesystem data.

### Repeated results and validation

| Image | Sample duration | Delivered FPS |
|---|---:|---:|
| Flash instructions, first run | 90.069 s | 34.429 |
| PSRAM instructions, first run | 90.074 s | 54.622 |
| Flash instructions, repeat | 60.040 s | 36.259 |
| PSRAM instructions, repeat | 120.046 s | 57.345 |

All four measurements use `TEST_MODE=1`. Each image was rebooted before its
capture, and the frame counter was sampled after warmup. The paired increases
are 20.19 and 21.09 FPS. Even the lower candidate result exceeds the higher
baseline by 18.36 FPS. Variation within each configuration means these are
observed averages, not a minimum-FPS guarantee or a precise prediction for real
radio traffic and other screens.

Both candidate runs ended with 5,554,544 bytes of free PSRAM. Endpoint internal
free heap was 7,387 and 7,271 bytes; the repeated baseline ended at 7,447 bytes.
The candidate's minimum-ever internal free heap was 5,539 and 5,635 bytes, versus
6,339 bytes for the repeated baseline. These are sampled heap observations, not
proof of peak memory behavior in every feature combination.

All four captures had no logged panic, assertion failure, error-level message,
or transfer timeout. Test and normal-mode firmware builds succeeded, and all
application flashes passed the uploader's hash verification. No renderer code
or pixel arithmetic changed, so no new rasterizer test was needed for this
configuration change. The user reported that the requested animated-display,
scrolling/navigation, touch, sleep/wake, and available-game checks looked and
worked normally. Battery endurance, other hardware models, and exhaustive
radio/OTA scenarios were not measured.

Keep `CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y` in this board's sdkconfig. There is no
new Slint release or local renderer override. Normal-use source has
`TEST_MODE=0`; the saved test binaries used for every FPS measurement have it
enabled. Reverting this optimization requires clearing that one sdkconfig
option and rebuilding/flashing the application.

Evidence is local and untracked under `.pio/fps-check/`: `next-results.json`,
`next-*-counter.log`, `next-*-build.log`, `next-*-upload.log`, and the three
comparison/normal images. The test baseline SHA-256 is
`9e6fcc0a4932f8d386837495ba1be6d38ab06808db45b367f14420bd929d2f4e`;
the test candidate is
`b307c83a4cc7655d175ae8366286ed530b8a8fde3334fea2951a4f036d2450fa`;
the normal candidate is
`4f82ba5c6405a9eed105fc628be99a8c5f026597e62badfadcde75553292cf5e`.

The normal candidate is installed, with uploader hash verification. A subsequent
serial check reported firmware 0.9.18, hardware
`pingumote_esp32s3_touch_amoled_132`, and build ID `c125468d`. The test-only
`render_stats` command was absent, confirming `TEST_MODE=0`. The capture had no
runtime errors; see `next-normal-boot.log`. The final normal-mode image was not
used to claim a separate FPS measurement.

## 19. Follow-up memory and DMA comparisons (2026-09-27)

Baseline: commit `6739691`, firmware 0.9.18, published Slint `mcu-v1.19.2`,
instructions executing from PSRAM. Each candidate changes one thing relative
to that baseline. Every FPS test uses `TEST_MODE=1`, an untouched stats screen,
the existing 60 FPS target, and a fresh reboot. Counter readings start around
20 seconds of device uptime and span 90 seconds. Settings and filesystem
partitions are preserved; only the application at `0x10000` is flashed.

### Candidates and repeated measurements

The proposed 64-byte instruction-cache line is **unsupported**, not a benchmark
candidate. ESP-IDF 5.5's
`components/esp_system/port/soc/esp32s3/Kconfig.cache` exposes only 16-byte and
32-byte instruction-cache lines. This board already uses 32 bytes. The earlier
suggestion confused this with the data-cache options; section 11 is corrected.

The viable candidates were measured with identical `SLINT_PERF_LOG=1`
instrumentation, so frame work and transfer waits could be compared alongside
delivered FPS. The FPS cap can hide work savings, but the frame-work measurement
excludes its pacing delay. Rows below are individual live runs, not guarantees
of minimum FPS or deterministic frame replay.

| Variant | Delivered FPS | Mean frame work | Mean DMA wait |
|---|---:|---:|---:|
| Baseline | 56.934 | 14.969 ms | 0.761 ms |
| Read-only data in PSRAM | 57.024 | 14.938 ms | 0.763 ms |
| Earlier DMA submission | 57.982 | 14.504 ms | 0.338 ms |
| Baseline repeat | 57.002 | 14.922 ms | 0.765 ms |
| Read-only data repeat | 57.153 | 14.887 ms | 0.770 ms |
| Earlier DMA repeat | 57.931 | 14.428 ms | 0.320 ms |

**Read-only data: reject.** The average difference is only about 0.12 FPS and
0.033 ms of frame work, with no convincing practical benefit for the memory
cost. Free PSRAM fell from 5,561,624 to 2,524,776 bytes at the final samples.
Boot logs confirmed that read-only data really was copied into PSRAM; this was
not a configuration no-op. Leave `CONFIG_SPIRAM_RODATA` disabled.

**Earlier DMA submission: retain.** Across
the paired profiled runs, delivered FPS increased by 1.05 and 0.93. Average
frame work fell about 0.48 ms (3.2%), of which about 0.43 ms was reduced transfer
waiting. Both configurations retained the same free PSRAM at the measured
endpoints. The gain is modest because most frame work is still preparation and
rasterization, and delivered FPS is approaching the configured target.

The scheduling change starts a full buffer immediately when no transfer is
outstanding. It also submits an accumulated region once the scanline has passed
its end, instead of retaining it until buffer pressure or the end of the frame.
The same allocations, pixel data, buffer capacities, ownership checks, completion
semaphore, bounded waits, and frame-level panel lock remain in place.

### Correctness checks

All six profiled captures had no logged panic, assertion failure, error-level
message, failed bitmap transfer, or transfer timeout. Builds and application
upload hash verification succeeded for each measured image.

`tests/dma` extracts the actual production accumulator into a C++20 host harness.
It checks 32,000 cases against independently constructed expected pixels and
write counts, while varying dirty rectangles, gaps, byte swapping, and transfer
completion timing. It also checks allocation/display bounds, DMA ownership, and
even address windows for the asynchronous AMOLED path. The retained test passed.
See `tests/dma/README.md` for standalone CMake/CTest commands.

A separate comparison ran the original implementation through the same pixel
and ownership checks. Both passed those checks. The comparison also found 80
asynchronous alignment-failure cases in the original implementation and none
in the candidate, with no new alignment failures across either transfer mode.
The candidate still had 48 synchronous-mode alignment cases inherited from the
baseline. The connected AMOLED uses asynchronous DMA. These are host-model
observations, not proof about every panel or interrupt schedule.

The initial test generator incorrectly supplied more than Slint's maximum of
three dirty rectangles. It was corrected to generate unions of at most three
even-aligned rectangles before the reported 32,000-case comparison; no pixel,
ownership, or asynchronous-alignment assertion was weakened in the retained test.

Evidence and saved images are in `.pio/fps-matrix/`, including the per-run
`*-result.json`, `*-serial.log`, build/upload logs, image hashes, and the original
versus candidate host-test logs. The test helper is local and untracked.
Windows file locks interrupted several managed-dependency refreshes; the exact
versions from `dependencies.lock` were restored from the local package cache,
and generated board-specific CMake metadata was regenerated. No dependency
version or renderer archive was changed for these experiments.

### Profiling-disabled confirmation

A final matched pair kept `TEST_MODE=1` and disabled `SLINT_PERF_LOG` on both
sides. The baseline delivered **57.269 FPS** over 90.084 seconds; the DMA
candidate delivered **57.979 FPS** over 90.085 seconds. This confirms a smaller
**0.710 FPS** delivered gain without continuous diagnostic logging. Both ended
with 5,627,160 bytes of free PSRAM. Neither capture contained runtime or transfer
errors. The configured 60 FPS target was unchanged throughout all eight runs.

The normal-use image has `TEST_MODE=0` and `SLINT_PERF_LOG=0`, with read-only data
still in flash and only the DMA scheduling change retained. It built successfully
at 107,640 bytes of static RAM and 5,933,211 bytes of application flash. These are
build-size reports, not peak runtime memory measurements. Its SHA-256 is
`8ecb522b72a6129795a7bfbec0e8c67628cf01ef970ea1a5e79f0932a9f2ba51`.
The saved image is `.pio/fps-matrix/dma-normal.bin`; normal operation is not the
source of the FPS measurements above.

The user confirmed that the final test-mode DMA candidate looked and worked
normally after checking animation, navigation, touch, and sleep/wake. The
normal-use image was then flashed to the connected remote with upload hash
verification. Serial verification reported version `0.9.18`, build ID `c125468d`,
and confirmed the test-only `render_stats` command was absent, with no captured
runtime errors. The final upload and serial evidence are
`.pio/fps-matrix/dma-normal-upload.log` and `dma-normal-boot.log`.
