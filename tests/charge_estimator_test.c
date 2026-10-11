#include "charge/charge_estimator.h"
#include <assert.h>
#include <stdio.h>

static ChargeEstimateModel model;
static ChargeEstimateSession session;

static bool sample(uint64_t seconds, unsigned voltage, unsigned current, bool done) {
  return charge_estimator_update(&model, &session, seconds * 1000, voltage, current, true, !done, done, false);
}

static void cycle(uint64_t start, unsigned scale) {
  for (unsigned t = 0; t <= 300 * scale; ++t) {
    const unsigned phase = t / (60 * scale);
    const unsigned voltage = phase < 4 ? 3800 + phase * 100 : 4200;
    const unsigned current = phase < 2 ? 400 : phase == 2 ? 300 : phase == 3 ? 150 : 100;
    assert(!sample(start + t, voltage, current, false));
  }
  assert(sample(start + 300 * scale + 1, 4200, 0, true));
  assert(!session.remaining_seconds);
  assert(!sample(start + 300 * scale + 2, 4200, 0, true)); // Save only once.
}

static uint32_t estimate(uint64_t start, unsigned voltage, unsigned current) {
  charge_estimator_session_reset(&session);
  for (unsigned t = 0; t <= 20; ++t)
    assert(!sample(start + t, voltage, current, false));
  return session.remaining_seconds;
}

int main(void) {
  charge_estimator_model_reset(&model);
  charge_estimator_session_reset(&session);
  assert(charge_estimator_model_valid(&model));
  assert(estimate(0, 3800, 400) == 0); // No history: no estimate or placeholder.
  charge_estimator_session_reset(&session);
  cycle(100, 1);
  assert(model.generation == 1);
  assert(estimate(1000, 3800, 400) > 200 && session.remaining_seconds < 300);
  assert(estimate(2000, 4200, 100) > 0);   // Voltage says 100%, but not finished yet.
  assert(estimate(3000, 3300, 400) == 0);  // Unobserved part of the charge curve.
  assert(estimate(4000, 3800, 2000) == 0); // A substantially different charging rate.
  charge_estimator_session_reset(&session);
  sample(5000, 3800, 400, false);
  for (unsigned t = 1; t <= 14; ++t)
    sample(5000 + t, 3800, 400, false);
  assert(!session.remaining_seconds); // Let readings settle before showing history.
  sample(5015, 3800, 400, false);
  assert(session.remaining_seconds);
  assert(!charge_estimator_update(&model, &session, 5016000, 3800, 400, false, true, false, false));
  assert(!session.active && !session.remaining_seconds);
  sample(6000, 3800, 400, false);
  assert(!charge_estimator_update(&model, &session, 6001000, 3800, 400, true, true, false, true));
  assert(!session.active);
  sample(6100, 3800, 400, false);
  assert(!sample(6101, 3800, 0, false));
  assert(!session.active && !sample(6102, 4200, 0, true));
  assert(model.generation == 1); // Interruptions do not label unfinished charges.
  charge_estimator_session_reset(&session);
  sample(6200, 3800, 400, false);
  sample(6211, 3800, 400, false);
  assert(session.started_ms == 6211000); // A missing interval begins a new segment.
  charge_estimator_session_reset(&session);
  cycle(7000, 1);
  assert(model.generation == 2); // Every completed charge updates the model.
  const uint32_t old_estimate = estimate(8000, 3800, 400);
  // Seed an old range not covered by the replacement battery's charge.
  model.cells[0] = (ChargeEstimateCell){100, model.generation, 1, 0};
  charge_estimator_session_reset(&session);
  cycle(9000, 2); // A replacement takes twice as long at the same measured current.
  assert(model.generation == 1 && !model.cells[0].sessions);
  assert(estimate(10000, 3800, 400) > old_estimate * 3 / 2);
  // Repeated partial-to-full charges update the tail and retire untouched old ranges.
  for (unsigned n = 0; n < 9; ++n) {
    charge_estimator_session_reset(&session);
    for (unsigned t = 0; t <= 60; ++t)
      sample(11000 + n * 100 + t, 4200, 100, false);
    assert(sample(11000 + n * 100 + 61, 4200, 0, true));
  }
  assert(estimate(13000, 3800, 400) == 0);
  assert(estimate(14000, 4200, 100) > 0);
  assert(charge_estimator_model_valid(&model));
  model.version = 0;
  assert(!charge_estimator_model_valid(&model));
  charge_estimator_model_reset(&model);
  model.cells[0] = (ChargeEstimateCell){100, 1, 5, 0};
  assert(!charge_estimator_model_valid(&model));
  puts("charging estimator tests passed");
}
