#include "connection.h"
#include "esp_event.h"
#include "esp_log.h"
#include "comms.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "peers.h"
#include "receiver.h"
#include "remoteinputs.h"
#include "stats.h"
#include "time.h"

#include <esp_timer.h>
#include <remote/settings.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "PUBREMOTE-CONNECTION";
#define CONNECTION_TIMER_DELAY_MS 20
#define RECONNECTING_DURATION_MS 1000
#define TIMEOUT_DURATION_MS 30000
// How long to sit in DISCONNECTED before automatically retrying while paired.
// Doubles after each failed attempt (up to the max) so a remote left on while
// the board is away doesn't burn its battery hunting at full duty forever.
#define AUTO_RECONNECT_INTERVAL_MS 10000
#define AUTO_RECONNECT_INTERVAL_MAX_MS 60000

static uint8_t last_saved_channel = 0;

static TaskHandle_t connection_task_handle = NULL;
ConnectionState connection_state = CONNECTION_STATE_DISCONNECTED;
PairingState pairing_state = PAIRING_STATE_UNPAIRED;
static int64_t last_connection_state_change = 0;
static bool auto_reconnect_enabled = true;
static int64_t auto_reconnect_interval_ms = AUTO_RECONNECT_INTERVAL_MS;

void connection_set_auto_reconnect(bool enabled) {
  auto_reconnect_enabled = enabled;
  if (enabled) {
    // Explicit user intent (e.g. menu Connect) restarts the retry cadence
    // from the fast end of the backoff
    auto_reconnect_interval_ms = AUTO_RECONNECT_INTERVAL_MS;
  }
}

bool connection_get_auto_reconnect() {
  return auto_reconnect_enabled;
}

static const char *CONNECTION_STATE_NAMES[] = {"DISCONNECTED", "CONNECTING", "CONNECTED", "RECONNECTING"};

void connection_update_state(ConnectionState state) {
  if (state != connection_state) {
    // Also consumed by tools/bench_check.py for on-air validation
    ESP_LOGI(TAG, "Connection state: %s -> %s", CONNECTION_STATE_NAMES[connection_state],
             CONNECTION_STATE_NAMES[state]);
  }
  connection_state = state;
  last_connection_state_change = get_current_time_ms();

  if (connection_state == CONNECTION_STATE_DISCONNECTED) {
    // Reset all stats when moving to disconnected state
    stats_init();
  }
  else if (connection_state == CONNECTION_STATE_RECONNECTING) {
    remoteStats.signalStrength = -255;
  }
  else if (connection_state == CONNECTION_STATE_CONNECTED) {
    // Successful connection resets the auto-reconnect backoff
    auto_reconnect_interval_ms = AUTO_RECONNECT_INTERVAL_MS;
  }

  stats_update();
}

// Use task rather than a timer so we can do heavy lifting in here
static void connection_task(void *pvParameters) {
  while (1) {
    if (connection_state == CONNECTION_STATE_DISCONNECTED) {
      // Automatically retry while paired so the remote reconnects on its own
      // when the board comes back in range / powers back on. Skipped while the
      // pairing screen owns the radio (pairing_state != PAIRED there) or after
      // an explicit user disconnect.
      if (auto_reconnect_enabled && pairing_state == PAIRING_STATE_PAIRED && comms_is_initialized() &&
          get_current_time_ms() - last_connection_state_change > auto_reconnect_interval_ms) {
        ESP_LOGI(TAG, "Auto-reconnect: retrying connection to paired board");
        // Back off while the board stays away; any successful connection
        // resets the interval (see connection_update_state)
        if (auto_reconnect_interval_ms < AUTO_RECONNECT_INTERVAL_MAX_MS) {
          auto_reconnect_interval_ms *= 2;
        }
        connection_connect_to_default_peer();
        if (connection_state == CONNECTION_STATE_DISCONNECTED) {
          // Connect attempt failed - restart the retry window instead of
          // retrying every loop iteration
          last_connection_state_change = get_current_time_ms();
        }
      }
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
        if (pairing_settings.channel != last_saved_channel) {
          save_pairing_data();
          last_saved_channel = pairing_settings.channel;
        }
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
        // The reconnect sweep may have found the board on a new channel (e.g.
        // its WiFi joined an AP) - persist it, or every wake/reboot pays the
        // full sweep again
        if (pairing_settings.channel != last_saved_channel) {
          save_pairing_data();
          last_saved_channel = pairing_settings.channel;
        }
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
  bool locked = receiver_lock_channel();

  // For ESP-NOW, move the radio to the peer's saved channel right away so
  // reconnects are instant instead of waiting on the channel sweep. (BLE
  // encodes the address type in `channel` with the 0x80 bit, skipped here.)
  // Only touch the radio when we actually hold the channel mutex - otherwise
  // we'd race the receiver task's sweep; the sweep re-aligns us anyway.
  if (locked && comms_get_active_type() == COMMS_TYPE_ESPNOW && channel >= 1 && channel <= 14) {
    comms_set_channel(channel);
  }

  esp_err_t result = comms_connect_peer(mac_addr, channel);

  if (locked) {
    receiver_unlock_channel();
  }

  if (result == ESP_OK) {
    // Any explicit connect re-enables automatic reconnection
    auto_reconnect_enabled = true;
    connection_update_state(CONNECTION_STATE_CONNECTING);
  }
  else {
    ESP_LOGE(TAG, "Failed to connect to peer");
  }
}

void connection_refresh_pairing_state() {
  int8_t default_idx = get_default_device_index();
  if (default_idx >= 0 && default_idx < pairing_settings.device_count) {
    // Restore the active peer fields from the saved device - an aborted
    // pairing attempt may have overwritten remote_addr/channel/secret_code
    set_default_device_index(default_idx);
    pairing_state = PAIRING_STATE_PAIRED;
  }
  else if (pairing_settings.secret_code != DEFAULT_PAIRING_SECRET_CODE) {
    // Legacy single-device pairing data without a devices-list entry
    pairing_state = PAIRING_STATE_PAIRED;
  }
  else {
    pairing_state = PAIRING_STATE_UNPAIRED;
  }
}

void connection_connect_to_default_peer() {
  if (pairing_state == PAIRING_STATE_PAIRED) {
    uint8_t *mac_addr = pairing_settings.remote_addr;
    connection_connect_to_peer(mac_addr, pairing_settings.channel);
  }
}

void connection_init() {
  connection_refresh_pairing_state();

  // Avoid a redundant NVS write when we reconnect on the same channel we
  // already have saved
  last_saved_channel = pairing_settings.channel;

  // start off in connecting mode
  if (pairing_state == PAIRING_STATE_PAIRED) {
    connection_connect_to_default_peer();
  }

  ESP_ERROR_CHECK(
      xTaskCreatePinnedToCore(connection_task, "connection_task", 3072, NULL, 20, &connection_task_handle, 0) == pdPASS
          ? ESP_OK
          : ESP_FAIL);
}

void connection_deinit() {
  if (connection_task_handle != NULL) {
    vTaskDelete(connection_task_handle);
    connection_task_handle = NULL;
  }
}