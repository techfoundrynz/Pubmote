# Installable games

Tetris, Flappy Penguin, and Whack-a-Baguette now live in `fs/games/*.lua`.
The old per-game C++ handlers and Slint screens have been removed. Firmware
provides one Lua host and a generic Slint drawing surface; installed packages
supply gameplay and visuals. The Games menu discovers files in `/games` inside LittleFS (`/littlefs/games` in firmware).

## Develop on a PC

Install Python 3.11 or newer with Tk support (included with the standard Windows
Python installer), then run from the repository root:

```sh
python -m pip install -r fs/games/requirements-dev.txt
python scripts/play_game.py whack
python scripts/play_game.py tetris
python scripts/play_game.py flappy
```

You can also pass a Lua file path and `--size 600`. Saving the file automatically
reloads it. The preview shows Lua memory and drawing-command counts. R restarts,
P pauses, and Escape closes the window. High scores survive reloads within that
window but are not persisted to disk.

- Enter or Space sends the action button; arrows send joystick direction events.
- Click or drag on the game to simulate touch down/up and swipes.
- In Tetris, arrows move/rotate/soft drop. Swipe down in the middle to hard drop,
  swipe up to hold, or tap the middle to rotate.
- In Whack, enable the analog stick checkbox, use the slider to select a hole,
  then press Space to swing, matching the original stick/button controls.
- Flappy uses an action press, direction press, or touch to flap.

The desktop renderer uses Pillow, the original fonts, and packaged scenery; it is not
an ESP32 emulator or a pixel-perfect display simulator. Sound and haptics appear
as status text. The desktop Lua 5.4 runtime uses wider numeric types than the
firmware's Lua 5.5 component (32-bit mode). Hardware timing, frame rate, touch,
flash writes, and memory outside Lua still require device testing. Use trusted
local packages; this preview is not a security boundary for downloaded code.

Run the automated behavior and preview-host tests without a GUI:

```sh
python -m unittest discover -s tests -p "test_games.py"
```

The Game tests workflow runs these checks for relevant PRs and master changes.

### Package artwork

Whack's original SVG scenery is packaged as losslessly compressed native-size
texture patches for each supported display. Regenerate them after artwork edits
with `python scripts/pack_game_assets.py` using Slint viewer **1.18.1** (`SLINT_VIEWER` points to the executable).
The SVGs are development assets; scenery geometry comes from pinned Git commit
`b6074accb7b432f6a82711d7bfc8f5389e855d76` and is generated under `.pio/` during export. Only the standalone
Lua package needs installing.

## Install on the remote

For initial installs and distribution, build one image containing the games:

```sh
python scripts/install_game.py --build-fs .pio/littlefs.bin fs/games/tetris.lua fs/games/flappy.lua fs/games/whack.lua
```

This uses PlatformIO's `mklittlefs`, or a tool supplied with `--mklittlefs PATH`.
The image matches `firmware/partitions_16MB.csv`: 2 MiB at offset `0xDF0000`.
Flash it with esptool after installing compatible game-host firmware:

```sh
python -m pip install esptool==4.9.0
python -m esptool --chip esp32s3 --port COM5 write_flash 0xDF0000 .pio/littlefs.bin
```

Writing this image replaces the entire LittleFS partition. Back up existing
content first; firmware, pairing/settings and OTA partitions are separate.
Build outputs belong under the ignored `.pio/` directory, not in commits.

### Experimental per-game USB updates

The console uploader currently has an unresolved chunk-transfer failure on the
device. Use the filesystem-image route above for releases until that is fixed.

Flash firmware that includes the game host once. Subsequent game updates do not
need a firmware rebuild. Close any serial monitor before running the installer:

```sh
python scripts/install_game.py --port COM5 fs/games/tetris.lua fs/games/flappy.lua fs/games/whack.lua
```

A previously unused LittleFS partition needs explicit formatting. `--format`
**erases the entire LittleFS partition**, including any non-game files:

```sh
python scripts/install_game.py --port COM5 --format fs/games/tetris.lua fs/games/flappy.lua fs/games/whack.lua
python scripts/install_game.py --port COM5 --remove whack
```

Reopen the Games menu after installation to refresh the list. Firmware never
formats automatically on mount failure. The existing 16 MiB partition layout
reserves 2 MiB for LittleFS, separate from both OTA application slots.

Uploads use bounded chunks, sequential offsets, a declared size, and SHA-256.
The installer stages a file, checks its hash and metadata, then renames it over
the installed version. Incomplete uploads do not replace the installed game.
A running game has already loaded its source, so updates take effect on its next
launch. Removal retains the device's saved high score. Packages are currently
installed over USB; an Internet catalog, download UI, and package signatures
are not implemented. The hash checks upload integrity, not publisher identity.

