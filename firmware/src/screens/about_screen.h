#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

  bool is_about_screen_active();
  void setup_about_properties();
  void teardown_about_properties();

#ifdef __cplusplus
}
#endif
