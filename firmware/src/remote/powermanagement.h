#pragma once
#include <esp_err.h>
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
  esp_err_t power_management_enter_shipping_mode();
  bool power_management_take_shipping_error();

  typedef enum {
    CHARGING,
    ON,
    OFF,
  } PowerState;

#ifdef __cplusplus
}
#endif
