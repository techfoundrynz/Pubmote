#include <atomic>
#include <cassert>

// Compile the production helpers after their standard headers.
#include "ota_gate_functions.inc"

int main() {
  assert(ota_request_cancel());
  ota_phase = OtaPhase::Downloading;
  assert(ota_request_cancel());
  assert(ota_phase == OtaPhase::Cancelled);
  assert(!ota_begin_commit(nullptr));
  ota_phase = OtaPhase::Downloading;
  assert(ota_begin_commit(nullptr));
  assert(ota_phase == OtaPhase::Committing);
  assert(!ota_request_cancel());
  assert(!ota_begin_commit(nullptr));
  ota_phase = OtaPhase::Idle;
  assert(ota_request_cancel());
}
