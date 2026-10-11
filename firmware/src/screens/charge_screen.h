#pragma once
#include "remote/imu.h"
#include "ui/screen_status.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

  void poll_charge_screen();
  void charge_screen_finish_boot();
  void setup_charge_properties();
  void teardown_charge_properties();
  // UI task only; preference refreshes must preserve a blanked charge page.
  bool charge_screen_display_blanked();
  void handle_charge_tapped();
  void handle_charge_imu_gesture(imu_gesture_t gesture);

#ifdef __cplusplus
}
#endif
