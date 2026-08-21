#include "screens/tetris_screen.h"
#include "config.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "generated/app-window.h"
#include "remote/display.h"
#include "remote/buzzer.h"
#include "remote/haptic.h"
#include "remote/input_router.h"
#include "remote/powermanagement.h"
#include "remote/remoteinputs.h"
#include "remote/settings.h"
#include <memory>
#include <stdio.h>
#include <string.h>

static const char *TAG = "PUBREMOTE-TETRIS_SCREEN";

#define COLS 10
#define ROWS 20
#define CELL_COUNT (COLS * ROWS)

#define STATE_READY 0
#define STATE_PLAYING 1
#define STATE_OVER 2

#define GHOST_OFFSET 10
#define FLASH_VALUE 9

#define DAS_DELAY_MS 170
#define DAS_REPEAT_MS 55
#define LOCK_DELAY_MS 500
#define MAX_LOCK_RESETS 15
#define FLASH_MS 130
#define BANNER_MS 1200

#define HIGH_SCORE_KEY "tetris_hi"

// Rows are nibbles, most significant first; within a nibble the most
// significant bit is the leftmost column.
static const uint16_t SHAPES[7][4] = {
    {0x0F00, 0x2222, 0x00F0, 0x4444}, // I
    {0x8E00, 0x6440, 0x0E20, 0x44C0}, // J
    {0x2E00, 0x4460, 0x0E80, 0xC440}, // L
    {0x6600, 0x6600, 0x6600, 0x6600}, // O
    {0x6C00, 0x4620, 0x06C0, 0x8C40}, // S
    {0x4E00, 0x4640, 0x0E40, 0x4C40}, // T
    {0xC600, 0x2640, 0x0C60, 0x4C80}, // Z
};

// Korobeiniki, the traditional Russian folk melody the game is associated with.
// Written out from the folk tune, which dates to the 1860s and is public domain.
// Structure is the conventional one: first strain twice, second strain twice,
// then the whole thing loops.
// One number sets the tempo; the note lengths derive from it.
#define BEAT_MS 400 // quarter note, ~150 BPM
#define Q BEAT_MS
#define E (BEAT_MS / 2)
#define DQ (BEAT_MS * 3 / 2)
#define H (BEAT_MS * 2)

#define STRAIN_ONE                                                                                                     \
  {NOTE_E5, Q}, {NOTE_B4, E}, {NOTE_C5, E}, {NOTE_D5, Q}, {NOTE_C5, E}, {NOTE_B4, E}, {NOTE_A4, Q}, {NOTE_A4, E},      \
      {NOTE_C5, E}, {NOTE_E5, Q}, {NOTE_D5, E}, {NOTE_C5, E}, {NOTE_B4, DQ}, {NOTE_C5, E}, {NOTE_D5, Q}, {NOTE_E5, Q}, \
      {NOTE_C5, Q}, {NOTE_A4, Q}, {NOTE_A4, Q}, {NOTE_REST, Q}, {NOTE_D5, DQ}, {NOTE_F5, E}, {NOTE_A5, Q},             \
      {NOTE_G5, E}, {NOTE_F5, E}, {NOTE_E5, DQ}, {NOTE_C5, E}, {NOTE_E5, Q}, {NOTE_D5, E}, {NOTE_C5, E},               \
      {NOTE_B4, Q}, {NOTE_B4, E}, {NOTE_C5, E}, {NOTE_D5, Q}, {NOTE_E5, Q}, {NOTE_C5, Q}, {NOTE_A4, Q},                \
      {NOTE_A4, Q}, {NOTE_REST, Q}

#define STRAIN_TWO                                                                                                     \
  {NOTE_E5, H}, {NOTE_C5, H}, {NOTE_D5, H}, {NOTE_B4, H}, {NOTE_C5, H}, {NOTE_A4, H}, {NOTE_GS4, H}, {NOTE_B4, H},     \
      {NOTE_E5, H}, {NOTE_C5, H}, {NOTE_D5, H}, {NOTE_B4, H}, {NOTE_C5, Q}, {NOTE_E5, Q}, {NOTE_A5, H},                \
      {NOTE_GS5, H}, {NOTE_REST, Q}

