#include "screens/flappy_screen.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "generated/app-window.h"
#include "remote/buzzer.h"
#include "remote/display.h"
#include "remote/haptic.h"
#include "remote/input_router.h"
#include "remote/powermanagement.h"
#include "remote/settings.h"
#include <math.h>
#include <memory>
#include <stdio.h>

static const char *TAG = "PUBREMOTE-FLAPPY_SCREEN";

#define STATE_READY 0
#define STATE_PLAYING 1
#define STATE_OVER 2

// Playfield units. The whole game lives in a 100x100 space; flappy.slint maps
// that onto whatever square the panel can spare.
#define FIELD 100.0f
#define BIRD_X 26.0f
#define BIRD_RX 3.0f
#define BIRD_RY 2.6f
#define GROUND_H 14.0f
#define FLOOR_Y (FIELD - GROUND_H - BIRD_RY)
// Not BIRD_RY: on a round panel the top of the play band is behind the bezel at
// the bird's column, and a penguin the player cannot see is a penguin they lose.
#define CEILING_Y 7.0f
// Gap centres keep this clear of the ceiling so the band above never has to be used.
#define GAP_TOP_PAD 9.0f

// Scaled from the original game's 1080 px/s^2 and -400 px/s over its 512px
// screen. The flap arc, v^2/2g, is 13.8 units against 8.9 units of clearance
// either side of the bird in the narrowest gap - so a single flap overshoots the
// gap and has to be timed to coast through near its apex, which is the whole
// difficulty of the original. Anything under about 1.0 there lets the player
// hover and the game plays itself.
#define GRAVITY 210.0f
#define FLAP_VELOCITY (-76.0f)
#define MAX_FALL 110.0f
#define TILT_SPAN 80.0f

#define PILLAR_COUNT 3
#define PILLAR_SPACING 46.0f
#define PILLAR_W 12.0f
#define FIRST_PILLAR_X 104.0f

#define GAP_START 26.0f
#define GAP_MIN 23.0f
#define GAP_STEP 0.1f
#define SCROLL_BASE 32.0f
#define SCROLL_MAX 42.0f
#define SCROLL_STEP 0.3f

#define WING_MS 110
// Consecutive gaps are kept within reach of each other. Unbounded, the full
// range needs more climb than the scroll speed leaves time for.
#define GAP_Y_DELTA 20.0f

#define HIGH_SCORE_KEY "flappy_hi"

// buzzer_play_sequence starts the first note on the calling thread, so a
// sequence of one is how a game sound gets no latency.
static const BuzzerNote SFX_FLAP[] = {{NOTE_A4, 40}};
static const BuzzerNote SFX_SCORE[] = {{NOTE_E5, 60}, {NOTE_A5, 60}};
static const BuzzerNote SFX_CRASH[] = {{NOTE_ERROR, 200}};

typedef struct {
  float x;
  float gap_y;
  bool passed;
} Pillar;

static Pillar pillars[PILLAR_COUNT];

static int game_state = STATE_READY;
static bool dying = false;
static float bird_y = 50.0f;
static float velocity = 0.0f;
static float gap_h = GAP_START;
static float scroll = SCROLL_BASE;
static int wing_timer = 0;
static int published_wing = 1;
static uint32_t score = 0;
static uint32_t high_score = 0;
static float last_gap_y = 0.0f;
static int64_t last_tick_us = 0;

static std::shared_ptr<slint::VectorModel<IcePillar>> pillar_model;

static const UiState *ui() {
  AppWindow *window = get_slint_window();
  return window ? &window->global<UiState>() : nullptr;
}

static bool sound_enabled() {
  return device_settings.startup_sound != STARTUP_SOUND_DISABLED;
}

static void sfx(const BuzzerNote *notes, size_t count) {
  if (sound_enabled()) {
    buzzer_play_sequence(notes, count, false);
  }
}

#define PLAY(name) sfx(name, sizeof(name) / sizeof(name[0]))

static float random_unit() {
  return (float)(esp_random() % 10000u) / 10000.0f;
}

static float random_gap_y(float previous) {
  float low = gap_h / 2.0f + GAP_TOP_PAD;
  float high = FIELD - GROUND_H - gap_h / 2.0f - 5.0f;
  if (previous > 0.0f) {
    if (previous - GAP_Y_DELTA > low) {
      low = previous - GAP_Y_DELTA;
    }
    if (previous + GAP_Y_DELTA < high) {
      high = previous + GAP_Y_DELTA;
    }
  }
  return low + random_unit() * (high - low);
}

