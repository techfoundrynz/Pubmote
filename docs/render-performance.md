# Render performance: state, findings, and what to do next

Target hardware: `pingumote_esp32s3_touch_amoled_132` — ESP32-S3 @ 240 MHz, 466×466 CO5300 AMOLED
over QSPI @ 80 MHz, 8 MB octal PSRAM, Slint software renderer via `render_by_line`.

All numbers below were measured on hardware over serial (`SLINT_PERF_LOG=1`), device untouched,
2026-08-15.

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
