#pragma once
#include <stdbool.h>

// Cached navigation state; safe to query without accessing the Slint window.
#ifdef __cplusplus
extern "C"
{
#endif
  bool is_about_screen_active(void);
  bool is_charge_screen_active(void);
  bool is_imu_calibration_screen_active(void);
  bool is_input_calibration_screen_active(void);
  bool is_menu_screen_active(void);
  bool is_pairing_screen_active(void);
  bool is_settings_screen_active(void);
  bool is_stats_screen_active(void);
  bool is_update_screen_active(void);
#ifdef __cplusplus
}
#endif
