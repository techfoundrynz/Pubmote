#include "screens/whack_screen.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "slint_generated/app-window.h"
#include "remote/buzzer.h"
#include "remote/display.h"
#include "remote/haptic.h"
#include "remote/input_router.h"
#include "remote/powermanagement.h"
#include "remote/remoteinputs.h"
#include "remote/settings.h"
#include <math.h>
#include <memory>
#include <stdio.h>

static const char *TAG = "PUBREMOTE-WHACK_SCREEN";

#define STATE_READY 0
#define STATE_PLAYING 1
#define STATE_OVER 2

#define HOLE_IDLE 0
#define HOLE_RISING 1
#define HOLE_UP 2
#define HOLE_SINKING 3
#define HOLE_BONKED 4
#define HOLE_FLEEING 5

#define KIND_BAGUETTE 0
#define KIND_GOLDEN 1
#define KIND_CROISSANT 2
#define KIND_WINE 3
#define KIND_GOLD_WINE 4

#define HOLE_COUNT 5
#define START_LIVES 3
#define MAX_LIVES 5

// Pop fraction per second on the way up and down
#define RISE_RATE 5.0f
#define BONK_SINK_RATE 7.0f
#define BONK_HOLD_MS 180
// Anything less is still in the hole
#define HIT_POP 0.35f

// Some loaves bolt for the nearest edge instead of ducking back down, more often as the game goes
// on. Speeds in playfield units/s.
#define FLEE_PCT_START 8
#define FLEE_PCT_MAX 25
#define FLEE_PCT_HITS_PER_STEP 4
#define FLEE_SPEED 60.0f
// Still close enough to its hole for the mallet to reach
#define FLEE_HIT_RANGE 9.0f
// Past the panel edge from either hole column
#define FLEE_GONE 48.0f
#define FLEE_STEP_MS 90

// Difficulty follows loaves whacked rather than score, which the multiplier inflates. Timings ease
// from BASE toward MIN along base - (base - min) * hits / (hits + HALF), so they tighten quickly at
// first and level off rather than hitting a wall.
#define DIFFICULTY_HALF_HITS 40
#define UP_BASE_MS 1200
#define UP_MIN_MS 550
#define SPAWN_BASE_MS 1000
#define SPAWN_MIN_MS 420
#define SPAWN_JITTER_MS 300
#define SECOND_LOAF_HITS 15
#define THIRD_LOAF_HITS 45

// Croissants only show up once the player has the hang of it
#define CROISSANT_FROM_HITS 8
#define GOLDEN_PCT 8
#define CROISSANT_PCT 12

// A bottle of wine now and then while a life is missing: whack it for one back, ignore it for free.
// Very rarely it's a gold bottle worth three.
#define WINE_PCT 6
#define WINE_STYLES 4
#define GOLD_WINE_PCT 1
#define GOLD_WINE_LIVES 3
#define WINE_UP_MS 900

#define COMBO_STEP 5
#define COMBO_MAX_MULT 4
#define GOLDEN_POINTS 5

// Mallet poses, stepped by the tick rather than animated so each costs a single redraw
#define POSE_REST 0
#define POSE_HIT 1
#define POSE_WHIFF 2
#define POSE_RECOIL 3
#define MALLET_IMPACT_MS 90
#define MALLET_RECOIL_MS 70
#define POPUP_MS 500
// Keeps a flurry of whacks at game over from restarting straight away
#define RESTART_GUARD_US 700000
// Stick travel past a row boundary before the target moves, in rows
#define TARGET_HYSTERESIS 0.6f

// Beret colours for bread (the classic one weighted up) and wine styles for bottles, picked per
// spawn; whack.slint maps the index
#define BERET_COUNT 6
#define BERET_CLASSIC_PCT 40

// Plain baguettes come pale, golden or well done, and some wear a moustache
#define BAKE_COUNT 3
#define STACHE_PCT 30

// Popup colour: whack.slint maps these
#define TONE_GOOD 0
#define TONE_BAD 1
#define TONE_LIFE 2

// Words that sometimes stand in for a popup. Plain ASCII: the embedded fonts carry no accents.
static const char *const WORDS_HIT[] = {"OUI!", "BRAVO!"};
static const char *const WORDS_COMBO[] = {"MAGNIFIQUE!", "FANTASTIQUE!"};
static const char *const WORDS_SUNK[] = {"MISS", "TROP TARD!"};
static const char *const WORDS_RAN[] = {"ADIEU!", "AU REVOIR!", "BYE BYE!"};
static const char *const WORDS_CROISSANT[] = {"NON!", "MON DIEU!", "SACRE BLEU!"};
// Share of ordinary hits that show a word instead of the points
#define HIT_WORD_PCT 25

