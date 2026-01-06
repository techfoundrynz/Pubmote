#include "wifi.h"
#include "string.h"

#define ESP_MAXIMUM_RETRY 5
#define RECONNECT_DELAY_MS 5000

static bool is_initialized = false;
static const char *TAG = "PUBMOTE-WIFI";
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static wifi_connection_state_t s_wifi_state = WIFI_STATE_DISCONNECTED;
static bool s_auto_reconnect_enabled = true;
static char s_stored_ssid[33] = "";
static char s_stored_password[65] = "";
static TimerHandle_t s_reconnect_timer = NULL;
esp_netif_t *wifi_netif_sta = NULL;

// Timer callback for automatic reconnection
static void reconnect_timer_callback(TimerHandle_t xTimer) {
  if (s_auto_reconnect_enabled && s_wifi_state == WIFI_STATE_RECONNECTING) {
    ESP_LOGI(TAG, "Attempting automatic reconnection to: %s", s_stored_ssid);
    s_wifi_state = WIFI_STATE_CONNECTING;
    s_retry_num = 0;
    esp_wifi_connect();
  }
}

// WiFi event handler with state management and auto-reconnection
static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    ESP_LOGI(TAG, "WiFi station started");
    s_wifi_state = WIFI_STATE_DISCONNECTED;
  }
  else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    wifi_event_sta_disconnected_t *disconnected = (wifi_event_sta_disconnected_t *)event_data;
    ESP_LOGI(TAG, "Disconnected from WiFi (reason: %d)", disconnected->reason);

    if (s_wifi_state == WIFI_STATE_CONNECTED) {
      // Unexpected disconnection - start reconnection process
      if (s_auto_reconnect_enabled && strlen(s_stored_ssid) > 0) {
        s_wifi_state = WIFI_STATE_RECONNECTING;
        ESP_LOGI(TAG, "Connection lost, will attempt reconnection in %d ms", RECONNECT_DELAY_MS);

        // Start timer for delayed reconnection
        if (s_reconnect_timer != NULL) {
          xTimerStart(s_reconnect_timer, 0);
        }
      }
      else {
        s_wifi_state = WIFI_STATE_DISCONNECTED;
      }
    }
    else if (s_wifi_state == WIFI_STATE_CONNECTING || s_wifi_state == WIFI_STATE_RECONNECTING) {
      // Connection attempt failed
      if (s_retry_num < ESP_MAXIMUM_RETRY) {
        esp_wifi_connect();
        s_retry_num++;
        ESP_LOGI(TAG, "Retry %d/%d to connect to the AP", s_retry_num, ESP_MAXIMUM_RETRY);
      }
      else {
        ESP_LOGE(TAG, "Failed to connect after %d attempts", ESP_MAXIMUM_RETRY);
        s_wifi_state = WIFI_STATE_DISCONNECTED;
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);

        // Schedule reconnection if auto-reconnect is enabled
        if (s_auto_reconnect_enabled && strlen(s_stored_ssid) > 0) {
          s_wifi_state = WIFI_STATE_RECONNECTING;
          ESP_LOGI(TAG, "Will retry connection in %d ms", RECONNECT_DELAY_MS);
          if (s_reconnect_timer != NULL) {
            xTimerStart(s_reconnect_timer, 0);
          }
        }
      }
    }
  }
  else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Connected! Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
    s_retry_num = 0;
    s_wifi_state = WIFI_STATE_CONNECTED;
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

    // Stop reconnection timer if running
    if (s_reconnect_timer != NULL) {
      xTimerStop(s_reconnect_timer, 0);
    }
  }
}