static void apply_difficulty() {
  gap_h = GAP_START - GAP_STEP * (float)score;
  if (gap_h < GAP_MIN) {
    gap_h = GAP_MIN;
  }
  scroll = SCROLL_BASE + SCROLL_STEP * (float)score;
  if (scroll > SCROLL_MAX) {
    scroll = SCROLL_MAX;
  }
  const UiState *state = ui();
  if (state) {
    state->set_flappy_gap(gap_h);
  }
}

static void publish_scores() {
  const UiState *state = ui();
  if (!state) {
    return;
  }
  char buf[16];
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)score);
  state->set_flappy_score(slint::SharedString(buf));
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)high_score);
  state->set_flappy_best(slint::SharedString(buf));
}

static void publish_pillars() {
  if (!pillar_model) {
    return;
  }
  for (int i = 0; i < PILLAR_COUNT; i++) {
    IcePillar row;
    row.x = pillars[i].x;
    row.gap_y = pillars[i].gap_y;
    pillar_model->set_row_data(i, row);
  }
}

static void publish_bird() {
  const UiState *state = ui();
  if (!state) {
    return;
  }
  state->set_flappy_bird_y(bird_y);

  float tilt = velocity / TILT_SPAN;
  if (tilt > 1.0f) {
    tilt = 1.0f;
  }
  else if (tilt < -1.0f) {
    tilt = -1.0f;
  }
  state->set_flappy_tilt(tilt);

  const int wing = wing_timer > 0 ? 2 : (velocity > 30.0f ? 0 : 1);
  if (wing != published_wing) {
    published_wing = wing;
    state->set_flappy_wing(wing);
  }
}

static void set_game_state(int new_state) {
  game_state = new_state;
  const UiState *state = ui();
  if (state) {
    state->set_flappy_game_state(new_state);
  }
}

static void reset_game() {
  dying = false;
  bird_y = 42.0f;
  velocity = 0.0f;
  wing_timer = 0;
  score = 0;
  apply_difficulty();
  last_gap_y = 0.0f;
  for (int i = 0; i < PILLAR_COUNT; i++) {
    pillars[i].x = FIRST_PILLAR_X + PILLAR_SPACING * (float)i;
    pillars[i].gap_y = random_gap_y(last_gap_y);
    last_gap_y = pillars[i].gap_y;
    pillars[i].passed = false;
  }
  publish_scores();
  publish_pillars();
  publish_bird();
  last_tick_us = esp_timer_get_time();
  set_game_state(STATE_PLAYING);
}

static void finish_game() {
  if (score > high_score) {
    high_score = score;
    nvs_write_int(HIGH_SCORE_KEY, high_score);
    haptic_vibrate(HAPTIC_TRIPLE_CLICK);
  }
  publish_scores();
  set_game_state(STATE_OVER);
  ESP_LOGI(TAG, "Splashed with score %lu", (unsigned long)score);
}

static void crash() {
  if (dying) {
    return;
  }
  dying = true;
  wing_timer = 0;
  PLAY(SFX_CRASH);
  haptic_vibrate(HAPTIC_STRONG_BUZZ);
}

static bool hits_pillar() {
  for (int i = 0; i < PILLAR_COUNT; i++) {
    if (fabsf(pillars[i].x - BIRD_X) > PILLAR_W / 2.0f + BIRD_RX) {
      continue;
    }
    const float gap_top = pillars[i].gap_y - gap_h / 2.0f;
    const float gap_bottom = pillars[i].gap_y + gap_h / 2.0f;
    if (bird_y - BIRD_RY < gap_top || bird_y + BIRD_RY > gap_bottom) {
      return true;
    }
  }
  return false;
}

