#include "connection.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_wifi.h"
#include "receiver.h"
#include "remote/comms.h"
#include "remote/stats.h"
#include "remote/stats_lifecycle.h"
#include "remoteinputs.h"
#include "time.h"
#include "transmitter.h"

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <remote/settings.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "PUBREMOTE-CONNECTION";
#define CONNECTION_TIMER_DELAY_MS 20
#define RECONNECTING_DURATION_MS 1000
#define TIMEOUT_DURATION_MS 30000
static uint8_t last_saved_channel = 0;

static _Atomic(TaskHandle_t) connection_task_handle = NULL;
static atomic_bool connection_task_should_exit = false;
static _Atomic(ConnectionState) connection_state = CONNECTION_STATE_DISCONNECTED;
static _Atomic(PairingState) pairing_state = PAIRING_STATE_UNPAIRED;
static int64_t last_connection_state_change = 0;
// Tracks user link intent (menu connect/disconnect, incompatible receiver).
// There is no periodic self-reconnect: this only gates whether user-initiated
// flows (e.g. pairing-screen teardown) may restore the link.
static atomic_bool auto_reconnect_enabled = true;

ConnectionState connection_get_state(void) {
  return atomic_load(&connection_state);
}

PairingState connection_get_pairing_state(void) {
  return atomic_load(&pairing_state);
}

void connection_update_pairing_state(PairingState state) {
  atomic_store(&pairing_state, state);
}

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
    stats_set_signal_strength(-255);
  }

  stats_update();
}

