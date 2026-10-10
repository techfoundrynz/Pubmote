#pragma once

#include "esp_err.h"
#include "remote/settings_types.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

  uint32_t display_get_frame_count(void);
  void display_init();
  void display_deinit();
  uint8_t display_get_bl_level();
  void display_set_bl_level(uint8_t level);
  void display_set_rotation(ScreenRotation rot);
  void display_off();
  bool display_get_hbm();
  void display_set_hbm(bool active);
  bool display_supports_hbm();

#ifdef __cplusplus
}
#endif
