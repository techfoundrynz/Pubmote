#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

  // Up/down defaults to focus traversal; double-press defaults to Escape/back.
  // Clicks reach Slint as Return instead, and long press stays
  // with power management so no screen can take power-off. Screens claim in their setup hook;
  // defaults are restored on every screen change.
  typedef enum {
    INPUT_ACTION_DOUBLE_PRESS,
    INPUT_ACTION_STICK_UP,
    INPUT_ACTION_STICK_DOWN,
    INPUT_ACTION_STICK_LEFT,
    INPUT_ACTION_STICK_RIGHT,
    INPUT_ACTION_COUNT
  } InputAction;

  typedef void (*input_action_cb_t)(void);

  // Fires on the edge, then every interval_ms once delay_ms has passed
  typedef struct {
    uint16_t delay_ms;
    uint16_t interval_ms; // 0 = fire once per deflection
  } InputRepeat;

  static inline InputRepeat input_repeat(uint16_t delay_ms, uint16_t interval_ms) {
    InputRepeat r;
    r.delay_ms = delay_ms;
    r.interval_ms = interval_ms;
    return r;
  }

#define INPUT_ONCE input_repeat(0, 0)

  void input_router_set_default(InputAction action, input_action_cb_t cb, InputRepeat repeat);
  void input_router_claim(InputAction action, input_action_cb_t cb, InputRepeat repeat);
  void input_router_restore_defaults();

  // True if a handler ran.
  bool input_router_dispatch(InputAction action);
  bool input_router_is_claimed(InputAction action);

  // Call at a steady rate from a UI-priority task, never the control input task
  void input_router_poll_stick(float x, float y);

  // Cleared on every screen change; forwarding waits for a calibrated neutral
  // stick sample after the claim. The getter also honours pocket mode.
  void input_router_claim_board_forwarding();
  bool input_router_forwards_to_board();

#ifdef __cplusplus
}
#endif
