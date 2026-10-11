#include "screens/charge_screen_state.h"
#include <cassert>

int main() {
  ChargeScreenIdleState idle;
  assert(!idle.update(30000)); // No timeout off the charge page.
  idle.enter(1000);
  assert(!idle.is_blanked());
  assert(!idle.wake(2000)); // A visible tap can still dismiss the page.
  assert(!idle.update(30999));
  assert(idle.update(31000));
  assert(idle.is_blanked());
  assert(!idle.wake_for_event(31050, 30999)); // Movement queued before blanking is stale.
  assert(!idle.update(32000));                // Blank once, not on every poll.
  assert(idle.wake_for_event(40000, 39950));  // Fresh movement wakes without dismissing.
  assert(!idle.is_blanked());
  assert(!idle.update(69999));
  assert(idle.update(70000));                 // Waking starts a fresh 30-second window.
  assert(!idle.wake_for_event(70050, 39950)); // An old queued movement cannot wake again.
  idle.exit();
  assert(!idle.is_blanked() && !idle.wake(71000));
  assert(!idle.update(100000));
  idle.enter(110000);
  assert(!idle.update(139999)); // Re-entry has a fresh timeout.
  assert(idle.update(140000));
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
  for (int i = 0; i < 10; ++i) {
    charger_wake.update(true);
    assert(!charger_wake.should_show(false, false)); // Keep the boot splash visible.
  }
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
