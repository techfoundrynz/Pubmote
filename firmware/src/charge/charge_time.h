#pragma once
#include "charge/charge_driver.h"

#ifdef __cplusplus
extern "C"
{
#endif
  void charge_time_init();
  // Power-task owner; sampling does not depend on charge-screen visibility.
  void charge_time_sample(const RemotePowerState *power);
  // Thread-safe snapshot. Zero means no usable estimate, so the UI hides it.
  uint32_t charge_time_remaining_seconds();
  // Maintenance-worker owner, used to prevent writes after factory reset.
  void charge_time_set_persistence_enabled(bool enabled);
#ifdef __cplusplus
}
#endif