#define HIGH_SCORE_KEY "whack_hi"

static const BuzzerNote SFX_BONK[] = {{NOTE_C5, 40}, {NOTE_G4, 40}};
static const BuzzerNote SFX_GOLDEN[] = {{NOTE_E5, 50}, {NOTE_GS5, 50}, {NOTE_A5, 80}};
static const BuzzerNote SFX_WHIFF[] = {{NOTE_A3, 30}};
static const BuzzerNote SFX_CROISSANT[] = {{NOTE_ERROR, 160}};
static const BuzzerNote SFX_ESCAPE[] = {{NOTE_E4, 60}, {NOTE_C4, 90}};
static const BuzzerNote SFX_OVER[] = {{NOTE_E4, 120}, {NOTE_D4, 120}, {NOTE_C4, 240}};

typedef struct {
  int phase;
  int kind;
  float pop;
  int timer_ms;
  // Horizontal offset from the hole while fleeing, negative is left
  float run;
  int step_ms;
  int style;
  int bake;
  bool stache;
} Hole;

static Hole holes[HOLE_COUNT];

static int game_state = STATE_READY;
static int lives = START_LIVES;
static int combo = 0;
static int target = HOLE_COUNT / 2;
static int spawn_timer_ms = 0;
static int mallet_pose = POSE_REST;
static int mallet_timer_ms = 0;
static int popup_timer_ms = 0;
static uint32_t score = 0;
static uint32_t hits = 0;
static uint32_t high_score = 0;
static int64_t last_tick_us = 0;
static int64_t over_at_us = 0;

static std::shared_ptr<slint::VectorModel<WhackHole>> hole_model;

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

static int random_below(int n) {
  return (int)(esp_random() % (uint32_t)n);
}

#define PICK(words) words[random_below(sizeof(words) / sizeof(words[0]))]

static int eased(int base, int min) {
  return base - (int)((int64_t)(base - min) * hits / (hits + DIFFICULTY_HALF_HITS));
}

static int max_active() {
  return 1 + (hits >= SECOND_LOAF_HITS ? 1 : 0) + (hits >= THIRD_LOAF_HITS ? 1 : 0);
}

static int flee_pct() {
  const int pct = FLEE_PCT_START + (int)hits / FLEE_PCT_HITS_PER_STEP;
  return pct > FLEE_PCT_MAX ? FLEE_PCT_MAX : pct;
}

// Bread is what costs a life when it gets away
static bool is_bread(int kind) {
  return kind == KIND_BAGUETTE || kind == KIND_GOLDEN;
}

static int combo_mult() {
  const int mult = 1 + combo / COMBO_STEP;
  return mult > COMBO_MAX_MULT ? COMBO_MAX_MULT : mult;
}

static void publish_hole(int i) {
  if (!hole_model) {
    return;
  }
  WhackHole row;
  row.pop = holes[i].pop;
  row.kind = holes[i].kind;
  row.bonked = holes[i].phase == HOLE_BONKED;
  row.run = holes[i].run;
  row.step = holes[i].step_ms >= FLEE_STEP_MS / 2;
  row.style = holes[i].style;
  row.bake = holes[i].bake;
  row.stache = holes[i].stache;
  hole_model->set_row_data(i, row);
}

static void publish_holes() {
  for (int i = 0; i < HOLE_COUNT; i++) {
    publish_hole(i);
  }
}

static void publish_scores() {
  const UiState *state = ui();
  if (!state) {
    return;
  }
  char buf[16];
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)score);
  state->set_whack_score(slint::SharedString(buf));
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)high_score);
  state->set_whack_best(slint::SharedString(buf));
  state->set_whack_lives(lives);
  state->set_whack_mult(combo_mult());
}

static void publish_target() {
  const UiState *state = ui();
  if (state) {
    state->set_whack_target(target);
  }
}

static void set_mallet(int pose, int hold_ms) {
  mallet_pose = pose;
  mallet_timer_ms = hold_ms;
  const UiState *state = ui();
  if (state) {
    state->set_whack_mallet_pose(pose);
  }
}

static void show_popup(int row, const char *text, int tone) {
  const UiState *state = ui();
  if (!state) {
    return;
  }
  popup_timer_ms = POPUP_MS;
  state->set_whack_popup_row(row);
  state->set_whack_popup_tone(tone);
  state->set_whack_popup(slint::SharedString(text));
}

