#include "transmitter.h"
#include "commands.h"
#include "comms.h"
#include "config.h"
#include "connection.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_system.h"
#include "peers.h"
#include "receiver.h"
#include "remoteinputs.h"
#include "screens/stats_screen.h"
#include "stats.h"
#include "time.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <remote/settings.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "PUBREMOTE-TRANSMITTER";
#define COMMAND_TIMEOUT 1000
// BLE flow control: hard cap between ANY two sends (the NimBLE mbuf pool is
// shared between TX and RX - sustained TX above the connection's drain rate
// starves inbound notifications and kills telemetry), unchanged-data poke
// cadence, and post-failure backoff
#define BLE_MIN_SEND_INTERVAL_MS 20
#define BLE_POKE_INTERVAL_MS 100
#define BLE_BACKOFF_MS 100
// Rate limit for send-failure error logs
#define TX_ERROR_LOG_INTERVAL_MS 1000

static int64_t last_send_time = 0;
static TaskHandle_t transmitter_task_handle = NULL;

static void on_data_sent(const uint8_t *mac_addr, bool success) {
  // This callback runs in WiFi task context!
  if (success) {
    ESP_LOGD(TAG, "Data sent successfully to %02X:%02X:%02X:%02X:%02X:%02X", mac_addr[0], mac_addr[1], mac_addr[2],
             mac_addr[3], mac_addr[4], mac_addr[5]);
  }
  else {
    // Rate-limited: a failure burst at send rate would saturate the serial
    // log and starve the CPU
    static int64_t last_log_time = 0;
    int64_t now = get_current_time_ms();
    if (connection_state == CONNECTION_STATE_CONNECTED && now - last_log_time > TX_ERROR_LOG_INTERVAL_MS) {
      last_log_time = now;
      ESP_LOGE(TAG, "Failed to send data to %02X:%02X:%02X:%02X:%02X:%02X", mac_addr[0], mac_addr[1], mac_addr[2],
               mac_addr[3], mac_addr[4], mac_addr[5]);
    }
  }
}

#define MAX_UPDATE_DELAY_MS 500

