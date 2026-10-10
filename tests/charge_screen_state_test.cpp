#include "screens/charge_screen_state.h"
#include <cassert>

int main() {
  ChargeScreenState screen;
  screen.update(false);
  assert(!screen.should_show(false));
  screen.update(true); // Plug in, or boot already connected.
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
  assert(boot.should_show(false));

  ChargeScreenState controlling;
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
}