static void set_game_state(int new_state) {
  game_state = new_state;
  const UiState *state = ui();
  if (state) {
    state->set_whack_game_state(new_state);
  }
}

static void clear_board() {
  for (int i = 0; i < HOLE_COUNT; i++) {
    holes[i].phase = HOLE_IDLE;
    holes[i].kind = KIND_BAGUETTE;
    holes[i].pop = 0.0f;
    holes[i].timer_ms = 0;
    holes[i].run = 0.0f;
    holes[i].step_ms = 0;
  }
  lives = START_LIVES;
  combo = 0;
  score = 0;
  hits = 0;
  popup_timer_ms = 0;
  spawn_timer_ms = 500;
  set_mallet(POSE_REST, 0);
  show_popup(0, "", TONE_GOOD);
  publish_holes();
  publish_scores();
  publish_target();
}

static void reset_game() {
  clear_board();
  last_tick_us = esp_timer_get_time();
  set_game_state(STATE_PLAYING);
}

static void finish_game() {
  PLAY(SFX_OVER);
  if (score > high_score) {
    high_score = score;
    nvs_write_int(HIGH_SCORE_KEY, high_score);
    haptic_vibrate(HAPTIC_TRIPLE_CLICK);
  }
  else {
    haptic_vibrate(HAPTIC_STRONG_BUZZ);
  }
  set_mallet(POSE_REST, 0);
  publish_scores();
  over_at_us = esp_timer_get_time();
  set_game_state(STATE_OVER);
  ESP_LOGI(TAG, "Out of bread with score %lu", (unsigned long)score);
}

static void lose_life() {
  combo = 0;
  if (lives > 0) {
    lives--;
  }
  publish_scores();
  if (lives == 0) {
    finish_game();
  }
}

static void spawn() {
  int active = 0;
  int idle[HOLE_COUNT];
  int idle_count = 0;
  for (int i = 0; i < HOLE_COUNT; i++) {
    if (holes[i].phase == HOLE_IDLE) {
      idle[idle_count++] = i;
    }
    else if (holes[i].phase != HOLE_BONKED) {
      active++;
    }
  }
  if (idle_count == 0 || active >= max_active()) {
    return;
  }

  Hole *h = &holes[idle[random_below(idle_count)]];
  const int roll = random_below(100);
  if (lives < MAX_LIVES && roll < GOLD_WINE_PCT) {
    h->kind = KIND_GOLD_WINE;
  }
  else if (lives < MAX_LIVES && roll < WINE_PCT) {
    h->kind = KIND_WINE;
  }
  else if (roll < WINE_PCT + GOLDEN_PCT) {
    h->kind = KIND_GOLDEN;
  }
  else if (hits >= CROISSANT_FROM_HITS && roll < WINE_PCT + GOLDEN_PCT + CROISSANT_PCT) {
    h->kind = KIND_CROISSANT;
  }
  else {
    h->kind = KIND_BAGUETTE;
  }
  if (h->kind == KIND_WINE) {
    h->style = random_below(WINE_STYLES);
  }
  else {
    h->style = random_below(100) < BERET_CLASSIC_PCT ? 0 : 1 + random_below(BERET_COUNT - 1);
  }
  h->bake = random_below(BAKE_COUNT);
  h->stache = random_below(100) < STACHE_PCT;
  h->phase = HOLE_RISING;
  h->pop = 0.0f;
  h->timer_ms = is_bread(h->kind) || h->kind == KIND_CROISSANT ? eased(UP_BASE_MS, UP_MIN_MS) : WINE_UP_MS;
}

// Left-column holes run left, right-column holes run right
static float flee_dir(int i) {
  return (i == 1 || i == 3) ? 1.0f : -1.0f;
}

static bool should_flee(const Hole *h) {
  if (!is_bread(h->kind)) {
    return false;
  }
  const int pct = h->kind == KIND_GOLDEN ? flee_pct() * 2 : flee_pct();
  return random_below(100) < pct;
}

static void gain_lives(int count) {
  lives += count;
  if (lives > MAX_LIVES) {
    lives = MAX_LIVES;
  }
  PLAY(SFX_GOLDEN);
  haptic_vibrate(HAPTIC_DOUBLE_CLICK);
  publish_scores();
}

// A baguette got away, by sinking or by running off the panel
static void escaped(int i, const char *label) {
  PLAY(SFX_ESCAPE);
  haptic_vibrate(HAPTIC_SOFT_BUZZ);
  show_popup(i, label, TONE_BAD);
  publish_hole(i);
  lose_life();
}

