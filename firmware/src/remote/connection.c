#include "connection.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "peers.h"
#include "receiver.h"
#include "remoteinputs.h"
#include "stats.h"
#include "time.h"
#include "transmitter.h"
#include "ui/ui.h"
#include <esp_err.h>
#include <esp_timer.h>
#include <remote/settings.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "PUBREMOTE-CONNECTION";
#define CONNECTION_TIMER_DELAY_MS 20
#define RECONNECTING_DURATION_MS 1000
#define TIMEOUT_DURATION_MS 30000

static TaskHandle_t connection_task_handle = NULL;
ConnectionState connection_state = CONNECTION_STATE_DISCONNECTED;
PairingState pairing_state = PAIRING_STATE_UNPAIRED;
static int64_t last_connection_state_change = 0;

void connection_update_state(ConnectionState state) {
  connection_state = state;
  last_connection_state_change = get_current_time_ms();

  if (connection_state == CONNECTION_STATE_DISCONNECTED) {
    // Reset all stats when moving to disconnected state
    stats_init();
  }
  else if (connection_state == CONNECTION_STATE_RECONNECTING) {
    remoteStats.signalStrength = -255;
  }

  stats_update();
}

// Use task rather than a timer so we can do heavy lifting in here
static void connection_task(void *pvParameters) {
  while (1) {
    if (connection_state == CONNECTION_STATE_DISCONNECTED) {
      // Do nothing
    }
    else if (connection_state == CONNECTION_STATE_CONNECTED) {
      if (get_current_time_ms() - remoteStats.lastUpdated > RECONNECTING_DURATION_MS) {
        // No data received for a while - update connection state
        connection_update_state(CONNECTION_STATE_RECONNECTING);
      }
    }
    else if (connection_state == CONNECTION_STATE_CONNECTING) {
      if (get_current_time_ms() - last_connection_state_change > TIMEOUT_DURATION_MS) {
        // Never connected - reset the connection state
        connection_update_state(CONNECTION_STATE_DISCONNECTED);
      }
      else if (remoteStats.lastUpdated > 0 &&
               get_current_time_ms() - remoteStats.lastUpdated < RECONNECTING_DURATION_MS) {
        // Connected - update connection state
        connection_update_state(CONNECTION_STATE_CONNECTED);
        // Save pairing data. This way we remember the last channel we connected on
        save_pairing_data();
      }
    }
    else if (connection_state == CONNECTION_STATE_RECONNECTING) {
      if (get_current_time_ms() - last_connection_state_change > TIMEOUT_DURATION_MS) {
        // Reconnect failed - reset the connection state
        connection_update_state(CONNECTION_STATE_DISCONNECTED);
      }
      else if (get_current_time_ms() - remoteStats.lastUpdated < RECONNECTING_DURATION_MS) {
        // Reconnected - update connection state
        connection_update_state(CONNECTION_STATE_CONNECTED);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(CONNECTION_TIMER_DELAY_MS));
  }

  // The task will not reach this point as it runs indefinitely
  ESP_LOGI(TAG, "Connection management task ended");
  vTaskDelete(NULL);
  connection_task_handle = NULL;
}

void connection_connect_to_peer(uint8_t *mac_addr, uint8_t channel) {
  esp_now_peer_info_t peerInfo = {};
  peerInfo.channel = channel; // Set the channel number (0-14)
  peerInfo.encrypt = false;
  memcpy(peerInfo.peer_addr, mac_addr, ESP_NOW_ETH_ALEN);

  if (esp_now_is_peer_exist(mac_addr)) {
    esp_err_t res = esp_now_del_peer(mac_addr);
    if (res != ESP_OK) {
      ESP_LOGE(TAG, "Failed to delete peer");
    }
  }

  esp_err_t result = esp_now_add_peer(&peerInfo);

  if (result == ESP_OK) {
    connection_update_state(CONNECTION_STATE_CONNECTING);
  }
  else {
    ESP_LOGE(TAG, "Failed to add peer");
  }
}

void connection_connect_to_default_peer() {
  if (pairing_state == PAIRING_STATE_PAIRED) {
    uint8_t *mac_addr = pairing_settings.remote_addr;
    connection_connect_to_peer(mac_addr, pairing_settings.channel);
  }
}

void connection_init() {
  if (pairing_settings.secret_code != DEFAULT_PAIRING_SECRET_CODE) {
    pairing_state = PAIRING_STATE_PAIRED;
  }

  // start off in connecting mode
  if (pairing_state == PAIRING_STATE_PAIRED) {
    connection_connect_to_default_peer();
  }

  ESP_ERROR_CHECK(
      xTaskCreatePinnedToCore(connection_task, "connection_task", 4096, NULL, 20, &connection_task_handle, 0) == pdPASS
          ? ESP_OK
          : ESP_FAIL);
}

void connection_deinit() {
  if (connection_task_handle != NULL) {
    vTaskDelete(connection_task_handle);
    connection_task_handle = NULL;
  }
}