## Package API version 1

A package is a UTF-8 Lua source file, at most 128 KiB, with this first line:

```lua
-- pubmote-game {"api":1,"id":"example","title":"Example","version":"1.0.0"}
```

IDs contain 1–23 lowercase letters, digits, underscores, or hyphens. The title
and version allow at most 47 and 23 UTF-8 bytes. The menu displays up to 12 games.

Two optional header fields opt into host capabilities and a faster update:

```lua
-- pubmote-game {"api":1,"id":"motion_demo","title":"Motion Demo","version":"1.0.0","needs":["imu","warp"],"rate":60}
```

- `needs`: `"imu"` adds `game.imu` and samples the IMU at 100 Hz while the game
  runs; `"warp"` adds `game.warp`. Firmware that does not know a listed
  capability hides the package instead of starting it. Older firmware ignores
  the field, so such packages fail there with a missing-function error.
- `rate`: `30` (default) or `60` update callbacks per second.
Packages implement these global functions:

```lua
function init(best_score) end
function update(dt, joystick_x, joystick_y, joystick_supported) end
function event(kind, x, y) end
function draw() end
```

`update` and `event` may return `false` when nothing visible changed, allowing
the host to skip drawing. Returning nothing preserves the default redraw behaviour.
The host collects startup garbage after `init`; packages can release temporary
asset data there after loading textures.

Events are action=0, up=1, down=2, left=3, right=4, touch-down=5, touch-up=6,
touch-move=7.
Touch positions and drawings use coordinates 0–100 across the whole display.
Packages position a firmware-owned Exit button with `game.exit`. Update runs
about 30 (or, with `"rate":60`, 60) times per second. Elapsed time is integer
milliseconds; negative gaps or gaps over 250 ms use one period (33 or 16 ms).

Host functions:

```lua
game.rect(x, y, width, height, rgb, radius, border_width, border_rgb, alpha)
game.text(x, y, width, text, rgb, font_size, height, monospace, bold)
game.tone(hertz, milliseconds)
game.sequence({{hertz, milliseconds}, ...}, repeat_sequence)
game.haptic(pattern)
game.save_score(best_score)
game.repeat_input(delay_ms, interval_ms) -- configure in init; 0 interval disables repeat
game.random(limit) -- integer 0..limit-1
game.screen() -- width, height, square, joystick_supported, button_supported
game.texture(id, width, height, base64_zlib_rgba)
game.sprite(id, x, y, width, height, optional_tint)
game.column(x, y, width, height, spacing, bottom_padding, rows)
game.exit(x, y, width, height)
-- With "needs":["imu"]:
game.imu() -- ax, ay, az (g), gx, gy, gz (deg/s), valid
-- With "needs":["warp"]:
game.warp.load(asset, width, height) -- true, or false and a message
game.warp.rig(rig)
game.warp.set(region, dx, dy, squash)
game.warp.draw(x, y) -- in draw(), at most once per frame
```

Optional rectangle arguments default to zero, except alpha (255). Text defaults
to an 8-unit-high centered box; height zero uses intrinsic height and width zero
uses intrinsic width with left alignment. Font size uses a 240-pixel base display.
Column rows are `{text, font, rgb, mono, bold, height, cells, cell_size, wrap}`;
optional cells form a 4×4 colour grid for previews. Columns allow 16 rows.
Sequences allow 128 notes. Input repeat applies to down/left/right; up remains
one event per press.

### IMU

`game.imu()` returns the mean of the calibrated samples since the previous call
(the latest one if none arrived), aligned with the display: the screen rotation
is applied to acceleration and to the x/y angular rates. Acceleration is the
specific force the sensor reports, so a remote at rest reads about 1 g.
`valid` is false when no sample is under 250 ms old or pocket mode is on;
games should then ignore the values. Calling it once per update sees every jolt,
not only the newest sample.

### Warp layer

A warp layer shows one RGB565 image from the package's assets and deforms it
smoothly, natively, at up to 60 frames per second. The package describes soft
regions and pins once per image (`game.warp.rig`) and moves each region every
frame (`game.warp.set`); the firmware turns that into a continuous 8 px mesh,
keeps it from folding, and redraws only the cells that moved, on both cores.
While nothing the package draws after the layer (nor the Exit button) overlaps
it and the screen is not sliding, the layer goes straight to the panel; otherwise
it is shown through Slint tiles at about 30 Hz. The per-pixel work stays in
firmware; a Lua warp would need about a million instructions per frame.

