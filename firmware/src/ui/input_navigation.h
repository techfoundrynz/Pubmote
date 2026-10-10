#pragma once
#include <stdbool.h>

// Input workers post copied key events to the UI event loop.
#ifdef __cplusplus
extern "C"
{
#endif
  void ui_dispatch_activate(void);
  void ui_dispatch_activate_edge(bool pressed);
#ifdef __cplusplus
}
#endif
