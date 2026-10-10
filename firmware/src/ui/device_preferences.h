#pragma once
#include "remote/settings_types.h"
#ifdef __cplusplus
extern "C"
{
#endif
  const char *ui_hbm_mode_label(HbmModeOptions mode);
  void ui_set_input_support(bool supported);
  void ui_apply_theme(void);
  void ui_refresh_device_settings(const DeviceSettings *previous);
#ifdef __cplusplus
}
#endif
