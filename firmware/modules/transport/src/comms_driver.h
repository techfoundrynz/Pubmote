#pragma once
#include "remote/comms.h"

// Private driver interface. Application consumers use remote/comms.h.
typedef struct {
  CommsType type;
  const char *name;
  esp_err_t (*init)(void);
  esp_err_t (*deinit)(void);
  bool (*is_initialized)(void);
  esp_err_t (*register_recv_cb)(comms_recv_cb_t cb);
  esp_err_t (*register_send_cb)(comms_send_cb_t cb);
  esp_err_t (*register_discovery_cb)(comms_discovery_cb_t cb);
  esp_err_t (*send)(const uint8_t *peer_mac, const uint8_t *data, size_t len);
  esp_err_t (*connect_peer)(const uint8_t *peer_mac, uint8_t channel);
  esp_err_t (*disconnect_peer)(const uint8_t *peer_mac);
  bool (*peer_exists)(const uint8_t *peer_mac);
  uint8_t (*get_peer_channel)(const uint8_t *peer_mac);
  esp_err_t (*set_channel)(uint8_t channel);
} CommsDriver;

extern const CommsDriver espnow_driver;
extern const CommsDriver ble_driver;
esp_err_t espnow_prepare_wifi(void);
