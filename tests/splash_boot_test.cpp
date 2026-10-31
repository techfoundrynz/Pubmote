#include <cassert>
#include <deque>
#include <functional>

enum class Screen { Splash, Stats, Charge, Menu };
struct UiState {
  mutable Screen screen = Screen::Splash;
  mutable bool ready = false;
  bool minimum_elapsed = false;
  Screen get_screen() const { return screen; }
  void set_screen(Screen value) const { screen = value; }
  bool get_boot_ready() const { return ready; }
  void set_boot_ready(bool value) const { ready = value; }
  bool get_splash_minimum_elapsed() const { return minimum_elapsed; }
};
static UiState state;
struct Window {
  template <typename T> const T &global() { return state; }
};
static Window window;
static Window *get_slint_window() { return &window; }
namespace slint {
static std::deque<std::function<void()>> pending;
static void invoke_from_event_loop(std::function<void()> callback) { pending.push_back(callback); }
}
static bool charger_pending;
static int charge_boot_calls;
static void charge_screen_finish_boot() {
  ++charge_boot_calls;
  if (charger_pending)
    state.set_screen(Screen::Charge);
}
#define ESP_LOGI(...) ((void)0)
extern "C" void ui_navigation_finish_boot(void);
#include "splash_exit.inc"
#include "splash_ready.inc"

static void dispatch() {
  while (!slint::pending.empty()) {
    auto callback = slint::pending.front();
    slint::pending.pop_front();
    callback();
  }
}
static void reset() {
  state = {};
  slint::pending.clear();
  charge_boot_calls = 0;
  charger_pending = false;
}
int main() {
  // A slow radio keeps the splash up after its minimum timer has fired.
  reset();
  state.minimum_elapsed = true;
  handle_splash_exit();
  dispatch();
  assert(state.screen == Screen::Splash && charge_boot_calls == 0);
  ui_navigation_finish_boot();
  assert(!state.ready); // Boot worker dispatches; it never mutates the UI directly.
  dispatch();
  assert(state.screen == Screen::Stats && charge_boot_calls == 1);

  // A fast radio and early taps still cannot bypass the visible minimum.
  reset();
  ui_navigation_finish_boot();
  handle_splash_exit();
  dispatch();
  assert(state.ready && state.screen == Screen::Splash && charge_boot_calls == 0);
  state.minimum_elapsed = true;
  handle_splash_exit();
  handle_splash_exit();
  dispatch();
  assert(state.screen == Screen::Stats && charge_boot_calls == 1);

  // Finishing boot preserves the charger routing and ignores later exit requests.
  reset();
  state.minimum_elapsed = true;
  charger_pending = true;
  ui_navigation_finish_boot();
  dispatch();
  assert(state.screen == Screen::Charge && charge_boot_calls == 1);
  handle_splash_exit();
  dispatch();
  assert(state.screen == Screen::Charge && charge_boot_calls == 1);

  reset();
  state.screen = Screen::Menu;
  state.minimum_elapsed = true;
  ui_navigation_finish_boot();
  dispatch();
  assert(state.screen == Screen::Menu && charge_boot_calls == 0);
}