static const BuzzerNote MUSIC[] = {
    STRAIN_ONE, STRAIN_ONE, STRAIN_TWO, STRAIN_TWO,
};

#undef BEAT_MS
#undef Q
#undef E
#undef DQ
#undef H
#undef STRAIN_ONE
#undef STRAIN_TWO

static const uint16_t GRAVITY_MS[] = {800, 720, 630, 550, 470, 380, 300, 220, 130, 100,
                                      80,  80,  80,  70,  70,  70,  50,  50,  50,  30};
#define MAX_LEVEL_INDEX ((int)(sizeof(GRAVITY_MS) / sizeof(GRAVITY_MS[0])) - 1)

static uint8_t board[ROWS][COLS];
static uint8_t view[CELL_COUNT];
static uint8_t last_view[CELL_COUNT];

static int game_state = STATE_READY;
static int piece_type = 0;
static int piece_rot = 0;
static int piece_x = 3;
static int piece_y = 0;
static int next_type = 0;
static int hold_type = -1;
static bool hold_used = false;

static uint8_t bag[7];
static int bag_pos = 7;

static uint32_t score = 0;
static uint32_t high_score = 0;
static uint32_t total_lines = 0;
static int level = 0;

static int gravity_timer = 0;
static int lock_timer = 0;
static int lock_resets = 0;
static bool resting = false;
static int flash_timer = 0;
static uint32_t flash_rows_mask = 0;
static int banner_timer = 0;

static int held_zone = -1;
static int das_timer = 0;

static int64_t last_tick_us = 0;

static std::shared_ptr<slint::VectorModel<int>> cells_model;
static std::shared_ptr<slint::VectorModel<int>> next_model;

static const UiState *ui() {
  AppWindow *window = get_slint_window();
  return window ? &window->global<UiState>() : nullptr;
}

static inline bool shape_bit(int type, int rot, int row, int col) {
  return (SHAPES[type][rot & 3] & (0x8000 >> (row * 4 + col))) != 0;
}

static bool collides(int type, int rot, int ox, int oy) {
  for (int row = 0; row < 4; row++) {
    for (int col = 0; col < 4; col++) {
      if (!shape_bit(type, rot, row, col)) {
        continue;
      }
      int x = ox + col;
      int y = oy + row;
      if (x < 0 || x >= COLS || y >= ROWS) {
        return true;
      }
      if (y >= 0 && board[y][x] != 0) {
        return true;
      }
    }
  }
  return false;
}

static void refill_bag() {
  for (int i = 0; i < 7; i++) {
    bag[i] = i;
  }
  for (int i = 6; i > 0; i--) {
    int j = (int)(esp_random() % (uint32_t)(i + 1));
    uint8_t tmp = bag[i];
    bag[i] = bag[j];
    bag[j] = tmp;
  }
  bag_pos = 0;
}

static int take_from_bag() {
  if (bag_pos >= 7) {
    refill_bag();
  }
  return bag[bag_pos++];
}

// The remote has one sound preference; treat "no startup sound" as "no sound".
static bool sound_enabled() {
  return device_settings.startup_sound != STARTUP_SOUND_DISABLED;
}

static void start_music() {
  if (sound_enabled()) {
    buzzer_play_sequence(MUSIC, sizeof(MUSIC) / sizeof(MUSIC[0]), true);
  }
}

static void stop_music() {
  buzzer_stop();
}

static void set_banner(const char *text) {
  const UiState *state = ui();
  if (state) {
    state->set_tetris_banner(slint::SharedString(text));
  }
  banner_timer = text[0] == '\0' ? 0 : BANNER_MS;
}

