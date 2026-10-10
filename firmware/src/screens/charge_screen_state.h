#pragma once

// One presentation per connection; dismissal lasts until unplugging.
class ChargeScreenState {
  bool connected = false;
  bool pending = false;
  bool power_off_on_unplug;

public:
  explicit ChargeScreenState(bool charger_wake = false) : power_off_on_unplug(charger_wake) {
  }
  void update(bool powered) {
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
