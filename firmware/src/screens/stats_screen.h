#pragma once
#include "ui/screen_status.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

  void setup_stats_properties();
  void teardown_stats_properties();
  // UI-task preference refresh; preserves current Stats pose and mode.
  void stats_screen_refresh_brightness();

#ifdef __cplusplus
}
#endif
