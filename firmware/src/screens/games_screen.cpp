#include "screens/games_screen.h"
#include "remote/display.h"
#include "remote/haptic.h"
#include "screens/game_screen.h"
#include "slint_generated/app-window.h"
extern "C" void handle_open_games() {
  haptic_vibrate(HAPTIC_DOUBLE_CLICK);
  slint::invoke_from_event_loop([]() {
    if (get_slint_window())
      get_slint_window()->global<UiState>().set_screen(Screen::Games);
  });
}
extern "C" void handle_games_back() {
  slint::invoke_from_event_loop([]() {
    if (get_slint_window())
      get_slint_window()->global<UiState>().set_screen(Screen::About);
  });
}
extern "C" void setup_games_properties() {
  game_refresh_catalog();
}
