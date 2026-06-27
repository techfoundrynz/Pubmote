#ifndef __ESPNOW_H
#define __ESPNOW_H

#include "comms.h"

#ifdef __cplusplus
extern "C" {
#endif

// Compatibility definitions for the old ESP-NOW header
typedef comms_event_t esp_now_event_t;

#define espnow_init comms_init
#define espnow_deinit comms_deinit
#define espnow_is_initialized comms_is_initialized
#define is_same_mac comms_is_same_mac

#ifdef __cplusplus
}
#endif

#endif
