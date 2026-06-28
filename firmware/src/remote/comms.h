#ifndef __COMMS_H
#define __COMMS_H

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define COMMS_MAC_LEN 6

  typedef enum {
    COMMS_TYPE_ESPNOW,
    COMMS_TYPE_BLE
  } CommsType;

  // Structure to hold received data events
  typedef struct {
    uint8_t mac_addr[COMMS_MAC_LEN];
    uint8_t *data;
    int len;
    uint8_t chan;
  } comms_event_t;

  // Callback type for when data is received
  typedef void (*comms_recv_cb_t)(const uint8_t *src_mac, const uint8_t *data, int len, uint8_t channel, int rssi);

  // Callback type for when data is sent (status callback)
  typedef void (*comms_send_cb_t)(const uint8_t *peer_mac, bool success);

  // Callback type for when a BLE device is discovered
  typedef void (*comms_discovery_cb_t)(const uint8_t *mac, const char *name, int rssi);

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

  // Public APIs
  esp_err_t comms_init(void);
  esp_err_t comms_deinit(void);
  bool comms_is_initialized(void);

  esp_err_t comms_register_recv_cb(comms_recv_cb_t cb);
  esp_err_t comms_register_send_cb(comms_send_cb_t cb);
  esp_err_t comms_register_discovery_cb(comms_discovery_cb_t cb);
  esp_err_t comms_send(const uint8_t *peer_mac, const uint8_t *data, size_t len);

  esp_err_t comms_connect_peer(const uint8_t *peer_mac, uint8_t channel);
  esp_err_t comms_disconnect_peer(const uint8_t *peer_mac);
  bool comms_peer_exists(const uint8_t *peer_mac);
  uint8_t comms_get_peer_channel(const uint8_t *peer_mac);

  esp_err_t comms_set_channel(uint8_t channel);

  // Driver selection
  esp_err_t comms_select_driver(CommsType type);
  CommsType comms_get_active_type(void);

  // Helper function to check if MAC addresses match
  bool comms_is_same_mac(const uint8_t *mac1, const uint8_t *mac2);

#define VESC_COMM_CUSTOM_APP_DATA 36
#define PUBMOTE_MAGIC 169

  uint8_t *comms_prepend_headers(const uint8_t *data, size_t len, CommsType type, size_t *out_len);
  bool comms_strip_headers(const uint8_t **data_ptr, int *len_ptr, CommsType type);
  bool ble_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif
