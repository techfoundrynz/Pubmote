#ifndef __INPUT_ROUTER_H
#define __INPUT_ROUTER_H
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Routes the remote inputs that Slint cannot model itself.
//
// The primary button is NOT here: a click is dispatched to the UI as a Return
// key, so a screen claims it the ordinary Slint way, with a FocusScope. What is
// left is the analog joystick, which needs hysteresis and auto-repeat that a key
// event cannot express, plus the button double-click, which has no key
// equivalent.
//
// Defaults are installed once at boot and reinstalled by the screen transition
// before the incoming screen's setup hook runs, so a screen only ever says what
// it handles - never has to remember to release it.
//
// Button down/up and long-press-hold are deliberately absent: long press is
// power off, and a screen able to claim it could ship a remote you cannot turn
// off. Those stay with power management via register_primary_button_cb.
typedef enum {
  INPUT_ACTION_DOUBLE_PRESS,
  INPUT_ACTION_STICK_UP,
  INPUT_ACTION_STICK_DOWN,
  INPUT_ACTION_STICK_LEFT,
  INPUT_ACTION_STICK_RIGHT,
  INPUT_ACTION_COUNT
} InputAction;

typedef void (*input_action_cb_t)(void);

// Keyboard-style auto-repeat while the stick is held: one fire on the edge, then
// one every interval_ms once delay_ms has passed. Both are per binding, not
// global, because menu scrolling and a falling tetromino want very different
// feel - a shared delay would force one to be wrong.
typedef struct {
  uint16_t delay_ms;    // wait before the first repeat
  uint16_t interval_ms; // 0 disables repeat: one fire per deflection
} InputRepeat;

static inline InputRepeat input_repeat(uint16_t delay_ms, uint16_t interval_ms) {
  InputRepeat r;
  r.delay_ms = delay_ms;
  r.interval_ms = interval_ms;
  return r;
}

// For actions that must not repeat - anything that toggles or navigates away.
#define INPUT_ONCE input_repeat(0, 0)

void input_router_set_default(InputAction action, input_action_cb_t cb, InputRepeat repeat);
void input_router_claim(InputAction action, input_action_cb_t cb, InputRepeat repeat);
void input_router_restore_defaults();

// True if a handler ran.
bool input_router_dispatch(InputAction action);

// Feeds joystick edge detection and auto-repeat. Call at a steady rate from a
// UI-priority task, never from the control input task.
void input_router_poll_stick(float x, float y);

// Claimed by the one screen that hands raw input to the board, and cleared on
// every screen change like any other claim - so no shared code needs to know
// which screen that is. The getter also accounts for the pocket-mode lock, so
// callers get one authoritative answer rather than having to remember it.
void input_router_claim_board_forwarding();
bool input_router_forwards_to_board();

#ifdef __cplusplus
}
#endif

#endif