- `load(asset, width, height)` inflates `/games/<id>/<asset>`: a zlib stream of
  width × height little-endian RGB565 pixels in row-major order. Encode each
  pixel as `(red >> 3) << 11 | (green >> 2) << 5 | (blue >> 3)`, write the low
  byte first, and zlib-compress the complete pixel buffer.
  Width and height are multiples of 8, at most 352. Loading replaces the image,
  clears the rig and makes the layer still. Its decoding time is not billed to
  the callback budget. It returns `false` and a message for a missing or
  invalid asset, or when memory runs out; the layer is then gone.
- `rig(table)` defines the deformation, in image pixels:

  ```lua
  {
    edge = 40, -- weights fade to 0 over this many px toward the image border
    pins = {   -- up to 8 still shapes
      {left, top, right, bottom},              -- a box
      {rows = {{y, left, right}, ...}},        -- 2-6 rows going down, open below
    },
    regions = { -- up to 8; the index is the one set() uses
      {patch = {x, y, rx, ry},  -- ellipse: moves as a unit to r = 0.5, stretches to r = 1.7
       group = 1,               -- overlapping regions of one group share weight (1-8)
       anchor = {0.3, -1.1, 0.1}, -- weight rises from 0.3 at y + ry * -1.1 to 1 at y + ry * 0.1
       pins = {1, 2}, feather = 24, -- held by these pins, fading in over 24 px
       travel = 1,              -- share of the fold-free travel budget (default 1)
       squash = 0.07},          -- limit of the area-preserving squash (default 0)
      {blobs = {{x, y, rx, ry}, ...}, -- up to 4 ellipses, combined by max
       clear = {1},             -- keeps a still ring around group 1's patches
       under = 1,               -- yields to region 1 where both reach
       travel = {8, 7}},        -- soft x/y travel limits in px (default 0: still)
    },
  }
  ```

- `set(region, dx, dy, squash)` sets a region's motion for the coming frames in
  source pixels (squash is a fraction; > 0 stretches vertically). Values are
  soft-limited as the rig says; the whole field is then scaled down if any mesh
  edge would stretch or shear by more than 0.45 of a cell.
- `draw(x, y)` places the layer at display coordinates (rounded to even pixels)
  at the image's native pixel size. Commands before it are underneath; any
  later command overlapping it switches it to the Slint path for that time.

A loaded layer uses the image (2 bytes per pixel), two direct frame buffers of
the same size, about 150 KiB of mesh state and, only on the Slint path, up to
two 24-bit copies of the image as tiles; all in PSRAM (about 1.7 MiB at
352 × 352). It is freed when the game exits.

### Package assets

Asset files sit in a folder named after the package ID beside its Lua file
(`fs/games/<id>/`), and install to `/games/<id>/`. Names are 1–31 characters of
lowercase letters, digits, `_`, `-` and `.`, not starting with a dot; files are
at most 256 KiB. Packages cannot read them directly; host functions such as
`game.warp.load` take their names.

Firmware accepts staged asset uploads with
`game begin <id> <size> <sha256> <asset-name>`, followed by the same sequential
`game chunk <offset> <hex-data>` and `game commit` used for Lua source. Upload assets before
the Lua package. Removing a package also removes its asset folder.

### Limits

Drawing is bounded to 512 commands per frame. Firmware limits Lua allocations
to 256 KiB of PSRAM and decoded texture storage to a separate 256 KiB (16 slots,
maximum 256×256 each). Slint models and renderer allocations are additional.
Each callback is limited to 200,000 instructions and 20 ms (60 Hz packages:
100,000 instructions and 16 ms); initialization gets 250 ms to decode bounded
textures. Only selected base, math, string and table functionality is exposed;
there is no filesystem, networking, module loading, or device-control API
beyond the declared capabilities above.
Scores are written on game exit, rather than during the frame loop.

The packages reproduce the original gameplay, artwork, layouts, music and
feedback through the shared host API. Automated comparisons guard the tested
states; final hardware gameplay and performance validation remains necessary.

## Web installation and updates

`pio run -t package` builds a shared filesystem image, `littlefs.bin`, and includes
it in each firmware ZIP. It currently packages `fs/games/*.lua` under `/games`;
the filesystem can also hold other content. Nightly, release, and requested PR build bundles include it.
The web tool automatically flashes this image whenever it is present in a ZIP
or selected under Individual Files. It replaces the entire LittleFS filesystem,
including custom files and games. Packages without it leave LittleFS unchanged unless
Erase flash is selected. Filesystem-only uploads use the device's partition table.

High scores are stored in NVS and saved when exiting a game. Normal firmware OTA
and LittleFS replacement preserve saved high scores and settings. Erase flash
clears them. Firmware OTA currently updates only the application; use the web tool
to install an updated filesystem image.
