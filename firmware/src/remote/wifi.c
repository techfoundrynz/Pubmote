#include "wifi.h"
#include "esp_heap_caps.h"
#include "freertos/semphr.h"
#include "string.h"
#include <stdatomic.h>

#define ESP_MAXIMUM_RETRY 5
#define RECONNECT_DELAY_MS 5000
#define WIFI_SCAN_MAX_RECORDS 40

// Retained across restarts so a waiter can never reference a deleted lock.
static SemaphoreHandle_t radio_mutex = NULL;
static atomic_bool connect_pending = false;

static bool is_initialized = false;
static const char *TAG = "PUBMOTE-WIFI";
static EventGroupHandle_t s_wifi_event_group = NULL;
static int s_retry_num = 0;
static wifi_connection_state_t s_wifi_state = WIFI_STATE_DISCONNECTED;
static bool s_auto_reconnect_enabled = true;
static char s_stored_ssid[33] = "";
static char s_stored_password[65] = "";
static TimerHandle_t s_reconnect_timer = NULL;
static esp_event_handler_instance_t s_instance_any_id = NULL;
static esp_event_handler_instance_t s_instance_got_ip = NULL;
static bool s_owns_event_loop = false;
esp_netif_t *wifi_netif_sta = NULL;

// Timer callback for automatic reconnection
static void reconnect_timer_callback(TimerHandle_t xTimer) {
  if (connect_pending || !radio_mutex || xSemaphoreTake(radio_mutex, 0) != pdTRUE) {
    if (s_auto_reconnect_enabled)
      xTimerStart(xTimer, 0);
    return;
  }
  if (s_auto_reconnect_enabled && s_wifi_state == WIFI_STATE_RECONNECTING) {
    ESP_LOGI(TAG, "Attempting automatic reconnection to: %s", s_stored_ssid);
    s_wifi_state = WIFI_STATE_CONNECTING;
    s_retry_num = 0;
    esp_wifi_connect();
  }
  xSemaphoreGive(radio_mutex);
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

// Release everything wifi_init() allocated. Safe to call on a partially
// initialized module - every step is guarded by its own handle
static void wifi_release_resources(void) {
  if (s_instance_any_id != NULL) {
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_instance_any_id);
    s_instance_any_id = NULL;
  }
  if (s_instance_got_ip != NULL) {
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_instance_got_ip);
    s_instance_got_ip = NULL;
  }

  if (wifi_netif_sta != NULL) {
    esp_netif_destroy_default_wifi(wifi_netif_sta);
    wifi_netif_sta = NULL;
  }

  // Only tear down the event loop if this module created it - the failure paths
  // reach here too, and ESP-NOW's loop is not ours to delete
  if (s_owns_event_loop) {
    esp_err_t err = esp_event_loop_delete_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
      ESP_LOGW(TAG, "esp_event_loop_delete_default failed: %s", esp_err_to_name(err));
    }
    s_owns_event_loop = false;
  }

  if (s_reconnect_timer != NULL) {
    xTimerStop(s_reconnect_timer, 0);
    xTimerDelete(s_reconnect_timer, 0);
    s_reconnect_timer = NULL;
  }

  if (s_wifi_event_group != NULL) {
    vEventGroupDelete(s_wifi_event_group);
    s_wifi_event_group = NULL;
  }
}

