#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
  void pairing_ui_secret_received(uint32_t secret);
  void pairing_ui_completed(void);
  void pairing_ui_incompatible_receiver(uint8_t version, uint8_t minimum);
#ifdef __cplusplus
}
#endif
