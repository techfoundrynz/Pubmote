#include "connection.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
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
static uint8_t last_saved_channel = 0;

static TaskHandle_t connection_task_handle = NULL;
ConnectionState connection_state = CONNECTION_STATE_DISCONNECTED;
PairingState pairing_state = PAIRING_STATE_UNPAIRED;
static int64_t last_connection_state_change = 0;
// Tracks user link intent (menu connect/disconnect, incompatible receiver).
// There is no periodic self-reconnect: this only gates whether user-initiated
// flows (e.g. pairing-screen teardown) may restore the link.
static bool auto_reconnect_enabled = true;

void connection_set_auto_reconnect(bool enabled) {
  auto_reconnect_enabled = enabled;
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

  stats_update();
}

// Use task rather than a timer so we can do heavy lifting in here
static void connection_task(void *pvParameters) {
  // Subscribe to the task watchdog: a hung connection task means reconnect
  // logic silently stops - panic and reboot instead. Safe here because
  // connect attempts are async (NimBLE completes them via callback).
  ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

  while (1) {
    esp_task_wdt_reset();
    // DISCONNECTED is terminal by design: the remote connects once on boot
    // (connection_init) and after that only on explicit user action (menu
    // connect, pairing-screen teardown restore) - it never retries on its own
    if (connection_state == CONNECTION_STATE_CONNECTED) {
      if (get_current_time_ms() - remoteStats.lastUpdated > RECONNECTING_DURATION_MS) {
        // No data received for a while - update connection state
        connection_update_state(CONNECTION_STATE_RECONNECTING);
      }
    }
    else if (connection_state == CONNECTION_STATE_CONNECTING) {
      if (get_current_time_ms() - last_connection_state_change > TIMEOUT_DURATION_MS) {
        // Never connected - reset the connection state. Also stop the driver's
        // own pursuit (the BLE reconnect timer keeps re-dialing otherwise),
        // so a DISCONNECTED remote is genuinely radio-idle
        connection_update_state(CONNECTION_STATE_DISCONNECTED);
        comms_disconnect_peer(pairing_settings.remote_addr);
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
        // Reconnect failed - reset the connection state and stop the driver's
        // own pursuit (see CONNECTING timeout above)
        connection_update_state(CONNECTION_STATE_DISCONNECTED);
        comms_disconnect_peer(pairing_settings.remote_addr);
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
  esp_task_wdt_delete(NULL);
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
    // Unsubscribe from the watchdog first: a deleted-but-subscribed task
    // leaves a dangling entry that can never be fed and would trip the WDT
    esp_task_wdt_delete(connection_task_handle);
    vTaskDelete(connection_task_handle);
    connection_task_handle = NULL;
  }
}