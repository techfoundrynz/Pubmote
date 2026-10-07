#include "games/game_warp.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "games/game_store.h"
#include "games/warp_mesh.h"
#include "miniz.h"
#include "remote/display.h"
#include "slint-esp.h"
#include "slint_generated/app-window.h"
extern "C"
{
#include "lauxlib.h"
#include "lua.h"
}
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <new>
#include <optional>

// Two paths show the layer:
//
// Direct, normally: the layer bypasses Slint's software renderer, which costs
// ~0.5 us a pixel for an image, and goes straight to the panel through
// slint_esp_set_overlay. A worker on core 0 (the UI loop is pinned to core 1)
// takes each frame's motion from the UI, poses the mesh, plans which cells are
// stale and warps them into one of two frame buffers; a helper on core 1 takes
// cell rows from the same counter, so the free core does more. The UI pushes
// only the tiles that changed and hands the worker the other buffer. Both run
// at priority 1, below everything that matters; board comms on core 0 run at 20.
//
// Slint tiles, while the game draws over the layer or the screen is sliding: the
// layer is a grid of up to 4x4 double-buffered tile images, redrawn at ~30 Hz
// on the UI core, only where the mesh moved, and only changed tiles republished.
namespace
{
const char *TAG = "GAME-WARP";
constexpr int kTiles = 4; // per side, at most
using warp::Rgb565BE;
using Point = warp::Mesh::Point;
using TileBuffer = slint::SharedPixelBuffer<slint::Rgb8Pixel>;

template <typename T> T *psram_new() {
  void *memory = heap_caps_malloc(sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return memory ? new (memory) T() : nullptr;
}
template <typename T> void psram_delete(T *&object) {
  if (object) {
    object->~T();
    heap_caps_free(object);
    object = nullptr;
  }
}
template <typename T> T *psram_array(size_t count) {
  return static_cast<T *>(heap_caps_malloc(sizeof(T) * count, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}
void psram_free(void *&memory) {
  heap_caps_free(memory);
  memory = nullptr;
}

struct Worker {
  TaskHandle_t task = nullptr;
  volatile bool run = false;  // direct mode: produce frames
  volatile bool busy = false; // mid-frame, owns the mesh
  volatile bool quit = false, exited = false;
  volatile bool pending = false;    // a finished frame waits for the UI
  volatile uint32_t generation = 0; // bumped on pause; stale frames are dropped
  int slot = 1;                     // buffer the next frame goes to
  // Motion from the UI, the latest wins.
  portMUX_TYPE input_lock = portMUX_INITIALIZER_UNLOCKED;
  warp::Motion input[warp::kMaxRegions] = {};
  volatile bool input_fresh = false;
  // The finished frame.
  uint32_t frame_generation = 0;
  int frame_slot = 0;
  uint16_t mask = 0;
  int cells = 0;
  int64_t warp_us = 0;
  // Mesh of the last frame produced (PSRAM): tiles whose mesh moved since then must
  // be pushed even when their cells were already current in this frame's buffer.
  Point *previous = nullptr;
  // Core-1 helper: draws cell rows from next_row alongside the worker.
  TaskHandle_t helper = nullptr;
  SemaphoreHandle_t helper_go = nullptr, helper_done = nullptr;
  volatile bool helper_exited = false;
  Rgb565BE *helper_frame = nullptr;
  std::atomic<int> next_row{0};
  int split_frames = 0, helper_rows = 0, shared_rows = 0;
};

// Mesh vertices each tile buffer was last drawn with.
struct TileMeshes {
  static constexpr int kPoints = (warp::kMaxVertexSide / kTiles + 2) * (warp::kMaxVertexSide / kTiles + 2);
  Point seen[kTiles * kTiles][2][kPoints];
  bool valid[kTiles * kTiles][2];
};

struct State {
  char id[24] = "";
  bool active = false;
  // Image, native RGB565 (PSRAM).
  uint16_t *source = nullptr;
  int width = 0, height = 0;
  // Rig and motion as the game last set them; applied on commit.
  warp::Rig rig{};
  bool config_dirty = false, configured = false;
  warp::Motion input[warp::kMaxRegions] = {};
  bool input_dirty = false;
  warp::Mesh *mesh = nullptr;
  // Tile grid: cells per tile and tiles per side.
  int tile_cells_x = 1, tile_cells_y = 1, tiles_x = 1, tiles_y = 1;
  std::optional<TileBuffer> tiles[kTiles * kTiles][2];
  uint8_t tile_front[kTiles * kTiles] = {};
  TileMeshes *meshes = nullptr;
  bool tiles_ready = false;
  int64_t last_tiles_us = 0;
  std::shared_ptr<slint::VectorModel<GameTile>> tile_model;
  // Layout from the game's last draw.
  bool shown = false, covered = false, placed = false;
  float x = 0, y = 0;
  // Direct path.
  Rgb565BE *frames[2] = {nullptr, nullptr};
  bool direct = false;
  int direct_x = 0, direct_y = 0;
  int shown_frame = 0;
  uint16_t direct_dirty = 0; // tiles to push, bit ty * kTiles + tx
  Worker *worker = nullptr;
  // Stats, logged every 2 s.
  int64_t last_log_us = 0, warp_us = 0;
  int frames_done = 0, cells = 0, tiles_sent = 0;
};
State g;

const UiState *ui() {
  auto *window = get_slint_window();
  return window ? &window->global<UiState>() : nullptr;
}
float panel_width() {
  return get_slint_window()->global<Theme>().get_panel_width();
}
float panel_height() {
  return get_slint_window()->global<Theme>().get_panel_height();
}
int tile_index(int tx, int ty) {
  return ty * kTiles + tx;
}
int tile_px_x(int tx) {
  return std::min(g.tile_cells_x, g.mesh->cell_columns() - tx * g.tile_cells_x) * warp::kCell;
}
int tile_px_y(int ty) {
  return std::min(g.tile_cells_y, g.mesh->cell_rows() - ty * g.tile_cells_y) * warp::kCell;
}

// Pauses the worker and waits until it no longer holds the mesh. Frames it
// finished for the old generation are dropped.
void pause_worker() {
  Worker *w = g.worker;
  if (!w)
    return;
  w->run = false;
  for (int i = 0; i < 500 && __atomic_load_n(&w->busy, __ATOMIC_SEQ_CST); ++i)
    vTaskDelay(1);
  w->generation = w->generation + 1;
  w->pending = false;
}

void warp_worker(void *) {
  Worker &w = *g.worker;
  static warp::Motion motion[warp::kMaxRegions];
  uint32_t produced = 0;
  while (!w.quit) {
    // Idle until direct mode is on, the last frame was taken and there is new motion.
    if (!w.run || w.pending || !w.input_fresh) {
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
      continue;
    }
    __atomic_store_n(&w.busy, true, __ATOMIC_SEQ_CST);
    if (!w.run) { // stopped between the check and claiming the mesh
      __atomic_store_n(&w.busy, false, __ATOMIC_SEQ_CST);
      continue;
    }
    const uint32_t generation = w.generation;
    taskENTER_CRITICAL(&w.input_lock);
    memcpy(motion, w.input, sizeof(motion));
    w.input_fresh = false;
    taskEXIT_CRITICAL(&w.input_lock);
    const int64_t start = esp_timer_get_time();
    warp::Mesh &mesh = *g.mesh;
    const bool changed = mesh.pose(motion);
    if (changed) {
      Rgb565BE *frame = g.frames[w.slot];
      const int planned = mesh.plan(w.slot);
      if (w.helper && planned >= 48) {
        w.helper_frame = frame;
        w.next_row.store(0);
        xSemaphoreGive(w.helper_go);
        mesh.draw_planned_shared(g.source, frame, w.next_row);
        // Always wait: the helper is still writing this frame until it answers.
        xSemaphoreTake(w.helper_done, portMAX_DELAY);
        w.shared_rows += mesh.cell_rows();
        ++w.split_frames;
      }
      else {
        mesh.draw_planned(g.source, frame, 0, mesh.cell_rows());
      }
      // Tiles with drawn cells, and tiles around every vertex that moved since the
      // last frame (their other buffer's pixels may already be current).
      uint16_t mask = 0;
      const int cols = mesh.cell_columns();
      for (int row = 0; row < mesh.cell_rows(); ++row)
        for (int col = 0; col < cols; ++col)
          if (mesh.drawn[row * cols + col])
            mask |= uint16_t(1u << tile_index(col / g.tile_cells_x, row / g.tile_cells_y));
      for (int k = 0; k < mesh.active_count; ++k) {
        const int i = mesh.active[k];
        if (mesh.points[i].x != w.previous[i].x || mesh.points[i].y != w.previous[i].y) {
          const int row = i / mesh.columns, col = i % mesh.columns;
          for (int r = row - 1; r <= row; ++r)
            for (int c = col - 1; c <= col; ++c)
              if (r >= 0 && c >= 0 && r < mesh.cell_rows() && c < cols)
                mask |= uint16_t(1u << tile_index(c / g.tile_cells_x, r / g.tile_cells_y));
        }
        w.previous[i] = mesh.points[i];
      }
      w.frame_generation = generation;
      w.frame_slot = w.slot;
      w.cells = mesh.last_cells_drawn;
      w.mask = mask;
      w.warp_us = esp_timer_get_time() - start;
      w.slot ^= 1;
      __atomic_store_n(&w.pending, true, __ATOMIC_SEQ_CST);
    }
    __atomic_store_n(&w.busy, false, __ATOMIC_SEQ_CST);
    if (changed) {
      slint::invoke_from_event_loop([]() {
        Worker *worker = g.worker;
        if (!worker || !worker->pending)
          return;
        if (g.direct && worker->frame_generation == worker->generation) {
          g.shown_frame = worker->frame_slot;
          g.direct_dirty |= worker->mask;
          g.warp_us += worker->warp_us;
          g.cells += worker->cells;
          ++g.frames_done;
          if (g.direct_dirty)
            slint_esp_request_overlay();
        }
        __atomic_store_n(&worker->pending, false, __ATOMIC_SEQ_CST);
        if (worker->task)
          xTaskNotifyGive(worker->task);
      });
      // Waiting for the UI normally lets IDLE0 run and feed the task watchdog; an
      // explicit tick every 16 frames guarantees it.
      if (++produced % 16 == 0)
        vTaskDelay(1);
    }
  }
  w.exited = true;
  vTaskDelete(nullptr);
}

void warp_helper(void *) {
  Worker &w = *g.worker;
  while (!w.quit) {
    if (xSemaphoreTake(w.helper_go, pdMS_TO_TICKS(100)) != pdTRUE)
      continue;
    if (w.quit)
      break;
    w.helper_rows += g.mesh->draw_planned_shared(g.source, w.helper_frame, w.next_row);
    xSemaphoreGive(w.helper_done);
  }
  w.helper_exited = true;
  vTaskDelete(nullptr);
}

void start_worker() {
  if (g.worker)
    return;
  // Internal RAM: its locks and atomics are shared between the cores.
  void *memory = heap_caps_malloc(sizeof(Worker), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  g.worker = memory ? new (memory) Worker() : nullptr;
  Worker *w = g.worker;
  if (!w)
    return;
  w->previous = psram_array<Point>(warp::kMaxVertices);
  if (!w->previous) {
    w->~Worker();
    heap_caps_free(w);
    g.worker = nullptr;
    return;
  }
  w->helper_go = xSemaphoreCreateBinary();
  w->helper_done = xSemaphoreCreateBinary();
  if (w->helper_go && w->helper_done &&
      xTaskCreatePinnedToCore(warp_helper, "warp_helper", 3072, nullptr, 1, &w->helper, 1) != pdPASS)
    w->helper = nullptr;
  if (xTaskCreatePinnedToCore(warp_worker, "warp_worker", 4096, nullptr, 1, &w->task, 0) != pdPASS) {
    w->task = nullptr;
    ESP_LOGW(TAG, "No warp worker; the layer stays on Slint tiles");
  }
}

void stop_worker() {
  Worker *w = g.worker;
  if (!w)
    return;
  pause_worker();
  w->quit = true;
  if (w->task) {
    xTaskNotifyGive(w->task);
    for (int i = 0; i < 200 && !w->exited; ++i)
      vTaskDelay(1);
  }
  if (w->helper) {
    xSemaphoreGive(w->helper_go); // wakes it to see quit
    for (int i = 0; i < 200 && !w->helper_exited; ++i)
      vTaskDelay(1);
  }
  if (w->helper_go)
    vSemaphoreDelete(w->helper_go);
  if (w->helper_done)
    vSemaphoreDelete(w->helper_done);
  heap_caps_free(w->previous);
  g.worker = nullptr;
  w->~Worker();
  heap_caps_free(w);
}

// Overlay callback: runs on the UI task after Slint's frame, with the panel bus held.
void push_direct(bool repainted, void *) {
  const Rgb565BE *frame = g.frames[g.shown_frame];
  if (!g.direct || !frame || !g.mesh)
    return;
  // Slint may have painted over the layer's area this frame; then push all of it.
  const uint16_t mask = repainted ? 0xFFFF : g.direct_dirty;
  g.direct_dirty = 0;
  for (int ty = 0; ty < g.tiles_y; ++ty) {
    for (int tx = 0; tx < g.tiles_x; ++tx) {
      if (!(mask & (1u << tile_index(tx, ty))))
        continue;
      const int x0 = tx * g.tile_cells_x * warp::kCell, y0 = ty * g.tile_cells_y * warp::kCell;
      const auto *pixels = reinterpret_cast<const uint16_t *>(frame + y0 * g.width + x0);
      slint_esp_overlay_draw(g.direct_x + x0, g.direct_y + y0, tile_px_x(tx), tile_px_y(ty), pixels, g.width);
      ++g.tiles_sent;
    }
  }
}

// Brings every tile whose on-screen buffer is stale up to date: warps its moved
// cells into the back buffer and publishes it. `all` republishes every tile.
void publish_tiles(bool all) {
  if (!g.mesh || !g.meshes || !g.tile_model)
    return;
  warp::Mesh &mesh = *g.mesh;
  while (g.tile_model->row_count() < size_t(g.tiles_x * g.tiles_y))
    g.tile_model->push_back(GameTile{});
  for (int ty = 0; ty < g.tiles_y; ++ty) {
    for (int tx = 0; tx < g.tiles_x; ++tx) {
      const int t = tile_index(tx, ty), row = ty * g.tiles_x + tx;
      const int col0 = tx * g.tile_cells_x, row0 = ty * g.tile_cells_y;
      const int cols = tile_px_x(tx) / warp::kCell, rows = tile_px_y(ty) / warp::kCell;
      const int back = g.tile_front[t], shown = back ^ 1;
      // Nothing to do if what is on screen already matches the mesh.
      if (!all && g.meshes->valid[t][shown] && mesh.region_matches(col0, row0, cols, rows, g.meshes->seen[t][shown]))
        continue;
      auto &slot = g.tiles[t][back];
      if (!slot) {
        slot.emplace(cols * warp::kCell, rows * warp::kCell);
        g.meshes->valid[t][back] = false;
      }
      auto *out = slot->begin(); // copies if Slint still holds this buffer
      g.cells += mesh.render_region(g.source, out, cols * warp::kCell, col0, row0, cols, rows, g.meshes->seen[t][back],
                                    g.meshes->valid[t][back]);
      g.meshes->valid[t][back] = true;
      GameTile tile{};
      tile.image = slint::Image(*slot);
      tile.x = float(col0 * warp::kCell);
      tile.y = float(row0 * warp::kCell);
      tile.w = float(cols * warp::kCell);
      tile.h = float(rows * warp::kCell);
      g.tile_model->set_row_data(row, tile);
      g.tile_front[t] = shown;
      ++g.tiles_sent;
    }
  }
  g.tiles_ready = true;
}

void free_tiles() {
  for (auto &pair : g.tiles) {
    pair[0].reset();
    pair[1].reset();
  }
  if (g.meshes)
    for (auto &v : g.meshes->valid)
      v[0] = v[1] = false;
  g.tiles_ready = false;
  if (g.tile_model)
    while (g.tile_model->row_count() > 0)
      g.tile_model->erase(g.tile_model->row_count() - 1);
}

void leave_direct() {
  if (!g.direct)
    return;
  pause_worker();
  g.direct = false;
  slint_esp_set_overlay(nullptr, nullptr);
  // Slint shows its tiles again: bring them up to date before it draws.
  publish_tiles(true);
  if (ui())
    ui()->set_game_layer_direct(false);
}

void enter_direct() {
  if (g.direct || !g.worker || !g.worker->task)
    return;
  const size_t pixels = size_t(g.width) * g.height;
  for (auto &frame : g.frames)
    if (!frame)
      frame = psram_array<Rgb565BE>(pixels);
  if (!g.frames[0] || !g.frames[1]) {
    ESP_LOGW(TAG, "No memory for direct mode; staying on Slint tiles");
    return;
  }
  pause_worker();
  g.direct_x = int(lrintf(g.x * panel_width() / 100)) & ~1; // the panel only takes even windows
  g.direct_y = int(lrintf(g.y * panel_height() / 100)) & ~1;
  // Draw it all now: Slint is about to hide its tiles, and the overlay pass after
  // that repaint pushes the whole layer. The worker's first frame goes to the
  // other buffer, which it redraws in full too.
  g.mesh->pose(g.input);
  g.mesh->rendered_valid[0] = g.mesh->rendered_valid[1] = false;
  g.mesh->render(g.source, g.frames[0], 0);
  g.shown_frame = 0;
  g.direct_dirty = 0xFFFF;
  g.direct = true;
  slint_esp_set_overlay(push_direct, nullptr);
  if (ui())
    ui()->set_game_layer_direct(true);
  Worker &w = *g.worker;
  for (int i = 0; i < g.mesh->columns * g.mesh->rows; ++i)
    w.previous[i] = g.mesh->points[i];
  w.slot = 1;
  w.run = true;
  xTaskNotifyGive(w.task);
}

// Direct whenever nothing is drawn over the layer and the screen is still.
void update_mode() {
  const bool want = g.active && g.configured && g.shown && g.placed && !g.covered;
  if (want && !g.direct)
    enter_direct();
  else if (!want && g.direct)
    leave_direct();
  if (g.shown && !g.direct && !g.tiles_ready && g.configured) {
    g.mesh->pose(g.input);
    publish_tiles(true);
  }
}

void post_motion() {
  Worker *w = g.worker;
  if (!w)
    return;
  taskENTER_CRITICAL(&w->input_lock);
  memcpy(w->input, g.input, sizeof(w->input));
  w->input_fresh = true;
  taskEXIT_CRITICAL(&w->input_lock);
  if (w->task)
    xTaskNotifyGive(w->task);
}

void drop_image() {
  pause_worker();
  if (g.direct) {
    g.direct = false;
    slint_esp_set_overlay(nullptr, nullptr);
    if (ui())
      ui()->set_game_layer_direct(false);
  }
  free_tiles();
  for (auto &frame : g.frames) {
    void *memory = frame;
    psram_free(memory);
    frame = nullptr;
  }
  void *memory = g.source;
  psram_free(memory);
  g.source = nullptr;
  g.width = g.height = 0;
  g.configured = g.config_dirty = false;
  g.rig = {};
}

// Reads and inflates one zlib RGB565 (little-endian) asset. Returns an error or nullptr.
const char *load_image(const char *name, int width, int height) {
  size_t length = 0;
  char *packed = game_store_read_asset(g.id, name, &length);
  if (!packed) {
    drop_image();
    return "Image asset missing";
  }
  if (width != g.width || height != g.height || !g.source) {
    drop_image();
    g.source = psram_array<uint16_t>(size_t(width) * height);
  }
  else {
    pause_worker(); // it reads the source
  }
  const size_t bytes = size_t(width) * height * sizeof(uint16_t);
  // tinfl's ~11 KB state lives on the heap, not on the UI task's stack.
  auto *inflater = static_cast<tinfl_decompressor *>(heap_caps_malloc(sizeof(tinfl_decompressor), MALLOC_CAP_8BIT));
  bool ok = g.source && inflater;
  if (ok) {
    tinfl_init(inflater);
    size_t in_size = length, out_size = bytes;
    auto *start = reinterpret_cast<mz_uint8 *>(g.source);
    const tinfl_status status =
        tinfl_decompress(inflater, reinterpret_cast<const mz_uint8 *>(packed), &in_size, start, start, &out_size,
                         TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    ok = status == TINFL_STATUS_DONE && out_size == bytes;
  }
  heap_caps_free(inflater);
  free(packed);
  if (!ok) {
    const bool memory = !g.source;
    drop_image();
    return memory ? "Not enough memory for the image" : "Invalid image asset";
  }
  g.width = width;
  g.height = height;
  // A new image starts still, with no rig, until the game sets one.
  g.rig = {};
  memset(g.input, 0, sizeof(g.input));
  g.config_dirty = true;
  g.input_dirty = true;
  return nullptr;
}

// Lua bindings. Arguments are validated before any C++ object with a destructor
// exists, because Lua errors unwind with longjmp.
float number_field(lua_State *L, int table, const char *key, float fallback, float low, float high) {
  lua_getfield(L, table, key);
  float value = fallback;
  if (!lua_isnil(L, -1)) {
    luaL_argcheck(L, lua_type(L, -1) == LUA_TNUMBER, 1, key);
    value = lua_tonumber(L, -1);
    if (!(value >= low && value <= high))
      luaL_error(L, "rig: %s out of range", key);
  }
  lua_pop(L, 1);
  return value;
}
// Reads `count` numbers from the table at the top of the stack.
void numbers(lua_State *L, float *out, int count, const char *what) {
  if (!lua_istable(L, -1) || (int)lua_rawlen(L, -1) != count)
    luaL_error(L, "rig: invalid %s", what);
  for (int i = 0; i < count; ++i) {
    lua_rawgeti(L, -1, i + 1);
    const float value = lua_tonumber(L, -1);
    if (lua_type(L, -1) != LUA_TNUMBER || !(value >= -10000 && value <= 10000))
      luaL_error(L, "rig: invalid %s", what);
    out[i] = value;
    lua_pop(L, 1);
  }
}
void ellipse(lua_State *L, warp::Ellipse &e) {
  float v[4];
  numbers(L, v, 4, "ellipse");
  e = {v[0], v[1], v[2], v[3]};
}
int list_length(lua_State *L, int limit, const char *what) {
  if (lua_isnil(L, -1))
    return 0;
  if (!lua_istable(L, -1))
    luaL_error(L, "rig: invalid %s", what);
  const int n = (int)lua_rawlen(L, -1);
  if (n > limit)
    luaL_error(L, "rig: too many %s", what);
  return n;
}
uint8_t mask_field(lua_State *L, int table, const char *key, int high) {
  lua_getfield(L, table, key);
  const int n = list_length(L, 8, key);
  uint8_t mask = 0;
  for (int i = 0; i < n; ++i) {
    lua_rawgeti(L, -1, i + 1);
    int is_integer = 0;
    const lua_Integer value = lua_tointegerx(L, -1, &is_integer);
    if (!is_integer || value < 1 || value > high)
      luaL_error(L, "rig: invalid %s", key);
    mask |= uint8_t(1u << (value - 1));
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  return mask;
}

int warp_rig(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  if (!g.source)
    return luaL_error(L, "rig: no image loaded");
  static warp::Rig rig; // parsed here, applied on commit
  rig = warp::Rig{};
  rig.edge = number_field(L, 1, "edge", 0, 0, 10000);
  lua_getfield(L, 1, "pins");
  rig.pin_count = list_length(L, warp::kMaxPins, "pins");
  for (int i = 0; i < rig.pin_count; ++i) {
    lua_rawgeti(L, -1, i + 1);
    if (!lua_istable(L, -1))
      luaL_error(L, "rig: invalid pin");
    warp::Pin &pin = rig.pins[i];
    lua_getfield(L, -1, "rows");
    if (lua_isnil(L, -1)) {
      lua_pop(L, 1);
      float box[4];
      numbers(L, box, 4, "pin box");
      pin.rows = 0;
      pin.left = box[0];
      pin.top = box[1];
      pin.right = box[2];
      pin.bottom = box[3];
    }
    else {
      pin.rows = list_length(L, warp::kMaxProfileRows, "profile rows");
      if (pin.rows < 2)
        luaL_error(L, "rig: a profile needs 2 rows or more");
      for (int r = 0; r < pin.rows; ++r) {
        lua_rawgeti(L, -1, r + 1);
        float row[3];
        numbers(L, row, 3, "profile row");
        pin.row[r] = {row[0], row[1], row[2]};
        if (r > 0 && !(row[0] > pin.row[r - 1].y))
          luaL_error(L, "rig: profile rows must go down");
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  lua_getfield(L, 1, "regions");
  rig.region_count = list_length(L, warp::kMaxRegions, "regions");
  const int regions = lua_gettop(L);
  for (int i = 0; i < rig.region_count; ++i) {
    lua_rawgeti(L, regions, i + 1);
    const int t = lua_gettop(L);
    if (!lua_istable(L, t))
      luaL_error(L, "rig: invalid region");
    warp::Region &r = rig.regions[i];
    lua_getfield(L, t, "patch");
    const bool patch = !lua_isnil(L, -1);
    if (patch) {
      r.kind = warp::Region::Patch;
      r.ellipse_count = 1;
      ellipse(L, r.ellipses[0]);
    }
    lua_pop(L, 1);
    lua_getfield(L, t, "blobs");
    if (patch == !lua_isnil(L, -1))
      luaL_error(L, "rig: a region needs either patch or blobs");
    if (!patch) {
      r.kind = warp::Region::Blob;
      r.ellipse_count = uint8_t(list_length(L, warp::kMaxBlobs, "blobs"));
      for (int k = 0; k < r.ellipse_count; ++k) {
        lua_rawgeti(L, -1, k + 1);
        ellipse(L, r.ellipses[k]);
        lua_pop(L, 1);
      }
    }
    lua_pop(L, 1);
    const float group = number_field(L, t, "group", 0, 0, 8);
    const float under = number_field(L, t, "under", 0, 1, float(i));
    if (group != float(int(group)) || under != float(int(under)))
      luaL_error(L, "rig: group and under are integers");
    r.group = uint8_t(group);
    r.under = int8_t(int(under) - 1);
    lua_getfield(L, t, "anchor");
    if (!lua_isnil(L, -1)) {
      if (!patch)
        luaL_error(L, "rig: only patches have an anchor");
      float anchor[3];
      numbers(L, anchor, 3, "anchor");
      if (!(anchor[0] >= 0 && anchor[0] <= 1) || (anchor[0] < 1 && !(anchor[2] > anchor[1])))
        luaL_error(L, "rig: invalid anchor");
      r.anchor = anchor[0];
      r.anchor_top = anchor[1];
      r.anchor_bottom = anchor[2];
    }
    lua_pop(L, 1);
    r.pins = mask_field(L, t, "pins", rig.pin_count);
    r.feather = number_field(L, t, "feather", 0, 0, 10000);
    if (r.pins && !(r.feather > 0))
      luaL_error(L, "rig: pinned regions need a feather");
    r.clear = mask_field(L, t, "clear", 8);
    if (patch) {
      r.travel = number_field(L, t, "travel", 1, 0, 100);
    }
    else {
      lua_getfield(L, t, "travel");
      if (!lua_isnil(L, -1)) {
        float travel[2];
        numbers(L, travel, 2, "travel");
        if (!(travel[0] >= 0 && travel[1] >= 0))
          luaL_error(L, "rig: invalid travel");
        r.travel_x = travel[0];
        r.travel_y = travel[1];
      }
      lua_pop(L, 1);
    }
    r.squash = number_field(L, t, "squash", 0, 0, 0.999f);
    lua_settop(L, regions);
  }
  lua_pop(L, 1);
  if (!warp::Mesh::valid_rig(rig))
    return luaL_error(L, "rig: invalid");
  g.rig = rig;
  memset(g.input, 0, sizeof(g.input));
  g.config_dirty = true;
  g.input_dirty = true;
  return 0;
}

int warp_load(lua_State *L) {
  const char *name = luaL_checkstring(L, 1);
  luaL_argcheck(L, game_store_asset_name(name), 1, "invalid asset name");
  const lua_Integer w = luaL_checkinteger(L, 2), h = luaL_checkinteger(L, 3);
  luaL_argcheck(L, warp::Mesh::valid_size(int(w), int(h)), 2, "size must be 8..352 px in steps of 8");
  const int64_t start = esp_timer_get_time();
  const char *error = load_image(name, int(w), int(h));
  const int64_t spent = esp_timer_get_time() - start;
  game_host_credit(spent);
  if (error) {
    ESP_LOGW(TAG, "%s: %s", name, error);
    lua_pushboolean(L, 0);
    lua_pushstring(L, error);
    return 2;
  }
  ESP_LOGI(TAG, "Loaded %s (%lldx%lld) in %lld us", name, (long long)w, (long long)h, (long long)spent);
  lua_pushboolean(L, 1);
  return 1;
}

int warp_set(lua_State *L) {
  const lua_Integer index = luaL_checkinteger(L, 1);
  luaL_argcheck(L, index >= 1 && index <= g.rig.region_count, 1, "no such region");
  const auto value = [L](int arg) {
    const float v = lua_isnoneornil(L, arg) ? 0.0f : float(luaL_checknumber(L, arg));
    return std::isfinite(v) ? v : 0.0f;
  };
  warp::Motion &m = g.input[index - 1];
  const warp::Motion next = {value(2), value(3), value(4)};
  if (next.dx != m.dx || next.dy != m.dy || next.squash != m.squash) {
    m = next;
    g.input_dirty = true;
  }
  return 0;
}
} // namespace

void game_warp_open(lua_State *L) {
  const luaL_Reg api[] = {{"load", warp_load}, {"rig", warp_rig}, {"set", warp_set}, {nullptr, nullptr}};
  luaL_setfuncs(L, api, 0);
}

void game_warp_begin(const char *game_id) {
  game_warp_end();
  strlcpy(g.id, game_id, sizeof(g.id));
  g.active = true;
  if (!g.mesh)
    g.mesh = psram_new<warp::Mesh>();
  if (!g.meshes)
    g.meshes = psram_new<TileMeshes>();
  if (!g.tile_model)
    g.tile_model = std::make_shared<slint::VectorModel<GameTile>>();
  if (ui()) {
    ui()->set_game_layer_tiles(g.tile_model);
    ui()->set_game_layer_direct(false);
  }
  if (!g.mesh || !g.meshes) {
    ESP_LOGE(TAG, "No memory for the warp mesh");
    g.active = false;
    return;
  }
  start_worker();
  g.last_log_us = esp_timer_get_time();
}

void game_warp_end() {
  if (!g.active && !g.mesh && !g.worker)
    return;
  g.active = false;
  drop_image(); // leaves direct mode
  stop_worker();
  psram_delete(g.mesh);
  psram_delete(g.meshes);
  g.shown = g.covered = false;
  g.id[0] = 0;
}

void game_warp_commit() {
  if (!g.active || !g.source)
    return;
  if (g.config_dirty) {
    pause_worker();
    g.configured = g.mesh->configure(g.width, g.height, g.rig);
    g.config_dirty = false;
    const int columns = g.mesh->cell_columns(), rows = g.mesh->cell_rows();
    g.tile_cells_x = (columns + kTiles - 1) / kTiles;
    g.tile_cells_y = (rows + kTiles - 1) / kTiles;
    g.tiles_x = (columns + g.tile_cells_x - 1) / g.tile_cells_x;
    g.tiles_y = (rows + g.tile_cells_y - 1) / g.tile_cells_y;
    // New image or rig: every tile is redrawn in full, at its own size.
    free_tiles();
    for (int i = 0; i < g.tiles_x * g.tiles_y; ++i)
      g.tile_model->push_back(GameTile{});
    if (g.direct) {
      if (g.worker)
        g.worker->run = true;
      g.input_dirty = true;
    }
    update_mode();
  }
  if (!g.configured)
    return;
  if (g.direct) {
    if (g.input_dirty)
      post_motion();
    g.input_dirty = false;
  }
  else if (g.shown) {
    // Slint path at ~30 Hz, on the UI core.
    const int64_t now = esp_timer_get_time();
    if (now - g.last_tiles_us >= 30000) {
      g.last_tiles_us = now;
      const int64_t start = now;
      if (g.mesh->pose(g.input) || !g.tiles_ready) {
        publish_tiles(!g.tiles_ready);
        g.warp_us += esp_timer_get_time() - start;
        ++g.frames_done;
      }
      g.input_dirty = false;
    }
  }
  const int64_t now = esp_timer_get_time();
  if (now - g.last_log_us >= 2000000) {
    const int per = g.frames_done ? g.frames_done : 1;
    const Worker *w = g.worker;
    // The game's update() cadence: Lua time per call and the interval between
    // calls (avg/max), and calls that started late or slots that were dropped.
    const GameUpdateStats u = game_host_take_update_stats();
    ESP_LOGI(TAG,
             "%s: %.1f fps, warp %lld us, cells %d/%d, tiles %d, guard %.2f, split %d, core1 %d%%, "
             "lua %lld/%lld us, tick %lld/%lld us, late %d, skip %d",
             g.direct ? "direct" : "tiles", g.frames_done * 1000000.0f / (now - g.last_log_us),
             (long long)(g.warp_us / per), g.cells / per, g.mesh->cell_columns() * g.mesh->cell_rows(),
             g.tiles_sent / per, g.mesh->last_limit_scale, w ? w->split_frames : 0,
             w && w->shared_rows ? 100 * w->helper_rows / w->shared_rows : 0, (long long)u.lua_avg_us,
             (long long)u.lua_max_us, (long long)u.interval_avg_us, (long long)u.interval_max_us, u.late, u.skipped);
    g.warp_us = 0;
    g.frames_done = g.cells = g.tiles_sent = 0;
    if (g.worker)
      g.worker->split_frames = g.worker->helper_rows = g.worker->shared_rows = 0;
    g.last_log_us = now;
  }
}

bool game_warp_place(float *x, float *y, float *w, float *h) {
  if (!g.active || !g.source)
    return false;
  const float pw = panel_width(), ph = panel_height();
  *x = float(int(lrintf(*x * pw / 100)) & ~1) * 100 / pw;
  *y = float(int(lrintf(*y * ph / 100)) & ~1) * 100 / ph;
  *w = g.width * 100 / pw;
  *h = g.height * 100 / ph;
  return true;
}

void game_warp_layout(bool shown, float x, float y, bool covered) {
  if (!g.active)
    return;
  if (g.direct && shown && (x != g.x || y != g.y)) {
    // Moved: Slint repaints the old and new placeholder areas, then the overlay
    // pushes the whole layer at its new origin.
    g.direct_x = int(lrintf(x * panel_width() / 100)) & ~1;
    g.direct_y = int(lrintf(y * panel_height() / 100)) & ~1;
    g.direct_dirty = 0xFFFF;
    slint_esp_request_overlay();
  }
  g.shown = shown;
  g.x = x;
  g.y = y;
  g.covered = covered;
  update_mode();
}

void game_warp_placed(bool placed) {
  g.placed = placed;
  if (g.active)
    update_mode();
}