// Initialize WiFi station mode after ESP-NOW
esp_err_t wifi_init(void) {
  if (is_initialized) {
    ESP_LOGW(TAG, "WiFi already initialized");
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Initializing WiFi station mode after ESP-NOW");
  if (!radio_mutex)
    radio_mutex = xSemaphoreCreateMutex();
  if (!radio_mutex)
    return ESP_ERR_NO_MEM;

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
    ret = ESP_ERR_NO_MEM;
    goto fail;
  }

  // Create reconnection timer
  s_reconnect_timer = xTimerCreate("reconnect_timer", pdMS_TO_TICKS(RECONNECT_DELAY_MS),
                                   pdFALSE, // One-shot timer
                                   NULL, reconnect_timer_callback);
  if (s_reconnect_timer == NULL) {
    ESP_LOGE(TAG, "Failed to create reconnection timer");
    ret = ESP_ERR_NO_MEM;
    goto fail;
  }

  // Network interface should already be initialized from ESP-NOW
  esp_err_t netif_ret = esp_netif_init();
  if (netif_ret != ESP_OK && netif_ret != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "Failed to initialize network interface: %s", esp_err_to_name(netif_ret));
    ret = netif_ret;
    goto fail;
  }
  if (netif_ret == ESP_ERR_INVALID_STATE) {
    ESP_LOGI(TAG, "Network interface already initialized from ESP-NOW");
  }

  // Event loop should already exist from ESP-NOW
  esp_err_t event_loop_ret = esp_event_loop_create_default();
  if (event_loop_ret != ESP_OK && event_loop_ret != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "Failed to create event loop: %s", esp_err_to_name(event_loop_ret));
    ret = event_loop_ret;
    goto fail;
  }
  s_owns_event_loop = (event_loop_ret == ESP_OK);
  if (event_loop_ret == ESP_ERR_INVALID_STATE) {
    ESP_LOGI(TAG, "Event loop already exists from ESP-NOW");
  }

  // Create WiFi station network interface
  if (wifi_netif_sta == NULL) {
    wifi_netif_sta = esp_netif_create_default_wifi_sta();
    if (wifi_netif_sta == NULL) {
      ESP_LOGE(TAG, "Failed to create WiFi STA netif");
      ret = ESP_ERR_NO_MEM;
      goto fail;
    }
  }

  // Reuse the WiFi driver if retained from ESP-NOW.
  wifi_mode_t existing_mode;
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_err_t wifi_init_ret = esp_wifi_get_mode(&existing_mode);
  if (wifi_init_ret == ESP_ERR_WIFI_NOT_INIT)
    wifi_init_ret = esp_wifi_init(&cfg);
  if (wifi_init_ret != ESP_OK && wifi_init_ret != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "Failed to initialize WiFi: %s (free internal: %u, largest block: %u)",
             esp_err_to_name(wifi_init_ret), heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    ret = wifi_init_ret;
    goto fail;
  }
  if (wifi_init_ret == ESP_ERR_INVALID_STATE) {
    ESP_LOGI(TAG, "WiFi already initialized from ESP-NOW, reconfiguring...");
  }

  // Register event handlers for WiFi station
  ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &s_instance_any_id);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register WiFi event handler: %s", esp_err_to_name(ret));
    goto fail_wifi;
  }
  ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &s_instance_got_ip);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register IP event handler: %s", esp_err_to_name(ret));
    goto fail_wifi;
  }

  // Set WiFi mode to station (transition from ESP-NOW mode)
  ret = esp_wifi_set_mode(WIFI_MODE_STA);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set WiFi mode: %s", esp_err_to_name(ret));
    goto fail_wifi;
  }

  // WiFi should already be started from ESP-NOW
  esp_err_t wifi_start_ret = esp_wifi_start();
  if (wifi_start_ret != ESP_OK && wifi_start_ret != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "Failed to start WiFi: %s (free internal: %u, largest block: %u)", esp_err_to_name(wifi_start_ret),
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    ret = wifi_start_ret;
    goto fail_wifi;
  }
  if (wifi_start_ret == ESP_ERR_INVALID_STATE) {
    ESP_LOGI(TAG, "WiFi already started from ESP-NOW, continuing...");
  }

  esp_wifi_set_ps(WIFI_PS_NONE); // No power save for ESP-NOW (better performance)
  esp_wifi_set_max_tx_power(52); // ~14 dBm for balanced power and range

  ESP_LOGI(TAG, "WiFi station initialization completed after ESP-NOW transition");
  s_auto_reconnect_enabled = true;
  is_initialized = true;
  return ESP_OK;

fail_wifi:
  esp_wifi_stop();
  esp_wifi_deinit();
