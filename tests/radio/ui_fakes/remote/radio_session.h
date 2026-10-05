#pragma once
#include "esp_err.h"
typedef esp_err_t (*radio_session_work_t)(void *);
typedef void (*radio_session_callback_t)(esp_err_t, void *);
esp_err_t radio_session_run(radio_session_work_t, radio_session_callback_t, void *);