// Absolute: the stick's Y position picks the row, centred stick = middle hole
static void track_stick() {
  const UiState *state = ui();
  if (!state || !state->get_joystick_supported()) {
    return;
  }
  float y = remote_data.js_y;
  if (y > 1.0f) {
    y = 1.0f;
  }
  else if (y < -1.0f) {
    y = -1.0f;
  }
  // Positive js_y is up, and row 0 is the top hole
  const float pos = (1.0f - y) / 2.0f * (float)(HOLE_COUNT - 1);
  if (fabsf(pos - (float)target) > TARGET_HYSTERESIS) {
    int next = (int)lroundf(pos);
    if (next < 0) {
      next = 0;
    }
    else if (next >= HOLE_COUNT) {
      next = HOLE_COUNT - 1;
    }
    if (next != target) {
      target = next;
      publish_target();
    }
  }
}

extern "C" void handle_whack_tick() {
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

  track_stick();

  if (mallet_timer_ms > 0) {
    mallet_timer_ms -= dt_ms;
    if (mallet_timer_ms <= 0) {
      if (mallet_pose == POSE_RECOIL) {
        set_mallet(POSE_REST, 0);
      }
      else {
        set_mallet(POSE_RECOIL, MALLET_RECOIL_MS);
      }
    }
  }
  if (popup_timer_ms > 0) {
    popup_timer_ms -= dt_ms;
    if (popup_timer_ms <= 0) {
      show_popup(0, "", TONE_GOOD);
      popup_timer_ms = 0;
    }
  }

  for (int i = 0; i < HOLE_COUNT; i++) {
    Hole *h = &holes[i];
    switch (h->phase) {
    case HOLE_RISING:
      h->pop += RISE_RATE * dt;
      if (h->pop >= 1.0f) {
        h->pop = 1.0f;
        h->phase = HOLE_UP;
      }
      break;
    case HOLE_UP:
      h->timer_ms -= dt_ms;
      if (h->timer_ms <= 0) {
        if (should_flee(h)) {
          h->phase = HOLE_FLEEING;
          h->run = 0.0f;
          h->step_ms = 0;
        }
        else {
          h->phase = HOLE_SINKING;
        }
      }
      // Nothing visible changed, so skip the model update and the redraw it would cause
      continue;
    case HOLE_FLEEING:
      h->run += flee_dir(i) * FLEE_SPEED * dt;
      h->step_ms = (h->step_ms + dt_ms) % FLEE_STEP_MS;
      if (fabsf(h->run) >= FLEE_GONE) {
        h->phase = HOLE_IDLE;
        h->pop = 0.0f;
        h->run = 0.0f;
        h->step_ms = 0;
        escaped(i, PICK(WORDS_RAN));
        if (game_state != STATE_PLAYING) {
          return;
        }
      }
      break;
    case HOLE_SINKING:
      h->pop -= RISE_RATE * dt;
      if (h->pop <= 0.0f) {
        h->pop = 0.0f;
        h->phase = HOLE_IDLE;
        if (is_bread(h->kind)) {
          escaped(i, PICK(WORDS_SUNK));
          if (game_state != STATE_PLAYING) {
            return;
          }
        }
      }
      break;
    case HOLE_BONKED:
      if (h->timer_ms > 0) {
        h->timer_ms -= dt_ms;
        continue;
      }
      h->pop -= BONK_SINK_RATE * dt;
      if (h->pop <= 0.0f) {
        h->pop = 0.0f;
        h->run = 0.0f;
        h->phase = HOLE_IDLE;
      }
      break;
    default:
      continue;
    }
    publish_hole(i);
  }

  spawn_timer_ms -= dt_ms;
  if (spawn_timer_ms <= 0) {
    spawn();
    spawn_timer_ms = eased(SPAWN_BASE_MS, SPAWN_MIN_MS) + random_below(SPAWN_JITTER_MS);
  }
}

