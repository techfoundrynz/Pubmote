#include "screens/games_screen.h"
#include "esp_log.h"
#include "generated/app-window.h"
#include "remote/display.h"
#include "remote/haptic.h"
#include "screens/flappy_screen.h"
#include "screens/tetris_screen.h"
#include <stdio.h>

static const char *TAG = "PUBREMOTE-GAMES_SCREEN";

static void go_to(Screen screen) {
  slint::invoke_from_event_loop([screen]() {
    if (get_slint_window()) {
      get_slint_window()->global<UiState>().set_screen(screen);
    }
  });
}

extern "C" void handle_open_games() {
  ESP_LOGI(TAG, "Arcade unlocked");
  haptic_vibrate(HAPTIC_DOUBLE_CLICK);
  go_to(Screen::Games);
}

extern "C" void handle_games_tetris() {
  go_to(Screen::Tetris);
}

extern "C" void handle_games_flappy() {
  go_to(Screen::Flappy);
}

extern "C" void handle_games_back() {
  go_to(Screen::About);
}

extern "C" void setup_games_properties() {
  AppWindow *window = get_slint_window();
  if (!window) {
    return;
  }
  const auto &state = window->global<UiState>();

  char buf[16];
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)tetris_high_score());
  state.set_tetris_best(slint::SharedString(buf));
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)flappy_high_score());
  state.set_flappy_best(slint::SharedString(buf));
}
