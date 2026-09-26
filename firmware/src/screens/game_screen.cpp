#include "game_screen.h"
#include "games/game_store.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "mbedtls/sha256.h"
#include "remote/buzzer.h"
#include "remote/display.h"
#include "remote/haptic.h"
#include "remote/input_router.h"
#include "remote/powermanagement.h"
#include "remote/remoteinputs.h"
#include "remote/settings.h"
#include "slint_generated/app-window.h"
extern "C" {
#include "lua.h"
#include "lauxlib.h"
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
static InputRepeat game_repeat;
static std::vector<GameDraw> frame;
static std::shared_ptr<slint::VectorModel<GameDraw>> model;
static const UiState *ui() {
  auto *window = get_slint_window();
  return window ? &window->global<UiState>() : nullptr;
}
static void *lua_alloc(void *, void *ptr, size_t old_size, size_t size) {
  if (!ptr) old_size = 0;
  if (!size) { free(ptr); allocated -= old_size; return nullptr; }
  if (size > LUA_BUDGET || allocated - old_size > LUA_BUDGET - size) return nullptr;
  void *next = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (next) allocated = allocated - old_size + size;
  return next;
}
static void budget_hook(lua_State *L, lua_Debug *) {
  if (--hooks_left <= 0 || esp_timer_get_time() > deadline) luaL_error(L, "Game exceeded execution budget");
}
static void arm_budget(void) {
  hooks_left = 200; // 200,000 instructions per callback, also capped by wall time.
  deadline = esp_timer_get_time() + 20000;
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
  if (!drawing || frame.size() >= MAX_COMMANDS) return luaL_error(L, "Drawing budget exceeded");
  float x = coordinate(L, 1), y = coordinate(L, 2), w = coordinate(L, 3), h = coordinate(L, 4);
  int color = bounded_int(L, 5, 0, 0xffffff);
  float radius = lua_gettop(L) >= 6 ? coordinate(L, 6) : 0;
  luaL_argcheck(L, w >= 0 && h >= 0 && radius >= 0, 3, "negative size");
  GameDraw cmd{};
  cmd.x = x; cmd.y = y; cmd.w = w; cmd.h = h; cmd.radius = radius;
  cmd.color = slint::Color::from_rgb_uint8(color >> 16, color >> 8, color);
  frame.push_back(cmd);
  return 0;
}
static int draw_text(lua_State *L) {
  if (!drawing || frame.size() >= MAX_COMMANDS) return luaL_error(L, "Drawing budget exceeded");
  float x = coordinate(L, 1), y = coordinate(L, 2), w = coordinate(L, 3);
  size_t size;
  const char *text = luaL_checklstring(L, 4, &size);
  luaL_argcheck(L, size <= 96 && !memchr(text, 0, size), 4, "text too long");
  int color = bounded_int(L, 5, 0, 0xffffff);
  int font = bounded_int(L, 6, 8, 28);
  GameDraw cmd{};
  cmd.kind = 1; cmd.x = x; cmd.y = y; cmd.w = w; cmd.h = 8; cmd.font = font;
  cmd.text = slint::SharedString(text);
  cmd.color = slint::Color::from_rgb_uint8(color >> 16, color >> 8, color);
  frame.push_back(cmd);
  return 0;
}
static int tone(lua_State *L) {
  int hz = bounded_int(L, 1, 0, 4000), ms = bounded_int(L, 2, 0, 1000);
  if (device_settings.startup_sound != STARTUP_SOUND_DISABLED) buzzer_set_tone((BuzzerToneFrequency)hz, ms);
  return 0;
}
static int vibrate(lua_State *L) {
  bounded_int(L, 1, 0, 2);
  haptic_vibrate(lua_tointeger(L, 1) == 0 ? HAPTIC_SINGLE_CLICK : HAPTIC_DOUBLE_CLICK);
  return 0;
}
static int save_score(lua_State *L) {
  uint32_t value = bounded_int(L, 1, 0, 1000000000);
  // Writes happen once on exit, never in a game callback or frame loop.
  if (value > best_score) best_score = value;
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
  luaL_requiref(L, "_G", luaopen_base, 1); lua_pop(L, 1);
  const char *blocked[] = {"dofile", "loadfile", "load", "collectgarbage", "pcall", "xpcall", "print", "warn", "setmetatable", "getmetatable"};
  for (auto name : blocked) { lua_pushnil(L); lua_setglobal(L, name); }
  luaL_requiref(L, "math", luaopen_math, 1); lua_pop(L, 1);
  luaL_requiref(L, "table", luaopen_table, 1); lua_pop(L, 1);
  luaL_requiref(L, "string", luaopen_string, 1);
  lua_pushnil(L); lua_setfield(L, -2, "dump"); lua_pop(L, 1);
  lua_newtable(L);
  const luaL_Reg api[] = {{"rect", draw_rect}, {"text", draw_text}, {"tone", tone},
                         {"haptic", vibrate}, {"save_score", save_score}, {"repeat_input", repeat_input}, {nullptr, nullptr}};
  luaL_setfuncs(L, api, 0); lua_setglobal(L, "game");
  return 0;
}
static void stop_vm(const char *error) {
  if (ui() && error) ui()->set_game_error(slint::SharedString(error));
  if (vm) { lua_close(vm); vm = nullptr; }
  buzzer_stop();
  drawing = false;
}
static bool finish_call(int arguments, int results = 0) {
  arm_budget();
  if (lua_pcall(vm, arguments, results, 0) != LUA_OK) {
    const char *message = lua_type(vm, -1) == LUA_TSTRING ? lua_tostring(vm, -1) : "callback failed";
    char error[160]; snprintf(error, sizeof(error), "Game stopped: %.120s", message);
    stop_vm(error); return false;
  }
  return true;
}
static bool function(const char *name) {
  if (!vm) return false;
  lua_getglobal(vm, name);
  if (!lua_isfunction(vm, -1)) { stop_vm("Game is missing a required callback"); return false; }
  return true;
}
static void render(void) {
  if (!function("draw")) return;
  frame.clear(); drawing = true;
  bool ok = finish_call(0);
  drawing = false;
  if (!ok) return;
  // Reuse rows to avoid recreating all Slint items on every frame.
  for (size_t i = 0; i < frame.size(); i++) {
    if (i < model->row_count()) model->set_row_data(i, frame[i]);
    else model->push_back(frame[i]);
  }
  while (model->row_count() > frame.size()) model->erase(model->row_count() - 1);
}
static void key(int kind) {
  slint::invoke_from_event_loop([kind]() {
    if (ui() && ui()->get_screen() == Screen::Game) handle_game_event(kind, 0, 0);
  });
}
extern "C" void game_refresh_catalog(void) {
  catalog_count = game_store_list(catalog, GAME_MAX_INSTALLED);
  // This list is tiny; insertion sort also avoids GCC's std::sort small-array warning.
  for (int i = 1; i < catalog_count; ++i) {
    for (int j = i; j > 0 && strcmp(catalog[j].title, catalog[j - 1].title) < 0; --j)
      std::swap(catalog[j], catalog[j - 1]);
  }
  auto entries = std::make_shared<slint::VectorModel<slint::SharedString>>();
  for (int i = 0; i < catalog_count; i++) entries->push_back(slint::SharedString(catalog[i].title));
  if (ui()) ui()->set_installed_games(entries);
}
extern "C" void handle_game_launch(int index) {
  if (index < 0 || index >= catalog_count || !ui()) return;
  selected = index;
  ui()->set_screen(Screen::Game);
}
extern "C" void setup_game_properties(void) {
  if (!ui() || selected < 0 || selected >= catalog_count) return;
  active = catalog[selected];
  ui()->set_game_error("");
  frame.clear(); frame.reserve(MAX_COMMANDS);
  model = std::make_shared<slint::VectorModel<GameDraw>>();
  ui()->set_game_draw(model);
  best_score = 0;
  game_repeat = INPUT_ONCE;
  if (strlen(active.id) <= 11) {
    snprintf(score_key, sizeof(score_key), "%.11s_hi", active.id);
  } else {
    // NVS keys are limited to 15 bytes; don't alias IDs sharing a prefix.
    unsigned char digest[32];
    mbedtls_sha256((const unsigned char *)active.id, strlen(active.id), digest, 0);
    score_key[0] = 'g';
    for (int i = 0; i < 7; ++i) snprintf(score_key + 1 + i * 2, 3, "%02x", digest[i]);
  }
  nvs_read_int(score_key, &best_score);
  size_t length = 0;
  char *source = game_store_read(active.id, &length);
  game_info_t info;
  if (!source || !game_store_metadata(source, length, &info) || strcmp(info.id, active.id)) { free(source); stop_vm("Game package is missing or incompatible"); return; }
  allocated = 0;
#if LUA_VERSION_NUM >= 505
  vm = lua_newstate(lua_alloc, nullptr, esp_random());
#else
  vm = lua_newstate(lua_alloc, nullptr);
#endif
  if (!vm) { free(source); stop_vm("Not enough PSRAM to start game"); return; }
  lua_pushcfunction(vm, bootstrap);
  if (!finish_call(0)) { free(source); return; }
  int status = luaL_loadbufferx(vm, source, length, active.id, "t"); free(source);
  if (status != LUA_OK) { stop_vm("Game script could not be loaded"); return; }
  if (!finish_call(0) || !function("init")) return;
  lua_pushinteger(vm, best_score);
  if (!finish_call(1)) return;
  input_router_claim(INPUT_ACTION_STICK_UP, []() { key(1); }, game_repeat);
  input_router_claim(INPUT_ACTION_STICK_DOWN, []() { key(2); }, game_repeat);
  input_router_claim(INPUT_ACTION_STICK_LEFT, []() { key(3); }, game_repeat);
  input_router_claim(INPUT_ACTION_STICK_RIGHT, []() { key(4); }, game_repeat);
  input_router_claim(INPUT_ACTION_DOUBLE_PRESS, []() { handle_game_back(); }, INPUT_ONCE);
  last_tick = esp_timer_get_time();
  render();
}
extern "C" void handle_game_tick(void) {
  if (!function("update")) return;
  int64_t now = esp_timer_get_time();
  double dt = std::clamp((now - last_tick) / 1000000.0, 0.0, 0.1); last_tick = now;
  lua_pushnumber(vm, dt); lua_pushnumber(vm, remote_data.js_x); lua_pushnumber(vm, remote_data.js_y);
  lua_pushboolean(vm, ui() && ui()->get_joystick_supported());
  if (finish_call(4)) render();
}
extern "C" void handle_game_event(int kind, float x, float y) {
  reset_sleep_timer();
  if (!function("event")) return;
  lua_pushinteger(vm, kind); lua_pushnumber(vm, x); lua_pushnumber(vm, y);
  if (finish_call(3)) render();
}
extern "C" void teardown_game_properties(void) {
  if (active.id[0]) {
    uint32_t stored = 0; nvs_read_int(score_key, &stored);
    if (best_score > stored) nvs_write_int(score_key, best_score);
  }
  stop_vm(nullptr);
  active = {};
  // Keep displayed rows through Slint's exit animation; VM memory is freed now.
}
extern "C" void handle_game_back(void) {
  slint::invoke_from_event_loop([]() { if (ui()) ui()->set_screen(Screen::Games); });
}