static void publish_next_preview(int type) {
  if (!next_model) {
    return;
  }
  for (int row = 0; row < 4; row++) {
    for (int col = 0; col < 4; col++) {
      int value = shape_bit(type, 0, row, col) ? type + 1 : 0;
      int index = row * 4 + col;
      if (next_model->row_data(index).value_or(-1) != value) {
        next_model->set_row_data(index, value);
      }
    }
  }
}

static void publish_stats() {
  const UiState *state = ui();
  if (!state) {
    return;
  }
  char buf[16];
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)score);
  state->set_tetris_score(slint::SharedString(buf));
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)high_score);
  state->set_tetris_best(slint::SharedString(buf));
  snprintf(buf, sizeof(buf), "%d", level + 1);
  state->set_tetris_level(slint::SharedString(buf));
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)total_lines);
  state->set_tetris_lines(slint::SharedString(buf));
}

static void set_game_state(int new_state) {
  game_state = new_state;
  const UiState *state = ui();
  if (state) {
    state->set_tetris_game_state(new_state);
  }
}

static int drop_distance() {
  int distance = 0;
  while (!collides(piece_type, piece_rot, piece_x, piece_y + distance + 1)) {
    distance++;
  }
  return distance;
}

static void render() {
  if (!cells_model) {
    return;
  }

  memset(view, 0, sizeof(view));

  for (int y = 0; y < ROWS; y++) {
    if (flash_timer > 0 && (flash_rows_mask & (1u << y))) {
      for (int x = 0; x < COLS; x++) {
        view[y * COLS + x] = FLASH_VALUE;
      }
      continue;
    }
    for (int x = 0; x < COLS; x++) {
      view[y * COLS + x] = board[y][x];
    }
  }

  if (game_state == STATE_PLAYING && flash_timer == 0) {
    int ghost_y = piece_y + drop_distance();
    for (int row = 0; row < 4; row++) {
      for (int col = 0; col < 4; col++) {
        if (!shape_bit(piece_type, piece_rot, row, col)) {
          continue;
        }
        int x = piece_x + col;
        int y = ghost_y + row;
        if (y >= 0 && y < ROWS && x >= 0 && x < COLS && view[y * COLS + x] == 0) {
          view[y * COLS + x] = piece_type + 1 + GHOST_OFFSET;
        }
      }
    }
    for (int row = 0; row < 4; row++) {
      for (int col = 0; col < 4; col++) {
        if (!shape_bit(piece_type, piece_rot, row, col)) {
          continue;
        }
        int x = piece_x + col;
        int y = piece_y + row;
        if (y >= 0 && y < ROWS && x >= 0 && x < COLS) {
          view[y * COLS + x] = piece_type + 1;
        }
      }
    }
  }

  for (int i = 0; i < CELL_COUNT; i++) {
    if (view[i] != last_view[i]) {
      last_view[i] = view[i];
      cells_model->set_row_data(i, view[i]);
    }
  }
}

static void spawn(int type) {
  piece_type = type;
  piece_rot = 0;
  piece_x = 3;
  piece_y = 0;
  resting = false;
  lock_resets = 0;
  lock_timer = LOCK_DELAY_MS;
  gravity_timer = GRAVITY_MS[level];

  if (collides(piece_type, piece_rot, piece_x, piece_y)) {
    if (score > high_score) {
      high_score = score;
      nvs_write_int(HIGH_SCORE_KEY, high_score);
      set_banner("NEW BEST");
    }
    else {
      set_banner("");
    }
    publish_stats();
    set_game_state(STATE_OVER);
    stop_music();
    haptic_vibrate(HAPTIC_DOUBLE_CLICK);
    ESP_LOGI(TAG, "Game over with score %lu", (unsigned long)score);
    return;
  }
}

static void spawn_next() {
  int type = next_type;
  next_type = take_from_bag();
  publish_next_preview(next_type);
  hold_used = false;
  spawn(type);
}

