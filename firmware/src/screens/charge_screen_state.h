#pragma once

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
  bool should_show(bool board_control_active) const {
    return pending && !board_control_active;
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
