#pragma once
#include "ui/screen_status.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

  void setup_pairing_properties();
  void handle_pairing_action();
  void teardown_pairing_properties();
  bool pairing_screen_prepare_exit(int destination);

#ifdef __cplusplus
}
#endif
