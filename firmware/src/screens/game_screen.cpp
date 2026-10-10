#include "game_screen.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "games/game_store.h"
#include "mbedtls/base64.h"
#include "miniz.h"
#include "psa/crypto.h"
#include "remote/buzzer.h"
#include "remote/haptic.h"
#include "remote/input_router.h"
#include "remote/powermanagement.h"
#include "remote/remoteinputs.h"
#include "remote/settings_snapshot.h"
#include "remote/settings_store.h"
#include "slint_generated/app-window.h"
#include "ui/slint_window.h"
#include "utilities/ui_operation.h"
extern "C"
{
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

static constexpr size_t LUA_BUDGET = 256 * 1024;
static constexpr size_t MAX_COMMANDS = 512;
static lua_State *vm;
static size_t allocated;
static int hooks_left;
static int64_t deadline, last_tick;
static game_info_t catalog[GAME_MAX_INSTALLED], active;
static int catalog_count, selected = -1;
static uint32_t best_score;
static char score_key[16];
static bool drawing;
static bool exit_saved = false;
static void start_game(char *source, size_t length);
static const char *callback_name = "startup";
static InputRepeat game_repeat;
static slint::Image textures[16];
static size_t texture_bytes;
static BuzzerNote notes[128];
static std::vector<GameDraw> frame;
static std::shared_ptr<slint::VectorModel<GameDraw>> model;
static std::shared_ptr<slint::VectorModel<GamePosition>> positions;
static const UiState *ui() {
  auto *window = get_slint_window();
  return window ? &window->global<UiState>() : nullptr;
}
static void *lua_alloc(void *, void *ptr, size_t old_size, size_t size) {
  if (!ptr)
    old_size = 0;
  if (!size) {
    free(ptr);
    allocated -= old_size;
    return nullptr;
  }
  if (size > LUA_BUDGET || allocated - old_size > LUA_BUDGET - size)
    return nullptr;
  void *next = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (next)
    allocated = allocated - old_size + size;
  return next;
}
static void budget_hook(lua_State *L, lua_Debug *) {
  if (--hooks_left <= 0 || esp_timer_get_time() > deadline)
    luaL_error(L, "Game exceeded execution budget");
}
static void arm_budget(int time_budget_us) {
  hooks_left = 200; // 200,000 instructions per callback, also capped by wall time.
  deadline = esp_timer_get_time() + time_budget_us;
  lua_sethook(vm, budget_hook, LUA_MASKCOUNT, 1000);
}
static int bounded_int(lua_State *L, int arg, int low, int high) {
  lua_Integer value = luaL_checkinteger(L, arg);
  luaL_argcheck(L, value >= low && value <= high, arg, "out of range");
  return (int)value;
}
static float coordinate(lua_State *L, int arg) {
  float value = luaL_checknumber(L, arg);
  luaL_argcheck(L, std::isfinite(value) && value >= -200 && value <= 300, arg, "invalid coordinate");
  return value;
}
static int draw_rect(lua_State *L) {
  if (!drawing || frame.size() >= MAX_COMMANDS)
    return luaL_error(L, "Drawing budget exceeded");
  float x = coordinate(L, 1), y = coordinate(L, 2), w = coordinate(L, 3), h = coordinate(L, 4);
  int color = bounded_int(L, 5, 0, 0xffffff);
  float radius = lua_gettop(L) >= 6 ? coordinate(L, 6) : 0;
  float border = lua_gettop(L) >= 7 ? coordinate(L, 7) : 0;
  int edge = lua_gettop(L) >= 8 ? bounded_int(L, 8, 0, 0xffffff) : 0;
  int alpha = lua_gettop(L) >= 9 ? bounded_int(L, 9, 0, 255) : 255;
  luaL_argcheck(L, w >= 0 && h >= 0 && radius >= 0, 3, "negative size");
  GameDraw cmd{};
  cmd.x = x;
  cmd.y = y;
  cmd.w = w;
  cmd.h = h;
  cmd.radius = radius;
  cmd.color = slint::Color::from_argb_uint8(alpha, color >> 16, color >> 8, color);
  cmd.border = border;
  cmd.edge = slint::Color::from_argb_uint8(border > 0 ? 255 : 0, edge >> 16, edge >> 8, edge);
  frame.push_back(cmd);
  return 0;
}
static int draw_text(lua_State *L) {
  if (!drawing || frame.size() >= MAX_COMMANDS)
    return luaL_error(L, "Drawing budget exceeded");
  float x = coordinate(L, 1), y = coordinate(L, 2), w = coordinate(L, 3);
  size_t size;
  const char *text = luaL_checklstring(L, 4, &size);
  luaL_argcheck(L, size <= 96 && !memchr(text, 0, size), 4, "text too long");
  int color = bounded_int(L, 5, 0, 0xffffff);
  int font = bounded_int(L, 6, 8, 28);
  float height = lua_gettop(L) >= 7 ? coordinate(L, 7) : 8;
  bool mono = lua_gettop(L) >= 8 && lua_toboolean(L, 8);
  bool bold = lua_gettop(L) >= 9 && lua_toboolean(L, 9);
  GameDraw cmd{};
  cmd.kind = 1;
  cmd.x = x;
  cmd.y = y;
  cmd.w = w;
  cmd.h = height;
  cmd.font = font;
  cmd.mono = mono;
  cmd.bold = bold;
  cmd.text = slint::SharedString(text);
  cmd.color = slint::Color::from_rgb_uint8(color >> 16, color >> 8, color);
  frame.push_back(cmd);
  return 0;
}
static int tone(lua_State *L) {
  int hz = bounded_int(L, 1, 0, 4000), ms = bounded_int(L, 2, 0, 1000);
  if (settings_get_device().startup_sound != STARTUP_SOUND_DISABLED)
    buzzer_set_tone((BuzzerToneFrequency)hz, ms);
  return 0;
}
static int vibrate(lua_State *L) {
  int pattern = bounded_int(L, 1, 0, 5);
  const HapticFeedbackPattern patterns[] = {HAPTIC_SINGLE_CLICK, HAPTIC_DOUBLE_CLICK, HAPTIC_TRIPLE_CLICK,
                                            HAPTIC_SOFT_BUMP,    HAPTIC_SOFT_BUZZ,    HAPTIC_STRONG_BUZZ};
  haptic_vibrate(patterns[pattern]);
  return 0;
}
static int random_below(lua_State *L) {
  int limit = bounded_int(L, 1, 1, 1000000);
  lua_pushinteger(L, esp_random() % limit);
  return 1;
}
static int sequence(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  int count = lua_rawlen(L, 1);
  luaL_argcheck(L, count > 0 && count <= 128, 1, "invalid sequence length");
  // Stop playback before replacing its persistent backing storage.
  buzzer_stop();
  for (int i = 0; i < count; ++i) {
    lua_rawgeti(L, 1, i + 1);
    luaL_checktype(L, -1, LUA_TTABLE);
    lua_rawgeti(L, -1, 1);
    int hz = bounded_int(L, lua_gettop(L), 0, 4000);
    lua_pop(L, 1);
    lua_rawgeti(L, -1, 2);
    int ms = bounded_int(L, lua_gettop(L), 1, 1000);
    lua_pop(L, 2);
    notes[i] = {(uint16_t)hz, (uint16_t)ms};
  }
  if (settings_get_device().startup_sound != STARTUP_SOUND_DISABLED)
    buzzer_play_sequence(notes, count, lua_toboolean(L, 2));
  return 0;
}
static int screen_info(lua_State *L) {
  const auto &theme = get_slint_window()->global<Theme>();
  lua_pushnumber(L, theme.get_panel_width());
  lua_pushnumber(L, theme.get_panel_height());
  lua_pushboolean(L, theme.get_is_square_mode());
  lua_pushboolean(L, ui() && ui()->get_joystick_supported());
  lua_pushboolean(L, ui() && ui()->get_button_supported());
  return 5;
}
static int texture(lua_State *L) {
  int id = bounded_int(L, 1, 0, 15), w = bounded_int(L, 2, 1, 256), h = bounded_int(L, 3, 1, 256);
  size_t length;
  const char *encoded = luaL_checklstring(L, 4, &length);
  size_t size = (size_t)w * h * 4;
  luaL_argcheck(L, !drawing && texture_bytes + size <= 256 * 1024 && length <= 32768 && length % 4 == 0, 4,
                "texture budget exceeded");
  unsigned char *compressed = (unsigned char *)malloc(length);
  if (!compressed)
    return luaL_error(L, "texture allocation failed");
  size_t compressed_size;
  if (mbedtls_base64_decode(compressed, length, &compressed_size, (const unsigned char *)encoded, length) != 0) {
    free(compressed);
    return luaL_error(L, "invalid texture encoding");
  }
  bool ok;
  {
    slint::SharedPixelBuffer<slint::Rgba8Pixel> pixels(w, h);
    ok = tinfl_decompress_mem_to_mem(pixels.begin(), size, compressed, compressed_size, TINFL_FLAG_PARSE_ZLIB_HEADER) ==
         size;
    if (ok) {
      textures[id] = slint::Image(pixels);
      texture_bytes += size;
    }
  }
  free(compressed);
  if (!ok)
    return luaL_error(L, "invalid texture data");
  return 0;
}
static int sprite(lua_State *L) {
  int id = bounded_int(L, 1, 0, 15);
  float x = coordinate(L, 2), y = coordinate(L, 3), w = coordinate(L, 4), h = coordinate(L, 5);
  if (!drawing || frame.size() >= MAX_COMMANDS)
    return luaL_error(L, "Drawing budget exceeded");
  int tint = lua_isnoneornil(L, 6) ? -1 : bounded_int(L, 6, 0, 0xffffff);
  GameDraw cmd{};
  cmd.kind = 2;
  cmd.x = x;
  cmd.y = y;
  cmd.w = w;
  cmd.h = h;
  cmd.image = textures[id];
  cmd.color =
      tint < 0 ? slint::Color::from_argb_uint8(0, 0, 0, 0) : slint::Color::from_rgb_uint8(tint >> 16, tint >> 8, tint);
  frame.push_back(cmd);
  return 0;
}
static int column(lua_State *L) {
  if (!drawing || frame.size() >= MAX_COMMANDS)
    return luaL_error(L, "Drawing budget exceeded");
  float x = coordinate(L, 1), y = coordinate(L, 2), w = coordinate(L, 3), h = coordinate(L, 4);
  float spacing = coordinate(L, 5), padding = coordinate(L, 6);
  luaL_checktype(L, 7, LUA_TTABLE);
  int count = lua_rawlen(L, 7);
  luaL_argcheck(L, count <= 16, 7, "too many labels");
  // Validate before constructing C++ values, because Lua errors use longjmp.
  struct LabelData {
    const char *text;
    int font, color;
    bool mono, bold, wrap;
    float height, cell;
    int cells[16];
  } labels[16]{};
  for (int i = 0; i < count; ++i) {
    lua_rawgeti(L, 7, i + 1);
    luaL_checktype(L, -1, LUA_TTABLE);
    lua_rawgeti(L, -1, 1);
    size_t size;
    labels[i].text = luaL_checklstring(L, -1, &size);
    luaL_argcheck(L, size <= 96, 7, "label too long");
    lua_pop(L, 1);
    lua_rawgeti(L, -1, 2);
    labels[i].font = bounded_int(L, lua_gettop(L), 8, 28);
    lua_pop(L, 1);
    lua_rawgeti(L, -1, 3);
    labels[i].color = bounded_int(L, lua_gettop(L), 0, 0xffffff);
    lua_pop(L, 1);
    lua_rawgeti(L, -1, 4);
    labels[i].mono = lua_toboolean(L, -1);
    lua_pop(L, 1);
    lua_rawgeti(L, -1, 5);
    labels[i].bold = lua_toboolean(L, -1);
    lua_pop(L, 1);
    lua_rawgeti(L, -1, 6);
    labels[i].height = lua_isnil(L, -1) ? -1 : coordinate(L, lua_gettop(L));
    lua_pop(L, 1);
    lua_rawgeti(L, -1, 7);
    if (lua_istable(L, -1)) {
      for (int j = 0; j < 16; ++j) {
        lua_rawgeti(L, -1, j + 1);
        labels[i].cells[j] = bounded_int(L, lua_gettop(L), 0, 0xffffff);
        lua_pop(L, 1);
      }
    }
    lua_pop(L, 1);
    lua_rawgeti(L, -1, 8);
    labels[i].cell = lua_isnil(L, -1) ? 0 : coordinate(L, lua_gettop(L));
    lua_pop(L, 1);
    lua_rawgeti(L, -1, 9);
    labels[i].wrap = lua_toboolean(L, -1);
    lua_pop(L, 2);
  }
  // Reuse nested label/preview models when their content is unchanged. Pointer
  // identity then makes the outer command comparable without dirtying layouts.
  std::shared_ptr<slint::Model<GameLabel>> cached;
  if (frame.size() < model->row_count()) {
    auto old = model->row_data(frame.size());
    if (old && old->kind == 3 && old->labels && old->labels->row_count() == (size_t)count)
      cached = old->labels;
  }
  for (int i = 0; cached && i < count; ++i) {
    const auto old = cached->row_data(i);
    const auto &data = labels[i];
    auto color = slint::Color::from_rgb_uint8(data.color >> 16, data.color >> 8, data.color);
    bool same = old && old->text == data.text && old->font == data.font && old->color == color &&
                old->mono == data.mono && old->bold == data.bold && old->height == data.height &&
                old->cell == data.cell && old->wrap == data.wrap;
    if (same && data.cell > 0) {
      same = old->cells && old->cells->row_count() == 16;
      for (int j = 0; same && j < 16; ++j) {
        int c = data.cells[j];
        same = old->cells->row_data(j) == slint::Color::from_argb_uint8(c ? 255 : 0, c >> 16, c >> 8, c);
      }
    }
    if (!same)
      cached.reset();
  }
  auto rows = cached ? nullptr : std::make_shared<slint::VectorModel<GameLabel>>();
  for (int i = 0; !cached && i < count; ++i) {
    GameLabel label{};
    auto &data = labels[i];
    label.text = slint::SharedString(data.text);
    label.font = data.font;
    label.mono = data.mono;
    label.bold = data.bold;
    label.height = data.height;
    label.color = slint::Color::from_rgb_uint8(data.color >> 16, data.color >> 8, data.color);
    label.cell = data.cell;
    label.wrap = data.wrap;
    if (data.cell > 0) {
      auto cells = std::make_shared<slint::VectorModel<slint::Color>>();
      for (int color : data.cells)
        cells->push_back(slint::Color::from_argb_uint8(color ? 255 : 0, color >> 16, color >> 8, color));
      label.cells = cells;
    }
    rows->push_back(label);
  }
  GameDraw cmd{};
  cmd.kind = 3;
  cmd.x = x;
  cmd.y = y;
  cmd.w = w;
  cmd.h = h;
  cmd.spacing = spacing;
  cmd.pad_bottom = padding;
  cmd.labels = cached ? cached : rows;
  frame.push_back(cmd);
  return 0;
}
static int exit_button(lua_State *L) {
  float x = coordinate(L, 1), y = coordinate(L, 2), w = coordinate(L, 3), h = coordinate(L, 4);
  if (!drawing || frame.size() >= MAX_COMMANDS)
    return luaL_error(L, "Drawing budget exceeded");
  GameDraw cmd{};
  cmd.kind = 4;
  cmd.x = x;
  cmd.y = y;
  cmd.w = w;
  cmd.h = h;
  frame.push_back(cmd);
  return 0;
}
static int save_score(lua_State *L) {
  uint32_t value = bounded_int(L, 1, 0, 1000000000);
  // Writes happen once on exit, never in a game callback or frame loop.
  if (value > best_score)
    best_score = value;
  return 0;
}
static int repeat_input(lua_State *L) {
  int delay = bounded_int(L, 1, 0, 1000);
  int interval = bounded_int(L, 2, 0, 1000);
  luaL_argcheck(L, interval == 0 || interval >= 30, 2, "repeat interval too short");
  game_repeat = input_repeat(delay, interval);
  return 0;
}
static int bootstrap(lua_State *L) {
  luaL_requiref(L, "_G", luaopen_base, 1);
  lua_pop(L, 1);
  const char *blocked[] = {"dofile", "loadfile", "load", "collectgarbage", "pcall",
                           "xpcall", "print",    "warn", "setmetatable",   "getmetatable"};
  for (auto name : blocked) {
    lua_pushnil(L);
    lua_setglobal(L, name);
  }
  luaL_requiref(L, "math", luaopen_math, 1);
  lua_pop(L, 1);
  luaL_requiref(L, "table", luaopen_table, 1);
  lua_pop(L, 1);
  luaL_requiref(L, "string", luaopen_string, 1);
  lua_pushnil(L);
  lua_setfield(L, -2, "dump");
  lua_pop(L, 1);
  lua_newtable(L);
  const luaL_Reg api[] = {{"rect", draw_rect},      {"text", draw_text},        {"tone", tone},
                          {"haptic", vibrate},      {"save_score", save_score}, {"repeat_input", repeat_input},
                          {"random", random_below}, {"sequence", sequence},     {"screen", screen_info},
                          {"texture", texture},     {"sprite", sprite},         {"column", column},
                          {"exit", exit_button},    {nullptr, nullptr}};
  luaL_setfuncs(L, api, 0);
  lua_setglobal(L, "game");
  return 0;
}
static void stop_vm(const char *error) {
  if (ui() && error)
    ui()->set_game_error(slint::SharedString(error));
  if (vm) {
    lua_close(vm);
    vm = nullptr;
  }
  buzzer_stop();
  drawing = false;
}
static bool finish_call(int arguments, int results = 0, int time_budget_us = 20000) {
  arm_budget(time_budget_us);
  if (lua_pcall(vm, arguments, results, 0) != LUA_OK) {
    const char *message = lua_type(vm, -1) == LUA_TSTRING ? lua_tostring(vm, -1) : "callback failed";
    char error[160];
    snprintf(error, sizeof(error), "Game stopped (%s): %.110s", callback_name, message);
    stop_vm(error);
    return false;
  }
  return true;
}
static bool function(const char *name) {
  if (!vm)
    return false;
  callback_name = name;
  lua_getglobal(vm, name);
  if (!lua_isfunction(vm, -1)) {
    stop_vm("Game is missing a required callback");
    return false;
  }
  return true;
}
static bool same_style(const GameDraw &a, const GameDraw &b) {
  if (a.kind != b.kind)
    return false;
  switch (a.kind) {
  case 0:
    return a.color == b.color && a.radius == b.radius && a.border == b.border && a.edge == b.edge;
  case 1:
    return a.text == b.text && a.color == b.color && a.font == b.font && a.mono == b.mono && a.bold == b.bold;
  case 2:
    return a.image == b.image && a.color == b.color;
  case 3:
    return a.labels == b.labels && a.spacing == b.spacing && a.pad_bottom == b.pad_bottom;
  case 4:
    return true;
  default:
    return false;
  }
}
static void render(void) {
  if (!function("draw"))
    return;
  frame.clear();
  drawing = true;
  bool ok = finish_call(0);
  drawing = false;
  if (!ok)
    return;
  // A row_changed notification invalidates every binding that reads the row,
  // including static full-screen backgrounds. Only notify real changes.
  for (size_t i = 0; i < frame.size(); i++) {
    const auto &cmd = frame[i];
    GamePosition pos{};
    pos.x = cmd.x;
    pos.y = cmd.y;
    pos.w = cmd.w;
    pos.h = cmd.h;
    if (i >= positions->row_count()) {
      positions->push_back(pos);
    }
    else if (positions->row_data(i) != pos) {
      positions->set_row_data(i, pos);
    }
    if (i < model->row_count()) {
      if (!same_style(*model->row_data(i), cmd)) {
        model->set_row_data(i, cmd);
      }
    }
    else {
      model->push_back(cmd);
    }
  }
  while (model->row_count() > frame.size())
    model->erase(model->row_count() - 1);
  while (positions->row_count() > frame.size())
    positions->erase(positions->row_count() - 1);
}
static void key(int kind) {
  slint::invoke_from_event_loop([kind]() {
    if (ui() && ui()->get_screen() == Screen::Game)
      handle_game_event(kind, 0, 0);
  });
}
extern "C" void game_refresh_catalog(void) {
  ui_operation_start(
      "Loading games...",
      []() {
        catalog_count = game_store_list(catalog, GAME_MAX_INSTALLED);
        return ESP_OK;
      },
      []() {
        // This list is tiny; insertion sort also avoids GCC's std::sort small-array warning.
        for (int i = 1; i < catalog_count; ++i) {
          for (int j = i; j > 0 && strcmp(catalog[j].title, catalog[j - 1].title) < 0; --j)
            std::swap(catalog[j], catalog[j - 1]);
        }
        auto entries = std::make_shared<slint::VectorModel<slint::SharedString>>();
        for (int i = 0; i < catalog_count; i++)
          entries->push_back(slint::SharedString(catalog[i].title));
        if (ui())
          ui()->set_installed_games(entries);
      });
}
extern "C" void handle_game_launch(int index) {
  if (index < 0 || index >= catalog_count || !ui())
    return;
  selected = index;
  ui()->set_screen(Screen::Game);
}
extern "C" void setup_game_properties(void) {
  if (!ui() || selected < 0 || selected >= catalog_count)
    return;
  exit_saved = false;
  active = catalog[selected];
  for (auto &image : textures)
    image = slint::Image();
  texture_bytes = 0;
  ui()->set_game_error("");
  frame.clear();
  frame.reserve(MAX_COMMANDS);
  model = std::make_shared<slint::VectorModel<GameDraw>>();
  positions = std::make_shared<slint::VectorModel<GamePosition>>();
  ui()->set_game_draw(model);
  ui()->set_game_positions(positions);
  best_score = 0;
  game_repeat = INPUT_ONCE;
  if (strlen(active.id) <= 11) {
    snprintf(score_key, sizeof(score_key), "%.11s_hi", active.id);
  }
  else {
    // NVS keys are limited to 15 bytes; don't alias IDs sharing a prefix.
    unsigned char digest[32];
    size_t digest_size = 0;
    if (psa_hash_compute(PSA_ALG_SHA_256, (const unsigned char *)active.id, strlen(active.id), digest, sizeof(digest),
                         &digest_size) != PSA_SUCCESS ||
        digest_size != sizeof(digest)) {
      stop_vm("Could not compute game score key");
      return;
    }
    score_key[0] = 'g';
    for (int i = 0; i < 7; ++i)
      snprintf(score_key + 1 + i * 2, 3, "%02x", digest[i]);
  }
  struct Package {
    char *source = nullptr;
    size_t length = 0;
    ~Package() {
      free(source);
    }
  };
  auto package = std::make_shared<Package>();
  ui_operation_start(
      "Loading game...",
      [package]() {
        nvs_read_int(score_key, &best_score);
        package->source = game_store_read(active.id, &package->length);
        game_info_t info;
        return package->source && game_store_metadata(package->source, package->length, &info) &&
                       !strcmp(info.id, active.id)
                   ? ESP_OK
                   : ESP_ERR_INVALID_STATE;
      },
      [package]() {
        char *source = package->source;
        package->source = nullptr;
        start_game(source, package->length);
      });
}
static void start_game(char *source, size_t length) {
  allocated = 0;
#if LUA_VERSION_NUM >= 505
  vm = lua_newstate(lua_alloc, nullptr, esp_random());
#else
  vm = lua_newstate(lua_alloc, nullptr);
#endif
  if (!vm) {
    free(source);
    stop_vm("Not enough PSRAM to start game");
    return;
  }
  lua_pushcfunction(vm, bootstrap);
  if (!finish_call(0)) {
    free(source);
    return;
  }
  int status = luaL_loadbufferx(vm, source, length, active.id, "t");
  free(source);
  if (status != LUA_OK) {
    stop_vm("Game script could not be loaded");
    return;
  }
  if (!finish_call(0) || !function("init"))
    return;
  lua_pushinteger(vm, best_score);
  // Initialization decodes bounded package textures; frame callbacks retain 20 ms.
  if (!finish_call(1, 0, 250000))
    return;
  // Discard source/initialization temporaries before the frame budget applies.
  // Whack releases its compressed texture constants after decoding them.
  lua_gc(vm, LUA_GCCOLLECT);
  input_router_claim(INPUT_ACTION_STICK_UP, []() { key(1); }, INPUT_ONCE);
  input_router_claim(INPUT_ACTION_STICK_DOWN, []() { key(2); }, game_repeat);
  input_router_claim(INPUT_ACTION_STICK_LEFT, []() { key(3); }, game_repeat);
  input_router_claim(INPUT_ACTION_STICK_RIGHT, []() { key(4); }, game_repeat);
  // Keep activation on the press edge. Claiming double-press delays Return until
  // release + the click window, and turns rapid gameplay presses into an exit.
  last_tick = esp_timer_get_time();
  render();
}
extern "C" void handle_game_tick(void) {
  if (ui_processing_active() || !function("update"))
    return;
  int64_t now = esp_timer_get_time();
  int64_t ms = (now - last_tick) / 1000;
  last_tick = now;
  double dt = (ms < 0 || ms > 250 ? 33 : ms) / 1000.0;
  lua_pushnumber(vm, dt);
  lua_pushnumber(vm, remote_data.js_x);
  lua_pushnumber(vm, remote_data.js_y);
  lua_pushboolean(vm, ui() && ui()->get_joystick_supported());
  bool ok = finish_call(4, 1);
  if (ok) {
    bool changed = !lua_isboolean(vm, -1) || lua_toboolean(vm, -1);
    lua_pop(vm, 1);
    if (changed)
      render();
  }
}
extern "C" void handle_game_event(int kind, float x, float y) {
  reset_sleep_timer();
  if (ui_processing_active() || !function("event"))
    return;
  lua_pushinteger(vm, kind);
  lua_pushnumber(vm, x);
  lua_pushnumber(vm, y);
  if (finish_call(3, 1)) {
    bool changed = !lua_isboolean(vm, -1) || lua_toboolean(vm, -1);
    lua_pop(vm, 1);
    if (changed)
      render();
  }
}
extern "C" void teardown_game_properties(void) {
  stop_vm(nullptr);
  active = {};
  // Keep displayed rows through Slint's exit animation; VM memory is freed now.
}
extern "C" void handle_game_back(void) {
  slint::invoke_from_event_loop([]() {
    if (ui())
      ui()->set_screen(Screen::Games);
  });
}

extern "C" bool game_screen_prepare_exit(int target) {
  if (exit_saved || !active.id[0] || best_score == 0)
    return false;
  const std::string key = score_key;
  const uint32_t score = best_score;
  ui_operation_start(
      "Saving game score...",
      [key, score]() {
        uint32_t stored = 0;
        nvs_read_int(key.c_str(), &stored);
        return score > stored ? nvs_write_int(key.c_str(), score) : ESP_OK;
      },
      [target]() {
        exit_saved = true;
        ui()->set_screen(static_cast<Screen>(target));
      });
  return true;
}