static void lock_piece() {
  for (int row = 0; row < 4; row++) {
    for (int col = 0; col < 4; col++) {
      if (!shape_bit(piece_type, piece_rot, row, col)) {
        continue;
      }
      int x = piece_x + col;
      int y = piece_y + row;
      if (y >= 0 && y < ROWS && x >= 0 && x < COLS) {
        board[y][x] = piece_type + 1;
      }
    }
  }

  flash_rows_mask = 0;
  int cleared = 0;
  for (int y = 0; y < ROWS; y++) {
    bool full = true;
    for (int x = 0; x < COLS; x++) {
      if (board[y][x] == 0) {
        full = false;
        break;
      }
    }
    if (full) {
      flash_rows_mask |= (1u << y);
      cleared++;
    }
  }

  if (cleared > 0) {
    static const uint16_t LINE_SCORE[5] = {0, 100, 300, 500, 800};
    const int previous_level = level;
    score += (uint32_t)LINE_SCORE[cleared] * (uint32_t)(level + 1);
    total_lines += cleared;
    level = (int)(total_lines / 10);
    if (level > MAX_LEVEL_INDEX) {
      level = MAX_LEVEL_INDEX;
    }
    if (level != previous_level) {
      set_banner("LEVEL UP");
      haptic_vibrate(HAPTIC_TRIPLE_CLICK);
    }
    flash_timer = FLASH_MS;
    if (cleared == 4) {
      set_banner("TETRIS");
      haptic_vibrate(HAPTIC_STRONG_BUZZ);
    }
    else {
      haptic_vibrate(HAPTIC_SOFT_BUMP);
    }
    publish_stats();
  }
  else {
    spawn_next();
  }
}

static void collapse_rows() {
  int write_row = ROWS - 1;
  for (int y = ROWS - 1; y >= 0; y--) {
    if (flash_rows_mask & (1u << y)) {
      continue;
    }
    if (write_row != y) {
      memcpy(board[write_row], board[y], COLS);
    }
    write_row--;
  }
  for (int y = write_row; y >= 0; y--) {
    memset(board[y], 0, COLS);
  }
  flash_rows_mask = 0;
  spawn_next();
}

static bool move_piece(int dx, int dy) {
  if (collides(piece_type, piece_rot, piece_x + dx, piece_y + dy)) {
    return false;
  }
  piece_x += dx;
  piece_y += dy;
  if (resting && lock_resets < MAX_LOCK_RESETS) {
    lock_resets++;
    lock_timer = LOCK_DELAY_MS;
  }
  return true;
}

static bool rotate_piece() {
  static const int KICKS[6][2] = {{0, 0}, {-1, 0}, {1, 0}, {-2, 0}, {2, 0}, {0, -1}};
  int target = (piece_rot + 1) & 3;
  for (int i = 0; i < 6; i++) {
    int nx = piece_x + KICKS[i][0];
    int ny = piece_y + KICKS[i][1];
    if (!collides(piece_type, target, nx, ny)) {
      piece_rot = target;
      piece_x = nx;
      piece_y = ny;
      if (resting && lock_resets < MAX_LOCK_RESETS) {
        lock_resets++;
        lock_timer = LOCK_DELAY_MS;
      }
      return true;
    }
  }
  return false;
}

static void hard_drop() {
  int distance = drop_distance();
  if (distance > 0) {
    piece_y += distance;
    score += (uint32_t)distance * 2;
    publish_stats();
    haptic_vibrate(HAPTIC_SINGLE_CLICK);
  }
  lock_piece();
}

static void hold_piece() {
  if (hold_used) {
    return;
  }
  hold_used = true;
  int previous = hold_type;
  hold_type = piece_type;
  if (previous < 0) {
    spawn(next_type);
    next_type = take_from_bag();
    publish_next_preview(next_type);
  }
  else {
    spawn(previous);
  }
}