// Initialize WiFi station mode after ESP-NOW
esp_err_t wifi_init(void) {
  ESP_LOGI(TAG, "Initializing WiFi station mode after ESP-NOW");

  // Note: Call esp_now_deinit() before calling this function
  // Do NOT call esp_wifi_stop() or esp_wifi_deinit() - leave WiFi running

  // Small delay to ensure ESP-NOW cleanup is complete
  vTaskDelay(pdMS_TO_TICKS(100));

  // Initialize NVS (should already be initialized from ESP-NOW)
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  else if (ret == ESP_ERR_INVALID_STATE) {
    ESP_LOGI(TAG, "NVS already initialized from ESP-NOW");
    ret = ESP_OK;
  }
  ESP_ERROR_CHECK(ret);

  // Create event group for WiFi station
  s_wifi_event_group = xEventGroupCreate();
  if (s_wifi_event_group == NULL) {
    ESP_LOGE(TAG, "Failed to create event group");
    return ESP_ERR_NO_MEM;
  }

  // Create reconnection timer
  s_reconnect_timer = xTimerCreate("reconnect_timer", pdMS_TO_TICKS(RECONNECT_DELAY_MS),
                                   pdFALSE, // One-shot timer
                                   NULL, reconnect_timer_callback);
  if (s_reconnect_timer == NULL) {
    ESP_LOGE(TAG, "Failed to create reconnection timer");
    return ESP_ERR_NO_MEM;
  }

  // Network interface should already be initialized from ESP-NOW
  esp_err_t netif_ret = esp_netif_init();
  if (netif_ret != ESP_OK && netif_ret != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "Failed to initialize network interface: %s", esp_err_to_name(netif_ret));
    return netif_ret;
  }
  if (netif_ret == ESP_ERR_INVALID_STATE) {
    ESP_LOGI(TAG, "Network interface already initialized from ESP-NOW");
  }

  // Event loop should already exist from ESP-NOW
  esp_err_t event_loop_ret = esp_event_loop_create_default();
  if (event_loop_ret != ESP_OK && event_loop_ret != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "Failed to create event loop: %s", esp_err_to_name(event_loop_ret));
    return event_loop_ret;
  }
  if (event_loop_ret == ESP_ERR_INVALID_STATE) {
    ESP_LOGI(TAG, "Event loop already exists from ESP-NOW");
  }

  // Create WiFi station network interface
  if (wifi_netif_sta == NULL) {
    wifi_netif_sta = esp_netif_create_default_wifi_sta();
  }

  // WiFi should already be initialized from ESP-NOW
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_err_t wifi_init_ret = esp_wifi_init(&cfg);
  if (wifi_init_ret != ESP_OK && wifi_init_ret != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "Failed to initialize WiFi: %s", esp_err_to_name(wifi_init_ret));
    return wifi_init_ret;
  }
  if (wifi_init_ret == ESP_ERR_INVALID_STATE) {
    ESP_LOGI(TAG, "WiFi already initialized from ESP-NOW, reconfiguring...");
  }

  // Register event handlers for WiFi station
  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(
      esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
  ESP_ERROR_CHECK(
      esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

  // Set WiFi mode to station (transition from ESP-NOW mode)
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

  // WiFi should already be started from ESP-NOW
  esp_err_t wifi_start_ret = esp_wifi_start();
  if (wifi_start_ret != ESP_OK && wifi_start_ret != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(wifi_start_ret));
    return wifi_start_ret;
  }
  if (wifi_start_ret == ESP_ERR_INVALID_STATE) {
    ESP_LOGI(TAG, "WiFi already started from ESP-NOW, continuing...");
  }

  esp_wifi_set_ps(WIFI_PS_NONE); // No power save for ESP-NOW (better performance)
  esp_wifi_set_max_tx_power(84); // ~14 dBm for balanced power and range

  ESP_LOGI(TAG, "WiFi station initialization completed after ESP-NOW transition");
  is_initialized = true;
  return ESP_OK;
}

