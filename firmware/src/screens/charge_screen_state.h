#pragma once
#include <stdint.h>

// UI task only. Blanking is distinct from dismissing the charge page.
class ChargeScreenIdleState {
  bool active = false;
  bool blanked = false;
  uint64_t visible_since = 0;

public:
  void enter(uint64_t now_ms) {
    active = true;
    blanked = false;
    visible_since = now_ms;
  }
  void exit() {
    active = false;
    blanked = false;
  }
  bool update(uint64_t now_ms) {
    if (!active || blanked || now_ms - visible_since < 30000)
      return false;
    blanked = true;
    return true;
  }
  bool wake(uint64_t now_ms) {
    if (!active || !blanked)
      return false;
    blanked = false;
    visible_since = now_ms;
    return true;
  }
  bool is_blanked() const {
    return active && blanked;
  }
  bool wake_for_event(uint64_t now_ms, uint64_t event_ms) {
    // Worker events can wait in the UI queue across a timeout or another wake.
    if (event_ms < visible_since + 30000)
      return false;
    return wake(now_ms);
  }
};

// One presentation per connection; dismissal lasts until unplugging.
class ChargeScreenState {
  bool connected = false;
  bool pending = false;
  bool initialized = false;
  bool power_off_on_unplug;

public:
  explicit ChargeScreenState(bool charger_wake = false) : power_off_on_unplug(charger_wake) {
  }
  void update(bool powered) {
    if (!initialized) {
      // USB already present during reset/button wake is the boot baseline,
      // not a new connection. Only a charger wake should open the screen.
      initialized = true;
      connected = powered;
      pending = powered && power_off_on_unplug;
      return;
    }
    if (!powered)
      pending = false;
    else if (!connected)
      pending = true;
    connected = powered;
  }
  bool should_show(bool board_control_active, bool boot_complete = true) const {
    return pending && !board_control_active && boot_complete;
  }
  bool should_power_off() const {
    return power_off_on_unplug && !connected;
  }
  void presented() {
    pending = false;
  }
  void dismiss() {
    pending = false;
    power_off_on_unplug = false;
  }
};