fail:
  wifi_release_resources();
  s_wifi_state = WIFI_STATE_DISCONNECTED;
  return ret;
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
  if (!is_initialized || !radio_mutex)
    return ESP_ERR_WIFI_NOT_INIT;
  if (connect_pending || xSemaphoreTake(radio_mutex, 0) != pdTRUE)
    return ESP_ERR_INVALID_STATE;
  esp_err_t err = ESP_OK;
  wifi_ap_record_t *ap_info = NULL;
  wifi_network_info_t *result = NULL;
  bool scan_attempted = false;
  if (!is_initialized || connect_pending || s_wifi_state == WIFI_STATE_CONNECTING ||
      s_wifi_state == WIFI_STATE_RECONNECTING) {
    err = ESP_ERR_INVALID_STATE;
    goto done;
  }

  // Start scan
  scan_attempted = true;
  err = esp_wifi_scan_start(NULL, true);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "WiFi scan start failed: %s", esp_err_to_name(err));
    goto done;
  }
  if (connect_pending) {
    err = ESP_ERR_INVALID_STATE;
    goto done;
  }

  // Get scan results
  uint16_t ap_count = 0;
  err = esp_wifi_scan_get_ap_num(&ap_count);
  if (err != ESP_OK)
    goto done;

  if (ap_count == 0) {
    ESP_LOGI(TAG, "No access points found");
    goto done;
  }
  if (ap_count > WIFI_SCAN_MAX_RECORDS)
    ap_count = WIFI_SCAN_MAX_RECORDS;

  ap_info = calloc(ap_count, sizeof(*ap_info));
  if (ap_info == NULL) {
    ESP_LOGE(TAG, "Failed to allocate memory for AP records");
    err = ESP_ERR_NO_MEM;
    goto done;
  }

  err = esp_wifi_scan_get_ap_records(&ap_count, ap_info);
  if (err != ESP_OK)
    goto done;

  // Allocate memory for network info array
  result = calloc(ap_count, sizeof(*result));
  if (result == NULL) {
    ESP_LOGE(TAG, "Failed to allocate memory for network info");
    err = ESP_ERR_NO_MEM;
    goto done;
  }

  ESP_LOGI(TAG, "Found %d access points:", ap_count);
  ESP_LOGI(TAG, "               SSID              | RSSI | Protected");
  ESP_LOGI(TAG, "----------------------------------------------------");

  // Fill network info array
  uint16_t unique_count = 0;
  for (int i = 0; i < ap_count; i++) {
    if (ap_info[i].ssid[0] == '\0')
      continue;
    int existing = -1;
    for (int j = 0; j < unique_count; ++j) {
      if (strcmp(result[j].ssid, (char *)ap_info[i].ssid) == 0) {
        existing = j;
        break;
      }
    }
    if (existing >= 0) {
      result[existing].password_protected |= ap_info[i].authmode != WIFI_AUTH_OPEN;
      if (ap_info[i].rssi > result[existing].rssi)
        result[existing].rssi = ap_info[i].rssi;
      continue;
    }
    wifi_network_info_t *network = &result[unique_count++];
    // Copy SSID
    strncpy(network->ssid, (char *)ap_info[i].ssid, sizeof(network->ssid) - 1);

    // Copy RSSI
    network->rssi = ap_info[i].rssi;

    // Determine if password protected
    network->password_protected = (ap_info[i].authmode != WIFI_AUTH_OPEN);

    // Log network info
    ESP_LOGI(TAG, "%32s | %4d | %s", network->ssid, network->rssi, network->password_protected ? "Yes" : "No");
  }

  *network_count = unique_count;
  if (unique_count) {
    *networks = result;
    result = NULL;
  }
done:
  // The driver owns a separate AP list, including on allocation/cancel failure.
  if (scan_attempted)
    esp_wifi_clear_ap_list();
  free(ap_info);
  free(result);
  xSemaphoreGive(radio_mutex);
  return err;
}

// Function to free the network list (call this when done with the scan results)
void wifi_free_network_list(wifi_network_info_t *networks) {
  if (networks != NULL) {
    free(networks);
  }
}

// Connect to WiFi network with SSID and password
static esp_err_t connect_to_network_locked(const char *ssid, const char *password, wifi_cancel_fn cancelled,
                                           void *context) {
  if (ssid == NULL || strlen(ssid) == 0 || strlen(ssid) > 32 || (password && strlen(password) > 64)) {
    ESP_LOGE(TAG, "SSID cannot be NULL");
    return ESP_ERR_INVALID_ARG;
  }

  if (!is_initialized || s_wifi_event_group == NULL) {
    ESP_LOGE(TAG, "WiFi is not initialized - cannot connect");
    return ESP_ERR_WIFI_NOT_INIT;
  }

  ESP_LOGI(TAG, "Connecting to WiFi network: %s", ssid);
  if (cancelled && cancelled(context))
    return ESP_ERR_TIMEOUT;

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
  memcpy(wifi_config.sta.ssid, ssid, strlen(ssid));
  if (password != NULL) {
    memcpy(wifi_config.sta.password, password, strlen(password));
  }

  // Set WiFi configuration
  esp_err_t cfg_err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
  if (cfg_err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set WiFi config: %s", esp_err_to_name(cfg_err));
    s_wifi_state = WIFI_STATE_DISCONNECTED;
    return cfg_err;
  }

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

  // Wait in bounded increments so the owning worker can cancel without touching UI state.
  EventBits_t bits = 0;
  const int check_interval_ms = 200;
  const int max_wait_ms = 30000; // 30 seconds timeout
  int waited_ms = 0;

  while (waited_ms < max_wait_ms) {
    if (cancelled && cancelled(context)) {
      ESP_LOGW(TAG, "WiFi connection cancelled");
      s_wifi_state = WIFI_STATE_DISCONNECTED;
      return ESP_ERR_TIMEOUT;
    }

    bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE,
                               pdMS_TO_TICKS(check_interval_ms));
    if (bits & (WIFI_CONNECTED_BIT | WIFI_FAIL_BIT)) {
      break;
    }
    waited_ms += check_interval_ms;
  }

  if (cancelled && cancelled(context)) {
    s_wifi_state = WIFI_STATE_DISCONNECTED;
    return ESP_ERR_TIMEOUT;
  }

  if (bits & WIFI_CONNECTED_BIT) {
    ESP_LOGI(TAG, "Successfully connected to WiFi network: %s", ssid);
    return ESP_OK;
  }
  else if (bits & WIFI_FAIL_BIT) {
    ESP_LOGE(TAG, "Failed to connect to WiFi network: %s", ssid);
    return ESP_FAIL;
  }
  else {
    ESP_LOGE(TAG, "Unexpected event or connection timeout");
    s_wifi_state = WIFI_STATE_DISCONNECTED;
    return ESP_ERR_TIMEOUT;
  }
}