// Scan for available WiFi networks and return list
esp_err_t wifi_scan_networks(wifi_network_info_t **networks, uint16_t *network_count) {
  ESP_LOGI(TAG, "Starting WiFi scan");

  if (networks == NULL || network_count == NULL) {
    ESP_LOGE(TAG, "Invalid parameters");
    return ESP_ERR_INVALID_ARG;
  }

  *networks = NULL;
  *network_count = 0;

  // Start scan
  esp_err_t err = esp_wifi_scan_start(NULL, true);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "WiFi scan start failed: %s", esp_err_to_name(err));
    return err;
  }

  // Get scan results
  uint16_t ap_count = 0;
  esp_wifi_scan_get_ap_num(&ap_count);

  if (ap_count == 0) {
    ESP_LOGI(TAG, "No access points found");
    return ESP_OK;
  }

  wifi_ap_record_t *ap_info = malloc(sizeof(wifi_ap_record_t) * ap_count);
  if (ap_info == NULL) {
    ESP_LOGE(TAG, "Failed to allocate memory for AP records");
    return ESP_ERR_NO_MEM;
  }

  esp_wifi_scan_get_ap_records(&ap_count, ap_info);

  // Allocate memory for network info array
  *networks = malloc(sizeof(wifi_network_info_t) * ap_count);
  if (*networks == NULL) {
    ESP_LOGE(TAG, "Failed to allocate memory for network info");
    free(ap_info);
    return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(TAG, "Found %d access points:", ap_count);
  ESP_LOGI(TAG, "               SSID              | RSSI | Protected");
  ESP_LOGI(TAG, "----------------------------------------------------");

  // Fill network info array
  for (int i = 0; i < ap_count; i++) {
    // Copy SSID
    strncpy((*networks)[i].ssid, (char *)ap_info[i].ssid, sizeof((*networks)[i].ssid) - 1);
    (*networks)[i].ssid[sizeof((*networks)[i].ssid) - 1] = '\0';

    // Copy RSSI
    (*networks)[i].rssi = ap_info[i].rssi;

    // Determine if password protected
    (*networks)[i].password_protected = (ap_info[i].authmode != WIFI_AUTH_OPEN);

    // Log network info
    ESP_LOGI(TAG, "%32s | %4d | %s", (*networks)[i].ssid, (*networks)[i].rssi,
             (*networks)[i].password_protected ? "Yes" : "No");
  }

  *network_count = ap_count;

  free(ap_info);
  ESP_LOGI(TAG, "WiFi scan completed, returning %d networks", ap_count);
  return ESP_OK;
}

// Function to free the network list (call this when done with the scan results)
void wifi_free_network_list(wifi_network_info_t *networks) {
  if (networks != NULL) {
    free(networks);
  }
}

// Connect to WiFi network with SSID and password
esp_err_t wifi_connect_to_network(const char *ssid, const char *password) {
  if (ssid == NULL) {
    ESP_LOGE(TAG, "SSID cannot be NULL");
    return ESP_ERR_INVALID_ARG;
  }

  ESP_LOGI(TAG, "Connecting to WiFi network: %s", ssid);

  // Store credentials for auto-reconnection
  strncpy(s_stored_ssid, ssid, sizeof(s_stored_ssid) - 1);
  s_stored_ssid[sizeof(s_stored_ssid) - 1] = '\0';

  if (password != NULL) {
    strncpy(s_stored_password, password, sizeof(s_stored_password) - 1);
    s_stored_password[sizeof(s_stored_password) - 1] = '\0';
  }
  else {
    s_stored_password[0] = '\0';
  }

  wifi_config_t wifi_config = {
      .sta =
          {
              .threshold.authmode = (password == NULL || strlen(password) == 0) ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK,
              .pmf_cfg = {.capable = true, .required = false},
          },
  };

  // Copy SSID and password
  strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
  if (password != NULL) {
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
  }

  // Set WiFi configuration
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

  // Reset retry counter and update state
  s_retry_num = 0;
  s_wifi_state = WIFI_STATE_CONNECTING;

  // Clear previous event bits
  xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

  // Start connection
  esp_err_t err = esp_wifi_connect();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "WiFi connect failed: %s", esp_err_to_name(err));
    s_wifi_state = WIFI_STATE_DISCONNECTED;
    return err;
  }

  // Wait for connection result
  EventBits_t bits =
      xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

  if (bits & WIFI_CONNECTED_BIT) {
    ESP_LOGI(TAG, "Successfully connected to WiFi network: %s", ssid);
    return ESP_OK;
  }
  else if (bits & WIFI_FAIL_BIT) {
    ESP_LOGE(TAG, "Failed to connect to WiFi network: %s", ssid);
    return ESP_FAIL;
  }
  else {
    ESP_LOGE(TAG, "Unexpected event");
    s_wifi_state = WIFI_STATE_DISCONNECTED;
    return ESP_ERR_TIMEOUT;
  }
}

