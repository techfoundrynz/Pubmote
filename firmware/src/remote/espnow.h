#ifndef __ESPNOW_H
#define __ESPNOW_H

#include <esp_now.h>
#include <stdio.h>


#ifdef __cplusplus
extern "C" {
#endif


void espnow_init();
void espnow_deinit();
bool espnow_is_initialized();

// Structure to hold ESP-NOW data
typedef struct {
  uint8_t mac_addr[ESP_NOW_ETH_ALEN];
  uint8_t *data;
  int len;
  uint8_t chan;
} esp_now_event_t;

bool is_same_mac(const uint8_t *mac1, const uint8_t *mac2);



#ifdef __cplusplus
}
#endif

#endif
