#include "screens/garage_screen.h"
#include "games/garage_game.h"
#include "games/garage_music.h"
#include "esp_timer.h"
#include "generated/app-window.h"
#include "remote/buzzer.h"
#include "remote/display.h"
#include "remote/haptic.h"
#include "remote/input_router.h"
#include "remote/powermanagement.h"
#include "remote/settings.h"
#include <memory>
#include <stdio.h>

namespace {
garage::Game game;
uint32_t best = 0;
int64_t last_tick = 0;
int facing = 1;
slint::Timer music_timer;
size_t music_phrase = 0;
std::shared_ptr<slint::VectorModel<GarageObject>> model;
constexpr const char *score_key = "garage_hi";
const BuzzerNote jump_sound[] = {{NOTE_A4, 35}, {NOTE_E5, 45}};
const BuzzerNote pickup_sound[] = {{NOTE_E5, 40}, {NOTE_A5, 50}};
const BuzzerNote crash_sound[] = {{NOTE_ERROR, 160}};

void service_music() {
  if (!is_garage_screen_active() || device_settings.startup_sound == STARTUP_SOUND_DISABLED ||
      buzzer_sequence_playing()) return;
  constexpr size_t phrase_count = sizeof(garage::music) / sizeof(garage::music[0]);
  buzzer_play_sequence(garage::music[music_phrase], 6, false);
  music_phrase = (music_phrase + 1) % phrase_count;
}

void publish() {
  auto *window = get_slint_window();
  if (!window || !model) return;
  const auto &ui = window->global<UiState>();
  ui.set_garage_game_state(static_cast<int>(game.state));
  ui.set_garage_player_x(game.x - game.camera);
  ui.set_garage_player_y(game.y);
  if (game.vx > 0.1f) facing = 1;
  if (game.vx < -0.1f) facing = -1;
  ui.set_garage_facing(facing);
  ui.set_garage_airborne(!game.grounded);
  ui.set_garage_level(game.level + 1);
  ui.set_garage_pace(static_cast<int>(game.riding_speed() / 36.0f * 100));
  ui.set_garage_lives(game.lives);
  ui.set_garage_score(game.score);
  ui.set_garage_best(static_cast<int>(best));
  ui.set_garage_checkpoint(game.checkpoint);
  ui.set_garage_progress(std::min(1.0f, game.x / game.finish_x));
  ui.set_garage_stage_batteries(game.stage_batteries);
  ui.set_garage_stars(game.stars);
  ui.set_garage_total_stars(game.total_stars);
  ui.set_garage_combo(game.combo);
  ui.set_garage_lap(game.lap + 1);
  ui.set_garage_shield_time(static_cast<int>(std::ceil(game.shield)));
  ui.set_garage_magnet_time(static_cast<int>(std::ceil(game.magnet)));
  ui.set_garage_turbo_time(static_cast<int>(std::ceil(game.turbo)));
  ui.set_garage_protected(game.invulnerable > 0);
  size_t row_index = 0;
  for (size_t i = 0; i < game.count; ++i) {
    const auto &o = game.objects[i];
    const float px = o.x - game.camera;
    if (!o.active || px + o.w < 0 || px > 100) continue;
    GarageObject row;
    row.x = px;
    row.y = o.y;
    row.w = o.w;
    row.h = o.h;
    row.kind = static_cast<int>(o.kind);
    if (row_index < model->row_count()) {
      auto old = model->row_data(row_index);
      if (!old || old->x != row.x || old->y != row.y || old->w != row.w ||
          old->h != row.h || old->kind != row.kind) model->set_row_data(row_index, row);
    } else {
      model->push_back(row);
    }
    ++row_index;
  }
  while (model->row_count() > row_index) model->erase(model->row_count() - 1);
}

void feedback() {
  const unsigned events = game.events;
  if (events & (garage::Game::Crash | garage::Game::Finish | garage::Game::ExtraLife)) {
    if (game.score > static_cast<int>(best)) {
      best = game.score;
      nvs_write_int(score_key, best);
    }
    haptic_vibrate(events & garage::Game::Crash ? HAPTIC_STRONG_BUZZ : HAPTIC_TRIPLE_CLICK);
  } else if (events & (garage::Game::Powerup | garage::Game::Pickup | garage::Game::Stomp | garage::Game::Check)) {
    haptic_vibrate(HAPTIC_SINGLE_CLICK);
  }
  if (device_settings.startup_sound == STARTUP_SOUND_DISABLED) return;
  if (events & garage::Game::Crash) buzzer_play_sequence(crash_sound, 1, false);
  else if (events & (garage::Game::Pickup | garage::Game::Powerup | garage::Game::ExtraLife | garage::Game::Finish | garage::Game::Check | garage::Game::Stomp))
    buzzer_play_sequence(pickup_sound, 2, false);
  else if (events & garage::Game::Jump) buzzer_play_sequence(jump_sound, 2, false);
}

void stick_left() { slint::invoke_from_event_loop([] { if (is_garage_screen_active()) handle_garage_steer(-1); }); }
void stick_right() { slint::invoke_from_event_loop([] { if (is_garage_screen_active()) handle_garage_steer(1); }); }
void stick_down() { slint::invoke_from_event_loop([] { if (is_garage_screen_active()) handle_garage_steer(0); }); }
void stick_up() { slint::invoke_from_event_loop([] { if (is_garage_screen_active()) handle_garage_action(); }); }
}

extern "C" uint32_t garage_high_score() {
  uint32_t stored = 0;
  if (nvs_read_int(score_key, &stored) == ESP_OK) best = stored;
  return best;
}

extern "C" void setup_garage_properties() {
  if (!get_slint_window()) return;
  garage_high_score();
  game.reset();
  facing = 1;
  music_phrase = 0;
  music_timer.start(slint::TimerMode::Repeated, std::chrono::milliseconds(40), service_music);
  model = std::make_shared<slint::VectorModel<GarageObject>>();
  get_slint_window()->global<UiState>().set_garage_objects(model);
  last_tick = esp_timer_get_time();
  input_router_claim(INPUT_ACTION_STICK_UP, stick_up, INPUT_ONCE);
  input_router_claim(INPUT_ACTION_STICK_DOWN, stick_down, INPUT_ONCE);
  input_router_claim(INPUT_ACTION_STICK_LEFT, stick_left, INPUT_ONCE);
  input_router_claim(INPUT_ACTION_STICK_RIGHT, stick_right, INPUT_ONCE);
  publish();
}

extern "C" void teardown_garage_properties() {
  music_timer.stop();
  buzzer_stop();
  game.state = garage::State::Ready;
  if (get_slint_window()) get_slint_window()->global<UiState>().set_garage_game_state(0);
}

extern "C" void handle_garage_tick() {
  if (!is_garage_screen_active()) return;
  const int64_t now = esp_timer_get_time();
  game.advance(static_cast<float>(now - last_tick) / 1000000.0f);
  last_tick = now;
  feedback();
  publish();
}

extern "C" void handle_garage_action() {
  if (!is_garage_screen_active()) return;
  reset_sleep_timer();
  const bool resuming = game.state != garage::State::Playing;
  game.action();
  if (resuming) last_tick = esp_timer_get_time();
  publish();
}

extern "C" void handle_garage_steer(int direction) {
  if (!is_garage_screen_active()) return;
  reset_sleep_timer();
  if (game.state != garage::State::Playing) return;
  game.steer(direction);
}

extern "C" void handle_garage_back() {
  slint::invoke_from_event_loop([] {
    if (get_slint_window() && is_garage_screen_active())
      get_slint_window()->global<UiState>().set_screen(Screen::Games);
  });
}
