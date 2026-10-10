#pragma once
#include "ui/screen_status.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

  bool update_screen_prepare_exit(int destination);
  void setup_update_properties();

#ifdef __cplusplus
}
#endif
