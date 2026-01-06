#include "connection.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "settings.h"
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <nvs.h>
#include <remote/stats.h>
#include <string.h>

static const char *TAG = "PUBREMOTE-ESPNOW";
static bool is_initialized = false;

void espnow_init() {
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
    return; // or handle error as appropriate
  }
  if (netif_ret == ESP_ERR_INVALID_STATE) {
    ESP_LOGI(TAG, "Network interface already initialized");
  }

  // Create default event loop (handle case where already exists)
  esp_err_t event_loop_ret = esp_event_loop_create_default();
  if (event_loop_ret != ESP_OK && event_loop_ret != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "Failed to create event loop: %s", esp_err_to_name(event_loop_ret));
    return; // or handle error as appropriate
  }
  if (event_loop_ret == ESP_ERR_INVALID_STATE) {
    ESP_LOGI(TAG, "Event loop already exists, reusing it");
  }

  // Initialize WiFi (should be fresh after wifi_uninit())
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_err_t wifi_init_ret = esp_wifi_init(&cfg);
  if (wifi_init_ret != ESP_OK && wifi_init_ret != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "Failed to initialize WiFi: %s", esp_err_to_name(wifi_init_ret));
    return; // or handle error as appropriate
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
    return; // or handle error as appropriate
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
  ESP_LOGI(TAG, "ESP-NOW initialized successfully");
  is_initialized = true;
}

void espnow_deinit() {
  ESP_ERROR_CHECK(esp_now_deinit());
  ESP_ERROR_CHECK(esp_wifi_stop());
  ESP_ERROR_CHECK(esp_wifi_deinit());
  ESP_LOGI(TAG, "ESP-NOW deinitialized");
  is_initialized = false;
}

bool is_same_mac(const uint8_t *mac1, const uint8_t *mac2) {
  return memcmp(mac1, mac2, ESP_NOW_ETH_ALEN) == 0;
}

bool espnow_is_initialized() {
  return is_initialized;
}