// Function to send ESP-NOW data
static void transmitter_task(void *pvParameters) {
  ESP_ERROR_CHECK(comms_register_send_cb(on_data_sent));
  ESP_LOGI(TAG, "Registered TX callback");

  ESP_LOGI(TAG, "TX task started");
  uint8_t ind = 0;
  uint8_t data[100];

  RemoteData last_message = {};
  ConnectionState last_connection_state = connection_state;
  bool should_emit_version = false;
  int version_retries_remaining = 0;
  int64_t last_version_request_time = 0;
  int64_t ble_backoff_until = 0;
  int64_t last_error_log_time = 0;

  // Subscribe to the task watchdog: a hung TX task means silent loss of
  // control transmission - panic and reboot instead
  ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

  while (1) {
    esp_task_wdt_reset();
    bool tx_failed = false;
    int64_t new_time = get_current_time_ms();

    if (connection_state == CONNECTION_STATE_CONNECTED && last_connection_state != CONNECTION_STATE_CONNECTED) {
      should_emit_version = true;
      version_retries_remaining = 10;
      last_version_request_time = new_time;
    }
    else if (connection_state == CONNECTION_STATE_CONNECTED && remoteStats.vehicleType == VEHICLE_TYPE_UNSPECIFIED &&
             !should_emit_version) {
      // Track cadence from arm time (not send time), otherwise every 20ms
      // iteration spent gated by the send pacing would burn a retry
      if (version_retries_remaining > 0 && new_time - last_version_request_time > 500) {
        should_emit_version = true;
        version_retries_remaining--;
        last_version_request_time = new_time;
      }
    }

    bool should_transmit =
        (connection_state == CONNECTION_STATE_CONNECTED || connection_state == CONNECTION_STATE_RECONNECTING ||
         connection_state == CONNECTION_STATE_CONNECTING);

#if TEST_MODE
    should_transmit = false;
#endif

    RemoteData tx_msg = remote_data;
    if (!is_stats_screen_active() || is_pocket_mode_enabled()) {
      tx_msg.js_y = 0.0f;
      tx_msg.js_x = 0.0f;
      tx_msg.bt_c = false;
      tx_msg.bt_z = false;
      tx_msg.is_rev = false;
    }

    bool telemetry_stale = new_time - remoteStats.lastUpdated > MAX_UPDATE_DELAY_MS;
    bool link_settled = connection_state == CONNECTION_STATE_CONNECTED && !telemetry_stale;
    bool data_changed = memcmp(&tx_msg, &last_message, sizeof(tx_msg)) != 0;
    bool is_ble = comms_get_active_type() == COMMS_TYPE_BLE;

    if (should_transmit && is_ble) {
      // BLE is connection-oriented: there is no channel-sweep dwell to feed
      // and the NimBLE stack has a small TX buffer pool, so pushing harder
      // than the link can drain wedges every subsequent write in ENOMEM
      // (observed on-air). Cap unchanged-data cadence at a 10Hz poke and back
      // off after a failed write to let the pool drain. The poke doubles as
      // the keepalive: the board cuts telemetry 1s after it last heard us, so
      // a sparser settled-link keepalive (500ms) left only two packets of
      // margin - one short TX stall silenced the board and flapped the
      // connection (observed on-air with strong RSSI).
      int64_t min_interval = BLE_POKE_INTERVAL_MS;
      if (new_time < ble_backoff_until) {
        should_transmit = false;
      }
      else if (new_time - last_send_time < BLE_MIN_SEND_INTERVAL_MS) {
        // Hard cap: even changed joystick data (ADC jitter marks nearly every
        // tick as changed) must not exceed the link's drain rate
        should_transmit = false;
      }
      else if (!data_changed && new_time - last_send_time < min_interval) {
        should_transmit = false;
      }
    }
    else if (should_transmit) {
      // ESP-NOW: only dedupe unchanged data while connected AND telemetry is
      // flowing. While hunting (CONNECTING / RECONNECTING) transmit every
      // cycle: the receiver dwells 200ms per channel during the sweep and the
      // board only answers when it hears us, so a 500ms keepalive would leave
      // most dwells silent and make reconnection probabilistic instead of
      // one-sweep deterministic. Similarly, when telemetry stalls while
      // connected, poke the board at full rate so sub-second RF gaps recover
      // before the RECONNECTING threshold trips (the board stops sending 1s
      // after it last heard us).
      if (link_settled && !data_changed && new_time - last_send_time < MAX_UPDATE_DELAY_MS) {
        should_transmit = false;
      }
    }
    else {
      // If not transmitting, reset last send time
      last_send_time = new_time;
    }

    if (should_transmit) {
      uint8_t *mac_addr = pairing_settings.remote_addr;
      if (receiver_lock_channel()) {
        if (should_emit_version) {
          // The version exchange replaces the input packet this tick so the
          // burst stays within the BLE send pacing (max two writes per tick)

          // Send remote version to receiver
          ind = 0;
          data[ind++] = REM_VERSION;
          memcpy(data + ind, &pairing_settings.secret_code, sizeof(int32_t));
          ind += sizeof(int32_t);
          uint8_t version[3] = {VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH};
          memcpy(data + ind, &version, sizeof(version));
          ind += sizeof(version);
          comms_send(mac_addr, data, ind);

          // Request receiver version
          ind = 0;
          data[ind++] = REM_RECEIVER_VERSION;
          memcpy(data + ind, &pairing_settings.secret_code, sizeof(int32_t));
          ind += sizeof(int32_t);
          comms_send(mac_addr, data, ind);

          should_emit_version = false;
          last_send_time = new_time;
        }
        else {
          ind = 0;
          data[ind++] = REM_SET_INPUT_STATE;

          memcpy(data + ind, &pairing_settings.secret_code, sizeof(int32_t));
          ind += sizeof(int32_t);

          // Copy tx_msg after secret_Code
          memcpy(data + ind, &tx_msg, sizeof(tx_msg));
          ind += sizeof(tx_msg);

          esp_err_t result = comms_send(mac_addr, data, ind);

          if (result != ESP_OK) {
            tx_failed = true;
            if (is_ble) {
              // Let the BLE stack drain its TX buffers before trying again
              ble_backoff_until = new_time + BLE_BACKOFF_MS;
            }
            // Rate-limited: at 50Hz a failure burst would otherwise saturate
            // the serial log and starve the CPU
            if (connection_state == CONNECTION_STATE_CONNECTED &&
                new_time - last_error_log_time > TX_ERROR_LOG_INTERVAL_MS) {
              last_error_log_time = new_time;
              uint8_t chann = pairing_settings.channel;
              uint8_t peer_chann = comms_get_peer_channel(mac_addr);
              ESP_LOGE(TAG, "Error sending remote data: %d  - Channel: %d, Peer Channel: %d", result, chann,
                       peer_chann);
            }
          }
          else {
            memcpy(&last_message, &tx_msg, sizeof(tx_msg));
            last_send_time = new_time;
            ESP_LOGD(TAG, "Sent command");
          }
        }

        receiver_unlock_channel();
      }
    }
    // Reset the index for the next data packet and clear the data buffer
    ind = 0;
    memset(data, 0, sizeof(data));

    last_connection_state = connection_state;
    // Only fast-retry failed sends for ESP-NOW while connected: a lost
    // ESP-NOW frame benefits from a quick resend, but a failed BLE write
    // means the stack's TX buffers are full - retrying faster makes it worse
    bool fast_retry = tx_failed && !is_ble && connection_state == CONNECTION_STATE_CONNECTED;
    int64_t target_rate = fast_retry ? (TX_RATE_MS / 4) : TX_RATE_MS;
    int64_t elapsed = get_current_time_ms() - new_time;
    if (elapsed >= 0 && elapsed < target_rate) {
      vTaskDelay(pdMS_TO_TICKS(target_rate - elapsed));
    }
    else {
      vTaskDelay(1);
    }
  }

  // The task will not reach this point as it runs indefinitely
  ESP_LOGI(TAG, "TX task ended");
  esp_task_wdt_delete(NULL);
  vTaskDelete(NULL);
  transmitter_task_handle = NULL;
}

void transmitter_init() {
  // 4096: the send path carries the wrapped-payload stack buffers of the
  // comms drivers (up to ~400 bytes) on this task's stack
  ESP_ERROR_CHECK(xTaskCreatePinnedToCore(transmitter_task, "transmitter_task", 4096, NULL, 20,
                                          &transmitter_task_handle, 0) == pdPASS
                      ? ESP_OK
                      : ESP_FAIL);
}

void transmitter_deinit() {
  if (transmitter_task_handle != NULL) {
    // Unsubscribe from the watchdog first: a deleted-but-subscribed task
    // leaves a dangling entry that can never be fed and would trip the WDT
    esp_task_wdt_delete(transmitter_task_handle);
    vTaskDelete(transmitter_task_handle);
    transmitter_task_handle = NULL;
  }
}