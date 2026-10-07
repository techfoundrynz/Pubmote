#pragma once
#include <stdint.h>

// game.warp: a deformable image layer for installed games (see warp_mesh.h and
// docs/installable-games.md). The game loads an RGB565 image asset from its
// package, describes a rig as data and sets each region's motion per frame; the
// firmware warps it on both cores and pushes it straight to the panel, or through
// Slint tiles while the game's own UI covers it or the screen is in transition.
// Everything here runs on the UI task.
struct lua_State;

// Adds load, rig and set to the table on top of the Lua stack.
void game_warp_open(lua_State *L);
// A game that needs the layer starts or stops (stop also frees everything).
void game_warp_begin(const char *game_id);
void game_warp_end();
// After each successful game callback: applies new images or rigs and hands
// the frame's motion to the warp.
void game_warp_commit();
// For game.warp.draw(): snaps (x, y) in display units to the layer's even pixel
// origin and gives the layer's size in display units. False with no image.
bool game_warp_place(float *x, float *y, float *w, float *h);
// After each draw(): whether the layer is in this frame, where, and whether a
// later command (or the Exit button) overlaps it.
void game_warp_layout(bool shown, float x, float y, bool covered);
// The game screen is fully in view with nothing over it.
void game_warp_placed(bool placed);

// Implemented by game_screen.cpp: host time spent inside a game's call (such as
// decoding an image) is not billed to its per-callback budget.
void game_host_credit(int64_t microseconds);
// Implemented by game_screen.cpp: update() cadence since the previous call, for
// the warp log. Times in microseconds; late counts updates that started over 2 ms
// after their slot, skipped the update slots dropped to catch up.
struct GameUpdateStats {
  int updates, late, skipped;
  int64_t lua_avg_us, lua_max_us, interval_avg_us, interval_max_us;
};
GameUpdateStats game_host_take_update_stats();
