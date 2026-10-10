#pragma once

// One presentation per connection; dismissal lasts until unplugging.
class ChargeScreenState {
  bool connected = false;
  bool pending = false;

public:
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
  void dismiss() {
    pending = false;
  }
};