esp_err_t wifi_connect_to_network_cancellable(const char *ssid, const char *password, wifi_cancel_fn cancelled,
                                              void *context) {
  if (!is_initialized || !radio_mutex)
    return ESP_ERR_WIFI_NOT_INIT;
  // Only one connect request can claim priority over an optional scan.
  if (atomic_exchange(&connect_pending, true))
    return ESP_ERR_INVALID_STATE;
  TickType_t started = xTaskGetTickCount();
  while (xSemaphoreTake(radio_mutex, 0) != pdTRUE) {
    if (cancelled && cancelled(context)) {
      connect_pending = false;
      return ESP_ERR_TIMEOUT;
    }
    esp_wifi_scan_stop();
    if (xTaskGetTickCount() - started >= pdMS_TO_TICKS(1000)) {
      connect_pending = false;
      return ESP_ERR_TIMEOUT;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  esp_err_t err =
      is_initialized ? connect_to_network_locked(ssid, password, cancelled, context) : ESP_ERR_WIFI_NOT_INIT;
  if (err != ESP_OK) {
    // Do not let an abandoned join or its retry timer revive after cancellation.
    s_wifi_state = WIFI_STATE_DISCONNECTED;
    if (s_reconnect_timer)
      xTimerStop(s_reconnect_timer, 0);
    esp_wifi_disconnect();
  }
  connect_pending = false;
  xSemaphoreGive(radio_mutex);
  return err;
}

static bool update_cancelled(void *context) {
  (void)context;
  extern bool is_update_screen_active(void);
  return !is_update_screen_active();
}

esp_err_t wifi_connect_to_network(const char *ssid, const char *password) {
  return wifi_connect_to_network_cancellable(ssid, password, update_cancelled, NULL);
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
  if (!is_initialized)
    return ESP_OK;
  if (!radio_mutex || xSemaphoreTake(radio_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
    return ESP_ERR_TIMEOUT;

  // Stop the reconnection timer before tearing the driver down
  if (s_reconnect_timer != NULL) {
    xTimerStop(s_reconnect_timer, 0);
  }

  // Clear stored credentials
  s_stored_ssid[0] = '\0';
  s_stored_password[0] = '\0';
  s_auto_reconnect_enabled = false; // Keep callbacks from reconnecting during teardown
  s_wifi_state = WIFI_STATE_DISCONNECTED;

  // Stop WiFi
  esp_err_t err = esp_wifi_stop();
  if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED && err != ESP_ERR_WIFI_NOT_INIT) {
    ESP_LOGE(TAG, "WiFi stop failed: %s", esp_err_to_name(err));
    xSemaphoreGive(radio_mutex);
    return err;
  }

  // Deinitialize WiFi
  err = esp_wifi_deinit();
  if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT) {
    ESP_LOGE(TAG, "WiFi deinit failed: %s", esp_err_to_name(err));
    xSemaphoreGive(radio_mutex);
    return err;
  }

  // Destroy the netif and event loop so a following BLE (re)init gets its
  // internal RAM back (the lwip TCP/IP thread itself cannot be reclaimed -
  // esp_netif_deinit is unsupported - but everything else can)
  wifi_release_resources();

  // Reset state
  s_wifi_state = WIFI_STATE_DISCONNECTED;

  ESP_LOGI(TAG, "WiFi uninitialized");
  is_initialized = false;
  xSemaphoreGive(radio_mutex);
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
