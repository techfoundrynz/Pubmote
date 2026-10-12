#include "input_router.h"
#include <assert.h>

static bool pocket;
static int up_calls;
static int64_t now;
int64_t esp_timer_get_time(void) { return now; }
bool is_pocket_mode_enabled(void) { return pocket; }
static void up(void) { ++up_calls; }

int main(void) {
  input_router_set_default(INPUT_ACTION_STICK_UP, up, input_repeat(750, 500));
  input_router_restore_defaults();
  input_router_poll_stick(0, 1);
  assert(up_calls == 1);
  now = 750000;
  input_router_poll_stick(0, 1);
  assert(up_calls == 2);

  // Returning to Stats while holding Up cannot forward the navigation input.
  input_router_restore_defaults();
  input_router_claim(INPUT_ACTION_STICK_UP, 0, INPUT_ONCE);
  input_router_claim_board_forwarding();
  input_router_poll_stick(0, 1);
  assert(!input_router_forwards_to_board());
  assert(up_calls == 2); // held stick never fires again on screen arrival
  input_router_poll_stick(0, 0.1f);
  assert(!input_router_forwards_to_board()); // below UI threshold isn't neutral
  input_router_poll_stick(0.1f, 0);
  assert(!input_router_forwards_to_board()); // both calibrated axes must be zero
  input_router_poll_stick(0, -1);
  assert(!input_router_forwards_to_board());
  input_router_poll_stick(0, 0);
  assert(input_router_forwards_to_board());
  input_router_poll_stick(0, 1);
  assert(input_router_forwards_to_board()); // later riding input is forwarded
  pocket = true;
  assert(!input_router_forwards_to_board());
  pocket = false;
  assert(input_router_forwards_to_board());

  input_router_restore_defaults();
  input_router_poll_stick(0, 0);
  assert(!input_router_forwards_to_board()); // neutral alone cannot claim Stats
  input_router_claim_board_forwarding();
  assert(!input_router_forwards_to_board()); // each claim requires a fresh sample
  input_router_poll_stick(0, 0);
  assert(input_router_forwards_to_board());
  return 0;
}