// Disconnect from WiFi
esp_err_t wifi_disconnect(void) {
  ESP_LOGI(TAG, "Disconnecting from WiFi");

  // Stop auto-reconnection timer
  if (s_reconnect_timer != NULL) {
    xTimerStop(s_reconnect_timer, 0);
  }

  // Clear stored credentials to prevent auto-reconnection
  s_stored_ssid[0] = '\0';
  s_stored_password[0] = '\0';

  esp_err_t err = esp_wifi_disconnect();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "WiFi disconnect failed: %s", esp_err_to_name(err));
    return err;
  }

  // Update state and clear event bits
  s_wifi_state = WIFI_STATE_DISCONNECTED;
  xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

  ESP_LOGI(TAG, "WiFi disconnected");
  return ESP_OK;
}

// Uninitialize WiFi and clean up resources
esp_err_t wifi_uninit(void) {
  ESP_LOGI(TAG, "Uninitializing WiFi");

  // Stop and delete reconnection timer
  if (s_reconnect_timer != NULL) {
    xTimerStop(s_reconnect_timer, 0);
    xTimerDelete(s_reconnect_timer, 0);
    s_reconnect_timer = NULL;
  }

  // Clear stored credentials
  s_stored_ssid[0] = '\0';
  s_stored_password[0] = '\0';
  s_auto_reconnect_enabled = true; // Reset to default

  // Stop WiFi
  esp_err_t err = esp_wifi_stop();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "WiFi stop failed: %s", esp_err_to_name(err));
  }

  // Deinitialize WiFi
  err = esp_wifi_deinit();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "WiFi deinit failed: %s", esp_err_to_name(err));
  }

  // Clean up event group
  if (s_wifi_event_group) {
    vEventGroupDelete(s_wifi_event_group);
    s_wifi_event_group = NULL;
  }

  // Reset state
  s_wifi_state = WIFI_STATE_DISCONNECTED;

  ESP_LOGI(TAG, "WiFi uninitialized");
  is_initialized = false;
  return ESP_OK;
}

// Get current WiFi connection state
wifi_connection_state_t wifi_get_connection_state(void) {
  return s_wifi_state;
}

// Get connection state as string
const char *wifi_get_connection_state_string(void) {
  switch (s_wifi_state) {
  case WIFI_STATE_DISCONNECTED:
    return "DISCONNECTED";
  case WIFI_STATE_CONNECTING:
    return "CONNECTING";
  case WIFI_STATE_CONNECTED:
    return "CONNECTED";
  case WIFI_STATE_RECONNECTING:
    return "RECONNECTING";
  default:
    return "UNKNOWN";
  }
}

// Enable/disable automatic reconnection
void wifi_set_auto_reconnect(bool enable) {
  s_auto_reconnect_enabled = enable;
  ESP_LOGI(TAG, "Auto-reconnect %s", enable ? "enabled" : "disabled");

  if (!enable && s_reconnect_timer != NULL) {
    xTimerStop(s_reconnect_timer, 0);
    if (s_wifi_state == WIFI_STATE_RECONNECTING) {
      s_wifi_state = WIFI_STATE_DISCONNECTED;
    }
  }
}

// Check if auto-reconnect is enabled
bool wifi_is_auto_reconnect_enabled(void) {
  return s_auto_reconnect_enabled;
}

bool wifi_is_initialized() {
  return is_initialized;
}

int8_t wifi_get_rssi(void) {
  wifi_ap_record_t ap_info;
  if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) {
    return 0;
  }
  return ap_info.rssi;
}