#include "screens/charge_screen_state.h"
#include <cassert>

int main() {
  ChargeScreenState screen;
  screen.update(false);
  assert(!screen.should_show(false));
  screen.update(true); // Plug in after boot.
  assert(screen.should_show(false));
  screen.update(true); // Busy UI can defer presentation without losing the edge.
  assert(screen.should_show(false));
  screen.dismiss();
  for (int i = 0; i < 100; ++i) {
    screen.update(true);
    assert(!screen.should_show(false));
  }
  screen.update(false);
  assert(!screen.should_show(false));
  screen.update(true);
  assert(screen.should_show(false));
  screen.update(false); // An unplug cancels a deferred presentation.
  assert(!screen.should_show(false));
  screen.update(true);
  assert(screen.should_show(false));
  ChargeScreenState boot;
  boot.update(true);
  assert(!boot.should_show(false)); // USB used for flashing is already connected.
  for (int i = 0; i < 100; ++i) {
    boot.update(true);
    assert(!boot.should_show(false));
  }
  boot.update(false);
  assert(!boot.should_power_off());
  boot.update(true);
  assert(boot.should_show(false)); // A later plug-in still opens the screen.

  ChargeScreenState controlling;
  controlling.update(false);
  controlling.update(true);
  for (int i = 0; i < 100; ++i) {
    controlling.update(true);
    assert(!controlling.should_show(true)); // Never interrupt active board control.
  }
  assert(controlling.should_show(false)); // Presentation stays pending until control ends.
  controlling.dismiss();
  assert(!controlling.should_show(false));
  assert(!controlling.should_show(true));
  controlling.update(false);
  controlling.update(true);
  assert(!controlling.should_show(true));
  controlling.update(false); // Unplug cancels a request deferred during riding.
  assert(!controlling.should_show(false));

  ChargeScreenState charger_wake(true);
  charger_wake.update(true);
  assert(charger_wake.should_show(false));
  assert(!charger_wake.should_power_off());
  charger_wake.presented(); // Showing the screen is not a user dismissal.
  assert(!charger_wake.should_show(false));
  charger_wake.update(false);
  assert(charger_wake.should_power_off());

  ChargeScreenState taken_over(true);
  taken_over.update(true);
  taken_over.presented();
  taken_over.dismiss(); // A tap converts a charger wake into normal operation.
  taken_over.update(false);
  assert(!taken_over.should_power_off());
  taken_over.update(true);
  taken_over.presented();
  taken_over.update(false); // Later charging sessions must not restore auto-off.
  assert(!taken_over.should_power_off());

  ChargeScreenState button_wake;
  button_wake.update(true);
  assert(!button_wake.should_show(false));
  button_wake.update(false);
  assert(!button_wake.should_power_off());
  button_wake.update(true);
  assert(button_wake.should_show(false));

  ChargeScreenState charger_removed_before_poll(true);
  charger_removed_before_poll.update(false);
  assert(!charger_removed_before_poll.should_show(false));
  assert(charger_removed_before_poll.should_power_off());
}