static void reset_game() {
  memset(board, 0, sizeof(board));
  memset(last_view, 0xFF, sizeof(last_view));
  score = 0;
  total_lines = 0;
  level = 0;
  hold_type = -1;
  hold_used = false;
  bag_pos = 7;
  flash_timer = 0;
  flash_rows_mask = 0;
  banner_timer = 0;
  held_zone = -1;
  das_timer = 0;
  next_type = take_from_bag();
  publish_next_preview(next_type);
  set_banner("");
  publish_stats();
  spawn_next();
  last_tick_us = esp_timer_get_time();
  set_game_state(STATE_PLAYING);
  start_music();
  render();
}

static void apply_horizontal(int zone) {
  move_piece(zone == 0 ? -1 : 1, 0);
}

extern "C" void handle_tetris_tick() {
  if (game_state != STATE_PLAYING) {
    return;
  }

  int64_t now = esp_timer_get_time();
  int dt_ms = (int)((now - last_tick_us) / 1000);
  last_tick_us = now;
  if (dt_ms < 0 || dt_ms > 250) {
    dt_ms = 33;
  }

  if (banner_timer > 0) {
    banner_timer -= dt_ms;
    if (banner_timer <= 0) {
      banner_timer = 0;
      const UiState *state = ui();
      if (state) {
        state->set_tetris_banner(slint::SharedString(""));
      }
    }
  }

  if (flash_timer > 0) {
    flash_timer -= dt_ms;
    if (flash_timer <= 0) {
      flash_timer = 0;
      collapse_rows();
    }
    render();
    return;
  }

  if (held_zone >= 0) {
    das_timer -= dt_ms;
    if (das_timer <= 0) {
      apply_horizontal(held_zone);
      das_timer = DAS_REPEAT_MS;
    }
  }

  if (game_state != STATE_PLAYING) {
    return;
  }

  const int interval = GRAVITY_MS[level];
  gravity_timer -= dt_ms;
  if (gravity_timer <= 0) {
    gravity_timer += interval;
    if (gravity_timer <= 0) {
      gravity_timer = interval;
    }
    if (!collides(piece_type, piece_rot, piece_x, piece_y + 1)) {
      piece_y++;
      resting = false;
    }
    else if (!resting) {
      resting = true;
      lock_timer = LOCK_DELAY_MS;
    }
  }

  if (collides(piece_type, piece_rot, piece_x, piece_y + 1)) {
    if (!resting) {
      resting = true;
      lock_timer = LOCK_DELAY_MS;
    }
    lock_timer -= dt_ms;
    if (lock_timer <= 0) {
      lock_piece();
    }
  }
  else {
    resting = false;
  }

  render();
}

// Touch only. A finger latches held_zone so the tick can repeat it, and the
// matching pointer-up clears it.
extern "C" void handle_tetris_press(int zone) {
  reset_sleep_timer();
  if (game_state != STATE_PLAYING) {
    reset_game();
    return;
  }
  if (zone != 0 && zone != 1) {
    return;
  }
  held_zone = zone;
  das_timer = DAS_DELAY_MS;
  apply_horizontal(zone);
  render();
}

// Stick only, and it must NOT latch held_zone: the router already repeats a held
// direction, and there is no stick release to clear the latch, so latching here
// slides the piece forever.
extern "C" void handle_tetris_shift(int dir) {
  reset_sleep_timer();
  if (game_state != STATE_PLAYING) {
    reset_game();
    return;
  }
  move_piece(dir, 0);
  render();
}

extern "C" void handle_tetris_soft_drop() {
  reset_sleep_timer();
  if (game_state != STATE_PLAYING) {
    reset_game();
    return;
  }
  if (move_piece(0, 1)) {
    score++;
    gravity_timer = GRAVITY_MS[level];
    publish_stats();
    render();
  }
}

extern "C" void handle_tetris_release() {
  held_zone = -1;
}

extern "C" void handle_tetris_rotate() {
  reset_sleep_timer();
  if (game_state != STATE_PLAYING) {
    reset_game();
    return;
  }
  rotate_piece();
  render();
}

extern "C" void handle_tetris_gesture(int kind) {
  reset_sleep_timer();
  held_zone = -1;
  if (game_state != STATE_PLAYING) {
    reset_game();
    return;
  }
  if (kind == 0) {
    hard_drop();
  }
  else {
    hold_piece();
  }
  render();
}

// Claimed handlers. The click arrives on the button task and the stick edges on
// the UI poll task, so both hop to the event loop before touching a model.
static void post_to_event_loop(void (*action)()) {
  slint::invoke_from_event_loop([action]() {
    if (is_tetris_screen_active()) {
      action();
    }
  });
}

static void tetris_rotate_action() {
  post_to_event_loop([]() { handle_tetris_rotate(); });
}

static void tetris_soft_drop() {
  post_to_event_loop([]() { handle_tetris_soft_drop(); });
}

static void tetris_stick_left() {
  post_to_event_loop([]() { handle_tetris_shift(-1); });
}

static void tetris_stick_right() {
  post_to_event_loop([]() { handle_tetris_shift(1); });
}

extern "C" void handle_tetris_back() {
  slint::invoke_from_event_loop([]() {
    if (get_slint_window()) {
      get_slint_window()->global<UiState>().set_screen(Screen::Games);
    }
  });
}

extern "C" uint32_t tetris_high_score() {
  uint32_t stored = 0;
  if (nvs_read_int(HIGH_SCORE_KEY, &stored) == ESP_OK) {
    high_score = stored;
  }
  return high_score;
}

extern "C" void setup_tetris_properties() {
  const UiState *state = ui();
  if (!state) {
    return;
  }

  tetris_high_score();

  if (!cells_model) {
    std::vector<int> empty_cells(CELL_COUNT, 0);
    cells_model = std::make_shared<slint::VectorModel<int>>(empty_cells);
  }
  if (!next_model) {
    std::vector<int> empty_next(16, 0);
    next_model = std::make_shared<slint::VectorModel<int>>(empty_next);
  }

  for (int i = 0; i < CELL_COUNT; i++) {
    cells_model->set_row_data(i, 0);
  }
  memset(last_view, 0, sizeof(last_view));
  memset(board, 0, sizeof(board));

  state->set_tetris_cells(cells_model);
  state->set_tetris_next(next_model);

  score = 0;
  total_lines = 0;
  level = 0;
  held_zone = -1;
  set_banner("");
  publish_stats();
  set_game_state(STATE_READY);

  // The click arrives as a Return key and is handled by the FocusScope in
  // tetris.slint, so only the stick is claimed here. Rotation must not repeat;
  // every held direction shares the numbers the touch zones use, so there is one
  // place to tune how a held input feels.
  const InputRepeat stick_repeat = input_repeat(DAS_DELAY_MS, DAS_REPEAT_MS);
  input_router_claim(INPUT_ACTION_STICK_UP, tetris_rotate_action, INPUT_ONCE);
  input_router_claim(INPUT_ACTION_STICK_DOWN, tetris_soft_drop, stick_repeat);
  input_router_claim(INPUT_ACTION_STICK_LEFT, tetris_stick_left, stick_repeat);
  input_router_claim(INPUT_ACTION_STICK_RIGHT, tetris_stick_right, stick_repeat);

  ESP_LOGI(TAG, "Tetris ready, best %lu (joystick %d, button %d)", (unsigned long)high_score,
           (int)input_pins_joystick_enabled(), (int)input_pins_button_enabled());
}

extern "C" void teardown_tetris_properties() {
  stop_music();
  // Claims are dropped by the screen transition, not here
  held_zone = -1;
  // Models stay allocated: clearing them here blanks the well mid slide-out.
  set_game_state(STATE_READY);
}