// Use task rather than a timer so we can do heavy lifting in here
static void connection_task(void *pvParameters) {
  // Subscribe to the task watchdog: a hung connection task means reconnect
  // logic silently stops - panic and reboot instead. Safe here because
  // connect attempts are async (NimBLE completes them via callback).
  ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

  while (!connection_task_should_exit) {
    esp_task_wdt_reset();
    const RemoteStats telemetry = stats_snapshot();
    PairingSettings peer = settings_get_pairing();
    // DISCONNECTED is terminal by design: the remote connects once on boot
    // (connection_init) and after that only on explicit user action (menu
    // connect, pairing-screen teardown restore) - it never retries on its own
    if (connection_state == CONNECTION_STATE_CONNECTED) {
      int64_t data_gap_ms = get_current_time_ms() - telemetry.lastUpdated;
      if (data_gap_ms > RECONNECTING_DURATION_MS) {
        // No data received for a while - update connection state. Log the
        // link diagnostics before RECONNECTING resets signalStrength
        ESP_LOGW(TAG, "Link stale: no data for %lldms (comms=%s, channel=%d, last RSSI=%d)", data_gap_ms,
                 comms_get_active_type() == COMMS_TYPE_BLE ? "BLE" : "ESPNOW", peer.channel, telemetry.signalStrength);
        connection_update_state(CONNECTION_STATE_RECONNECTING);
      }
    }
    else if (connection_state == CONNECTION_STATE_CONNECTING) {
      if (get_current_time_ms() - last_connection_state_change > TIMEOUT_DURATION_MS) {
        // Never connected - reset the connection state. Also stop the driver's
        // own pursuit (the BLE reconnect timer keeps re-dialing otherwise),
        // so a DISCONNECTED remote is genuinely radio-idle
        connection_update_state(CONNECTION_STATE_DISCONNECTED);
        comms_disconnect_peer(peer.remote_addr);
      }
      else if (telemetry.lastUpdated > 0 && get_current_time_ms() - telemetry.lastUpdated < RECONNECTING_DURATION_MS) {
        // Connected - update connection state
        connection_update_state(CONNECTION_STATE_CONNECTED);
        // Save pairing data. This way we remember the last channel we connected on
        if (peer.channel != last_saved_channel) {
          save_pairing_data();
          last_saved_channel = peer.channel;
        }
      }
    }
    else if (connection_state == CONNECTION_STATE_RECONNECTING) {
      if (get_current_time_ms() - last_connection_state_change > TIMEOUT_DURATION_MS) {
        // Reconnect failed - reset the connection state and stop the driver's
        // own pursuit (see CONNECTING timeout above)
        connection_update_state(CONNECTION_STATE_DISCONNECTED);
        comms_disconnect_peer(peer.remote_addr);
      }
      else if (get_current_time_ms() - telemetry.lastUpdated < RECONNECTING_DURATION_MS) {
        // Reconnected - update connection state
        connection_update_state(CONNECTION_STATE_CONNECTED);
        // The reconnect sweep may have found the board on a new channel (e.g.
        // its WiFi joined an AP) - persist it, or every wake/reboot pays the
        // full sweep again
        if (peer.channel != last_saved_channel) {
          save_pairing_data();
          last_saved_channel = peer.channel;
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(CONNECTION_TIMER_DELAY_MS));
  }

  ESP_LOGI(TAG, "Connection management task ended");
  esp_task_wdt_delete(NULL);
  connection_task_handle = NULL;
  vTaskDelete(NULL);
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
  if (default_idx >= 0 && default_idx < settings_get_pairing().device_count) {
    // Restore the active peer fields from the saved device - an aborted
    // pairing attempt may have overwritten remote_addr/channel/secret_code
    set_default_device_index(default_idx);
    pairing_state = PAIRING_STATE_PAIRED;
  }
  else if (settings_get_pairing().secret_code != DEFAULT_PAIRING_SECRET_CODE) {
    // Legacy single-device pairing data without a devices-list entry
    pairing_state = PAIRING_STATE_PAIRED;
  }
  else {
    pairing_state = PAIRING_STATE_UNPAIRED;
  }
}

esp_err_t connection_switch_comms_mode(CommsType type) {
  // This may wait for tasks and the controller; callers must use a worker.
  esp_err_t err = connection_deinit();
  if (err != ESP_OK)
    return err;
  connection_update_state(CONNECTION_STATE_DISCONNECTED);
  err = transmitter_deinit();
  if (err == ESP_OK)
    err = receiver_deinit();
  if (err != ESP_OK)
    return err;
  err = comms_select_driver(type);
  if (err == ESP_OK)
    err = comms_init();
  if (err != ESP_OK && comms_get_active_type() == type && comms_is_initialized()) {
    // A partially initialized/stopped host must drain before a retry can start it.
    esp_err_t cleanup = comms_deinit();
    if (cleanup == ESP_OK)
      err = comms_init();
  }
  if (err == ESP_OK)
    err = receiver_start();
  if (err == ESP_OK)
    err = transmitter_start();
  if (err == ESP_OK)
    err = connection_start();
  return err;
}

void connection_connect_to_default_peer() {
  if (pairing_state == PAIRING_STATE_PAIRED) {
    PairingSettings peer = settings_get_pairing();
    uint8_t *mac_addr = peer.remote_addr;
    connection_connect_to_peer(mac_addr, peer.channel);
  }
}

esp_err_t connection_start(void) {
  if (connection_task_handle)
    return connection_task_should_exit ? ESP_ERR_INVALID_STATE : ESP_OK;
  connection_refresh_pairing_state();

  // Avoid a redundant NVS write when we reconnect on the same channel we
  // already have saved
  last_saved_channel = settings_get_pairing().channel;

  connection_task_should_exit = false;
  TaskHandle_t handle = NULL;
  if (xTaskCreatePinnedToCore(connection_task, "connection_task", 3072, NULL, 20, &handle, 0) != pdPASS)
    return ESP_ERR_NO_MEM;
  connection_task_handle = handle;
  return ESP_OK;
}

void connection_init(void) {
  ESP_ERROR_CHECK(connection_start());
  connection_connect_to_default_peer();
}

esp_err_t connection_deinit(void) {
  connection_task_should_exit = true;
  for (int i = 0; i < 200 && connection_task_handle; ++i) {
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  return connection_task_handle ? ESP_ERR_TIMEOUT : ESP_OK;
}
