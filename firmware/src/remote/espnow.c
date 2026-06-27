#include "comms.h"
#include "connection.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "settings.h"
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi_types.h>
#include <nvs.h>
#include <remote/stats.h>
#include <string.h>

static const char *TAG = "PUBREMOTE-ESPNOW-DRV";
static bool is_initialized = false;
static comms_recv_cb_t registered_recv_cb = NULL;
static comms_send_cb_t registered_send_cb = NULL;

static void on_espnow_recv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
  if (registered_recv_cb) {
    registered_recv_cb(recv_info->src_addr, data, len, recv_info->rx_ctrl->channel, recv_info->rx_ctrl->rssi);
  }
}

static void on_espnow_sent(const esp_now_send_info_t *tx_info, esp_now_send_status_t status) {
  if (registered_send_cb) {
    registered_send_cb(tx_info->des_addr, status == ESP_NOW_SEND_SUCCESS);
  }
}

static esp_err_t espnow_driver_init(void) {
  if (is_initialized) {
    return ESP_OK;
  }

  // Initialize NVS (handle case where already initialized)
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  else if (ret == ESP_ERR_INVALID_STATE) {
    ESP_LOGI(TAG, "NVS already initialized");
    ret = ESP_OK;
  }
  ESP_ERROR_CHECK(ret);

  // Initialize network interface (handle case where already initialized)
  esp_err_t netif_ret = esp_netif_init();
  if (netif_ret != ESP_OK && netif_ret != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "Failed to initialize network interface: %s", esp_err_to_name(netif_ret));
    return netif_ret;
  }
  if (netif_ret == ESP_ERR_INVALID_STATE) {
    ESP_LOGI(TAG, "Network interface already initialized");
  }

  // Create default event loop (handle case where already exists)
  esp_err_t event_loop_ret = esp_event_loop_create_default();
  if (event_loop_ret != ESP_OK && event_loop_ret != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "Failed to create event loop: %s", esp_err_to_name(event_loop_ret));
    return event_loop_ret;
  }
  if (event_loop_ret == ESP_ERR_INVALID_STATE) {
    ESP_LOGI(TAG, "Event loop already exists, reusing it");
  }

  // Create default WiFi station network interface before esp_wifi_init
  extern esp_netif_t *wifi_netif_sta;
  if (wifi_netif_sta == NULL) {
    wifi_netif_sta = esp_netif_create_default_wifi_sta();
  }

  // Initialize WiFi (should be fresh after wifi_uninit())
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_err_t wifi_init_ret = esp_wifi_init(&cfg);
  if (wifi_init_ret != ESP_OK && wifi_init_ret != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "Failed to initialize WiFi: %s", esp_err_to_name(wifi_init_ret));
    return wifi_init_ret;
  }
  if (wifi_init_ret == ESP_ERR_INVALID_STATE) {
    ESP_LOGW(TAG, "WiFi already initialized (unexpected after wifi_uninit())");
  }

  // Set WiFi mode to station for ESP-NOW
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

  // Start WiFi
  esp_err_t wifi_start_ret = esp_wifi_start();
  if (wifi_start_ret != ESP_OK && wifi_start_ret != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(wifi_start_ret));
    return wifi_start_ret;
  }
  if (wifi_start_ret == ESP_ERR_INVALID_STATE) {
    ESP_LOGW(TAG, "WiFi already started (unexpected after wifi_uninit())");
  }

  esp_wifi_set_ps(WIFI_PS_NONE); // No power save for ESP-NOW (better performance)
  esp_wifi_set_max_tx_power(52); // ~14 dBm for balanced power and range
  ESP_LOGI(TAG, "ESP-NOW power settings configured");

  // Set WiFi channel for ESP-NOW
  ESP_ERROR_CHECK(esp_wifi_set_channel(pairing_settings.channel, WIFI_SECOND_CHAN_NONE));

  // Initialize ESP-NOW
  ESP_ERROR_CHECK(esp_now_init());

  // Register callbacks
  ESP_ERROR_CHECK(esp_now_register_recv_cb(on_espnow_recv));
  ESP_ERROR_CHECK(esp_now_register_send_cb(on_espnow_sent));

  ESP_LOGI(TAG, "ESP-NOW initialized successfully");
  is_initialized = true;
  return ESP_OK;
}