extern "C" void handle_flappy_tick() {
  if (game_state != STATE_PLAYING) {
    return;
  }

  const int64_t now = esp_timer_get_time();
  int dt_ms = (int)((now - last_tick_us) / 1000);
  last_tick_us = now;
  if (dt_ms < 0 || dt_ms > 250) {
    dt_ms = 33;
  }
  const float dt = (float)dt_ms / 1000.0f;

  if (wing_timer > 0) {
    wing_timer -= dt_ms;
  }

  if (!dying) {
    for (int i = 0; i < PILLAR_COUNT; i++) {
      Pillar *p = &pillars[i];
      p->x -= scroll * dt;
      if (!p->passed && p->x + PILLAR_W / 2.0f < BIRD_X - BIRD_RX) {
        p->passed = true;
        score++;
        apply_difficulty();
        publish_scores();
        PLAY(SFX_SCORE);
        haptic_vibrate(HAPTIC_SINGLE_CLICK);
      }
      if (p->x < -(PILLAR_W / 2.0f + 1.0f)) {
        p->x += PILLAR_SPACING * (float)PILLAR_COUNT;
        p->gap_y = random_gap_y(last_gap_y);
        last_gap_y = p->gap_y;
        p->passed = false;
      }
    }
    publish_pillars();
  }

  velocity += GRAVITY * dt;
  if (velocity > MAX_FALL) {
    velocity = MAX_FALL;
  }
  bird_y += velocity * dt;

  if (bird_y < CEILING_Y) {
    bird_y = CEILING_Y;
    if (velocity < 0.0f) {
      velocity = 0.0f;
    }
  }

  if (bird_y >= FLOOR_Y) {
    bird_y = FLOOR_Y;
    velocity = 0.0f;
    crash();
    publish_bird();
    finish_game();
    return;
  }

  if (!dying && hits_pillar()) {
    crash();
  }

  publish_bird();
}

extern "C" void handle_flappy_flap() {
  reset_sleep_timer();
  if (game_state != STATE_PLAYING) {
    reset_game();
    return;
  }
  if (dying) {
    return;
  }
  velocity = FLAP_VELOCITY;
  wing_timer = WING_MS;
  PLAY(SFX_FLAP);
  publish_bird();
}

// The stick edge arrives on the UI poll task, so it hops to the event loop
// before touching a model.
static void flappy_stick_flap() {
  slint::invoke_from_event_loop([]() {
    if (is_flappy_screen_active()) {
      handle_flappy_flap();
    }
  });
}

extern "C" void handle_flappy_back() {
  slint::invoke_from_event_loop([]() {
    if (get_slint_window()) {
      get_slint_window()->global<UiState>().set_screen(Screen::Games);
    }
  });
}

extern "C" uint32_t flappy_high_score() {
  uint32_t stored = 0;
  if (nvs_read_int(HIGH_SCORE_KEY, &stored) == ESP_OK) {
    high_score = stored;
  }
  return high_score;
}

extern "C" void setup_flappy_properties() {
  const UiState *state = ui();
  if (!state) {
    return;
  }

  flappy_high_score();

  if (!pillar_model) {
    std::vector<IcePillar> rows;
    for (int i = 0; i < PILLAR_COUNT; i++) {
      IcePillar row;
      row.x = FIRST_PILLAR_X + PILLAR_SPACING * (float)i;
      row.gap_y = FIELD / 2.0f;
      rows.push_back(row);
    }
    pillar_model = std::make_shared<slint::VectorModel<IcePillar>>(rows);
  }
  state->set_flappy_pillars(pillar_model);

  dying = false;
  score = 0;
  bird_y = 42.0f;
  velocity = 0.0f;
  wing_timer = 0;
  published_wing = 1;
  state->set_flappy_wing(published_wing);
  apply_difficulty();
  last_gap_y = 0.0f;
  for (int i = 0; i < PILLAR_COUNT; i++) {
    pillars[i].x = FIRST_PILLAR_X + PILLAR_SPACING * (float)i;
    pillars[i].gap_y = random_gap_y(last_gap_y);
    last_gap_y = pillars[i].gap_y;
    pillars[i].passed = false;
  }
  publish_scores();
  publish_pillars();
  publish_bird();
  set_game_state(STATE_READY);

  // Any stick deflection flaps. The four directions are claimed rather than just
  // up so the default focus navigation cannot steal the Exit button mid-game.
  input_router_claim(INPUT_ACTION_STICK_UP, flappy_stick_flap, INPUT_ONCE);
  input_router_claim(INPUT_ACTION_STICK_DOWN, flappy_stick_flap, INPUT_ONCE);
  input_router_claim(INPUT_ACTION_STICK_LEFT, flappy_stick_flap, INPUT_ONCE);
  input_router_claim(INPUT_ACTION_STICK_RIGHT, flappy_stick_flap, INPUT_ONCE);

  ESP_LOGI(TAG, "Flappy ready, best %lu", (unsigned long)high_score);
}

extern "C" void teardown_flappy_properties() {
  buzzer_stop();
  set_game_state(STATE_READY);
}
