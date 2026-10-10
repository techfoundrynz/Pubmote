#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

  void acc1_power_set_level(bool enable);
  void acc2_power_set_level(uint8_t level);
  void reset_sleep_timer();
  void power_management_preinit();
  void power_management_init();
  bool power_management_is_power_connected();
  bool power_management_woke_for_charging();
  void enter_sleep();

  typedef enum {
    CHARGING,
    ON,
    OFF,
  } PowerState;

#ifdef __cplusplus
}
#endif
