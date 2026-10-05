#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C"
{
#endif
  bool wifi_screen_prepare_exit(int destination);
  void wifi_return_to_update(void);
  void setup_wifi_properties(void);
  void teardown_wifi_properties(void);
#ifdef __cplusplus
}
#endif