static esp_err_t espnow_driver_deinit(void) {
  if (!is_initialized) {
    return ESP_OK;
  }
  esp_now_unregister_recv_cb();
  esp_now_unregister_send_cb();

  esp_err_t err = esp_now_deinit();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_now_deinit failed: %s", esp_err_to_name(err));
  }

  // Stop WiFi to release controller memory
  err = esp_wifi_stop();
  if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT) {
    ESP_LOGE(TAG, "esp_wifi_stop failed: %s", esp_err_to_name(err));
  }

  // Deinitialize WiFi to release driver structures
  err = esp_wifi_deinit();
  if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT) {
    ESP_LOGE(TAG, "esp_wifi_deinit failed: %s", esp_err_to_name(err));
  }

  ESP_LOGI(TAG, "ESP-NOW deinitialized");
  is_initialized = false;
  return ESP_OK;
}

static bool espnow_driver_is_initialized(void) {
  return is_initialized;
}

static esp_err_t espnow_driver_register_recv_cb(comms_recv_cb_t cb) {
  registered_recv_cb = cb;
  return ESP_OK;
}

static esp_err_t espnow_driver_register_send_cb(comms_send_cb_t cb) {
  registered_send_cb = cb;
  return ESP_OK;
}

static esp_err_t espnow_driver_register_discovery_cb(comms_discovery_cb_t cb) {
  return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t espnow_driver_send(const uint8_t *peer_mac, const uint8_t *data, size_t len) {
  size_t wrapped_len = 0;
  uint8_t *wrapped_payload = comms_prepend_headers(data, len, COMMS_TYPE_ESPNOW, &wrapped_len);
  if (!wrapped_payload) {
    return ESP_ERR_NO_MEM;
  }

  esp_err_t res = esp_now_send(peer_mac, wrapped_payload, wrapped_len);
  free(wrapped_payload);
  return res;
}

static esp_err_t espnow_driver_connect_peer(const uint8_t *peer_mac, uint8_t channel) {
  esp_now_peer_info_t peerInfo = {};
  peerInfo.channel = channel;
  peerInfo.encrypt = false;
  memcpy(peerInfo.peer_addr, peer_mac, COMMS_MAC_LEN);

  if (esp_now_is_peer_exist(peer_mac)) {
    esp_err_t res = esp_now_del_peer(peer_mac);
    if (res != ESP_OK) {
      ESP_LOGE(TAG, "Failed to delete peer");
      return res;
    }
  }

  return esp_now_add_peer(&peerInfo);
}

static esp_err_t espnow_driver_disconnect_peer(const uint8_t *peer_mac) {
  if (esp_now_is_peer_exist(peer_mac)) {
    return esp_now_del_peer(peer_mac);
  }
  return ESP_ERR_NOT_FOUND;
}

static bool espnow_driver_peer_exists(const uint8_t *peer_mac) {
  return esp_now_is_peer_exist(peer_mac);
}

static uint8_t espnow_driver_get_peer_channel(const uint8_t *peer_mac) {
  esp_now_peer_info_t peer_info = {0};
  esp_err_t result = esp_now_get_peer(peer_mac, &peer_info);
  if (result != ESP_OK) {
    return 0;
  }
  return peer_info.channel;
}

static esp_err_t espnow_driver_set_channel(uint8_t channel) {
  return esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

const CommsDriver espnow_driver = {.type = COMMS_TYPE_ESPNOW,
                                   .name = "ESP-NOW",
                                   .init = espnow_driver_init,
                                   .deinit = espnow_driver_deinit,
                                   .is_initialized = espnow_driver_is_initialized,
                                   .register_recv_cb = espnow_driver_register_recv_cb,
                                   .register_send_cb = espnow_driver_register_send_cb,
                                   .register_discovery_cb = espnow_driver_register_discovery_cb,
                                   .send = espnow_driver_send,
                                   .connect_peer = espnow_driver_connect_peer,
                                   .disconnect_peer = espnow_driver_disconnect_peer,
                                   .peer_exists = espnow_driver_peer_exists,
                                   .get_peer_channel = espnow_driver_get_peer_channel,
                                   .set_channel = espnow_driver_set_channel};