#include "input_router.h"
#include "esp_timer.h"
#include "settings.h"
#include <string.h>

// Expo squashes the mid range of the axis, so engage early and release late
// rather than chattering around a single threshold.
#define STICK_ENGAGE 0.45f
#define STICK_RELEASE 0.2f

typedef struct {
  input_action_cb_t cb;
  InputRepeat repeat;
} Binding;

static Binding defaults[INPUT_ACTION_COUNT];
static Binding current[INPUT_ACTION_COUNT];
static bool forwards_to_board;

// Latched per axis so a held stick fires once and then repeats. The latch
// deliberately survives a screen change: holding the stick through a transition
// must not fire on the screen being entered.
static InputAction x_latch = INPUT_ACTION_COUNT;
static InputAction y_latch = INPUT_ACTION_COUNT;
static int64_t x_next_repeat_us;
static int64_t y_next_repeat_us;

void input_router_set_default(InputAction action, input_action_cb_t cb, InputRepeat repeat) {
  if (action >= INPUT_ACTION_COUNT) {
    return;
  }
  defaults[action].cb = cb;
  defaults[action].repeat = repeat;
  if (current[action].cb == NULL) {
    current[action] = defaults[action];
  }
}

void input_router_claim(InputAction action, input_action_cb_t cb, InputRepeat repeat) {
  if (action >= INPUT_ACTION_COUNT) {
    return;
  }
  current[action].cb = cb;
  current[action].repeat = repeat;
}

void input_router_restore_defaults() {
  memcpy(current, defaults, sizeof(current));
  forwards_to_board = false;
}

bool input_router_dispatch(InputAction action) {
  if (action >= INPUT_ACTION_COUNT || current[action].cb == NULL) {
    return false;
  }
  current[action].cb();
  return true;
}

void input_router_claim_board_forwarding() {
  forwards_to_board = true;
}

bool input_router_forwards_to_board() {
  // Pocket mode is a lock rather than a screen, but it means the same thing here
  return forwards_to_board && !is_pocket_mode_enabled();
}

// Which action a signed axis is currently asserting, with hysteresis against
// whatever it was asserting before.
static InputAction axis_action(float value, InputAction latch, InputAction negative, InputAction positive) {
  if (latch == positive) {
    return value > STICK_RELEASE ? positive : INPUT_ACTION_COUNT;
  }
  if (latch == negative) {
    return value < -STICK_RELEASE ? negative : INPUT_ACTION_COUNT;
  }
  if (value > STICK_ENGAGE) {
    return positive;
  }
  if (value < -STICK_ENGAGE) {
    return negative;
  }
  return INPUT_ACTION_COUNT;
}

static void drive_axis(float value, InputAction negative, InputAction positive, InputAction *latch,
                       int64_t *next_repeat_us, int64_t now_us) {
  InputAction action = axis_action(value, *latch, negative, positive);

  if (action != *latch) {
    *latch = action;
    if (action != INPUT_ACTION_COUNT && input_router_dispatch(action)) {
      *next_repeat_us = now_us + (int64_t)current[action].repeat.delay_ms * 1000;
    }
    return;
  }

  if (action == INPUT_ACTION_COUNT || current[action].repeat.interval_ms == 0) {
    return;
  }

  if (now_us >= *next_repeat_us) {
    input_router_dispatch(action);
    *next_repeat_us = now_us + (int64_t)current[action].repeat.interval_ms * 1000;
  }
}

// Axis signs are hardware convention, not arithmetic: on this remote a positive
// js_y is the DOWN gesture. Getting this backwards inverts every consumer at
// once, so check against a known-good mapping before changing it - the pre-router
// nav code sent Tab (focus down the list) on js_y > 0.7.
void input_router_poll_stick(float x, float y) {
  const int64_t now_us = esp_timer_get_time();
  drive_axis(x, INPUT_ACTION_STICK_LEFT, INPUT_ACTION_STICK_RIGHT, &x_latch, &x_next_repeat_us, now_us);
  drive_axis(y, INPUT_ACTION_STICK_UP, INPUT_ACTION_STICK_DOWN, &y_latch, &y_next_repeat_us, now_us);
}
