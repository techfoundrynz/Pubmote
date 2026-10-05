#pragma once
#include <esp_err.h>
#ifdef __cplusplus
extern "C"
{
#endif
  typedef void (*radio_session_callback_t)(esp_err_t result, void *context);
  typedef esp_err_t (*radio_session_work_t)(void *context);
  // Reserve the internal worker stack before starting the board transport.
  esp_err_t radio_session_init(void);
  // Callbacks run on the radio worker. End also recovers a partially failed begin.
  // The caller must finish its network worker before requesting end.
  esp_err_t radio_session_begin(radio_session_callback_t callback, void *context);
  esp_err_t radio_session_end(radio_session_callback_t callback, void *context);
  // Run via radio_session_run on the internal maintenance worker. Success
  // leaves board workers stopped for the caller to reboot; failure attempts recovery.
  esp_err_t radio_session_reset_settings(void);
  // Serialize maintenance work on the same internal stack (safe for NVS/flash).
  esp_err_t radio_session_run(radio_session_work_t work, radio_session_callback_t callback, void *context);
#ifdef __cplusplus
}
#endif
