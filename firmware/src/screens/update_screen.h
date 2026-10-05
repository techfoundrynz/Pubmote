#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

  bool is_update_screen_active();
  bool update_screen_prepare_exit(int destination);
  void setup_update_properties();

#ifdef __cplusplus
}
#endif
