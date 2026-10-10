#pragma once
#include "ui/screen_status.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

  void poll_charge_screen();
  void handle_charge_tapped();

#ifdef __cplusplus
}
#endif