// The button whacks whichever hole the stick is aiming at
extern "C" void handle_whack_hit() {
  reset_sleep_timer();
  if (game_state != STATE_PLAYING) {
    if (game_state == STATE_READY || esp_timer_get_time() - over_at_us > RESTART_GUARD_US) {
      reset_game();
    }
    return;
  }

  Hole *h = &holes[target];
  const bool hittable =
      ((h->phase == HOLE_RISING || h->phase == HOLE_UP || h->phase == HOLE_SINKING) && h->pop >= HIT_POP) ||
      (h->phase == HOLE_FLEEING && fabsf(h->run) < FLEE_HIT_RANGE);
  set_mallet(hittable ? POSE_HIT : POSE_WHIFF, MALLET_IMPACT_MS);
  if (!hittable) {
    combo = 0;
    PLAY(SFX_WHIFF);
    haptic_vibrate(HAPTIC_SOFT_BUMP);
    publish_scores();
    return;
  }

  h->phase = HOLE_BONKED;
  h->timer_ms = BONK_HOLD_MS;
  h->step_ms = 0;
  publish_hole(target);

  if (h->kind == KIND_CROISSANT) {
    PLAY(SFX_CROISSANT);
    haptic_vibrate(HAPTIC_STRONG_BUZZ);
    show_popup(target, PICK(WORDS_CROISSANT), TONE_BAD);
    lose_life();
    return;
  }

  if (h->kind == KIND_WINE) {
    show_popup(target, "+1 LIFE", TONE_LIFE);
    gain_lives(1);
    return;
  }
  if (h->kind == KIND_GOLD_WINE) {
    show_popup(target, "+3 LIVES", TONE_LIFE);
    gain_lives(GOLD_WINE_LIVES);
    return;
  }

  hits++;
  combo++;
  // Reaching the top multiplier is worth a life too
  const bool max_combo = combo == COMBO_STEP * (COMBO_MAX_MULT - 1);
  const int mult = combo_mult();
  const uint32_t points = (uint32_t)((h->kind == KIND_GOLDEN ? GOLDEN_POINTS : 1) * mult);
  score += points;

  // Gold always shows its points; combo milestones and some ordinary hits get a word instead
  if (h->kind != KIND_GOLDEN && combo % COMBO_STEP == 0) {
    show_popup(target, PICK(WORDS_COMBO), TONE_GOOD);
  }
  else if (h->kind != KIND_GOLDEN && random_below(100) < HIT_WORD_PCT) {
    show_popup(target, PICK(WORDS_HIT), TONE_GOOD);
  }
  else {
    char buf[8];
    snprintf(buf, sizeof(buf), "+%lu", (unsigned long)points);
    show_popup(target, buf, TONE_GOOD);
  }

  if (h->kind == KIND_GOLDEN) {
    PLAY(SFX_GOLDEN);
    haptic_vibrate(HAPTIC_DOUBLE_CLICK);
  }
  else if (combo % COMBO_STEP == 0) {
    PLAY(SFX_GOLDEN);
    haptic_vibrate(HAPTIC_TRIPLE_CLICK);
  }
  else {
    PLAY(SFX_BONK);
    haptic_vibrate(HAPTIC_SINGLE_CLICK);
  }
  if (max_combo && lives < MAX_LIVES) {
    show_popup(target, "+1 LIFE", TONE_LIFE);
    gain_lives(1);
    return;
  }
  publish_scores();
}

static void whack_stick_noop() {
}

extern "C" void handle_whack_back() {
  slint::invoke_from_event_loop([]() {
    if (get_slint_window()) {
      get_slint_window()->global<UiState>().set_screen(Screen::Games);
    }
  });
}

extern "C" uint32_t whack_high_score() {
  uint32_t stored = 0;
  if (nvs_read_int(HIGH_SCORE_KEY, &stored) == ESP_OK) {
    high_score = stored;
  }
  return high_score;
}

extern "C" void setup_whack_properties() {
  const UiState *state = ui();
  if (!state) {
    return;
  }

  whack_high_score();

  if (!hole_model) {
    std::vector<WhackHole> rows(HOLE_COUNT);
    hole_model = std::make_shared<slint::VectorModel<WhackHole>>(rows);
  }
  state->set_whack_holes(hole_model);

  target = HOLE_COUNT / 2;
  clear_board();
  set_game_state(STATE_READY);

  // The stick targets by position in the tick, so its edges just need to stay away from focus nav
  input_router_claim(INPUT_ACTION_STICK_UP, whack_stick_noop, INPUT_ONCE);
  input_router_claim(INPUT_ACTION_STICK_DOWN, whack_stick_noop, INPUT_ONCE);
  input_router_claim(INPUT_ACTION_STICK_LEFT, whack_stick_noop, INPUT_ONCE);
  input_router_claim(INPUT_ACTION_STICK_RIGHT, whack_stick_noop, INPUT_ONCE);

  ESP_LOGI(TAG, "Whack ready, best %lu", (unsigned long)high_score);
}

extern "C" void teardown_whack_properties() {
  buzzer_stop();
  set_game_state(STATE_READY);
}
