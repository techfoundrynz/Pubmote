#include "receiver.h"
#include "commands.h"
#include "config.h"
#include "connection.h"
#include "display.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "pairing.h"
#include "powermanagement.h"
#include "remote/comms.h"
#include "remote/protocol.h"
#include "remote/settings_state.h"
#include "remote/stats.h"
#include "settings.h"
#include "time.h"
#include "ui/screen_status.h"
#include "utilities/conversion_utils.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "PUBREMOTE-RECEIVER";
#define RX_QUEUE_SIZE 10

static _Atomic(TaskHandle_t) receiver_task_handle = NULL;
static QueueHandle_t comms_queue = NULL;
static SemaphoreHandle_t queue_mutex = NULL;
// Cooperative shutdown: the task must exit at its loop boundary, never be
// force-deleted (it could be holding channel_mutex mid change_channel, and a
// killed owner leaks a non-recursive mutex forever)
static atomic_bool receiver_task_should_exit = false;

static void on_comms_data_recv(const uint8_t *src_mac, const uint8_t *data, int len, uint8_t channel, int rssi) {
  // This callback runs in WiFi/BLE host task context!
  ESP_LOGD(TAG, "RECEIVED");
  if (len <= 0 || !queue_mutex || xSemaphoreTake(queue_mutex, 0) != pdTRUE) {
    return;
  }
  if (!comms_queue) {
    xSemaphoreGive(queue_mutex);
    return;
  }

  comms_event_t evt;
  memcpy(evt.mac_addr, src_mac, COMMS_MAC_LEN);
  evt.data = malloc(len);
  if (evt.data == NULL) {
    ESP_LOGE(TAG, "RX allocation failed (%d bytes)", len);
    xSemaphoreGive(queue_mutex);
    return;
  }
  memcpy(evt.data, data, len);
  evt.len = len;
  evt.chan = channel;
  evt.rssi = rssi;

#if RX_QUEUE_SIZE > 1
  // Send to queue for processing in application task
  if (uxQueueSpacesAvailable(comms_queue) == 0) {
    // If the queue is full, remove and free the oldest event to make room
    comms_event_t old_evt;
    if (xQueueReceive(comms_queue, &old_evt, 0) == pdTRUE) {
      free(old_evt.data);
    }
  }
  // Never block the radio task; a slot was freed above if needed
  if (xQueueSend(comms_queue, &evt, 0) != pdTRUE) {
#else
  // overwrite the previous data
  if (xQueueOverwrite(comms_queue, &evt) != pdTRUE) {
#endif
    ESP_LOGE(TAG, "Queue send failed");
    free(evt.data);
  }
  xSemaphoreGive(queue_mutex);
}

static void process_data(comms_event_t evt) {
  uint8_t *data = evt.data;
  int len = evt.len;

  bool is_pairing_start = connection_get_pairing_state() == PAIRING_STATE_UNPAIRED && is_pairing_screen_active();
  // Check mac for security on anything other than initial pairing
  if (!comms_is_same_mac(evt.mac_addr, settings_get_pairing().remote_addr) && !is_pairing_start) {
    ESP_LOGD(TAG, "Ignoring data from unknown MAC");
    return;
  }

  // Only track signal strength for packets from our paired peer (or pairing)
  stats_set_signal_strength(evt.rssi);

  const uint8_t *payload_data = data;
  int payload_len = len;
  if (!comms_strip_headers(&payload_data, &payload_len, comms_get_active_type())) {
    ESP_LOGD(TAG, "Ignoring data: missing required magic headers");
    return;
  }

  // Update variables for subsequent use in the function
  data = (uint8_t *)payload_data;
  len = payload_len;

  if (len < 1) {
    ESP_LOGE(TAG, "Invalid data length: %d", len);
    return;
  }

  RemoteCommands command = (RemoteCommands)data[0];
  len -= 1; // Remove command byte from length

  data += 1; // Move data pointer to the actual data

  ESP_LOGD(TAG, "Command: %d", command);

  switch (command) {
  case REM_VERSION:
    if (len >= 2) {
      ESP_LOGI(TAG, "Rec: Version: %d", data[1]);
    }
    // TODO - send back receiver version
    break;
  case REM_RECEIVER_VERSION: {
    ReceiverVersion version;
    if (protocol_decode_receiver_version(data, (size_t)len, &version)) {
      uint16_t api_version = version.api_version;
      ESP_LOGI(TAG, "Rec: Receiver API version: %d", api_version);

      // Check if the receiver API version is below the minimum required version
      if (api_version < MIN_RCV_API_VERSION) {
        handle_receiver_api_version_too_low(api_version);
      }

      if (version.has_vehicle_type) {
        uint8_t vehicle_type = version.vehicle_type;
        ESP_LOGI(TAG, "Rec: Vehicle type: %d", vehicle_type);
        stats_set_vehicle_type(vehicle_type);

        // Persist if it changed for the current default device
        if (settings_set_saved_vehicle(evt.mac_addr, vehicle_type)) {
          save_pairing_data();
        }
      }
      else {
        stats_set_vehicle_type(VEHICLE_TYPE_UNSPECIFIED);
      }
      stats_update();
    }
    break;
  }
  case REM_PAIR_INIT:
    if (is_pairing_start) {
      ESP_LOGI(TAG, "Process: Pairing init");
      pairing_process_init_event(data, len, evt);
    }
    break;
  case REM_PAIR_BOND:
    if (connection_get_pairing_state() == PAIRING_STATE_PAIRING && is_pairing_screen_active()) {
      ESP_LOGI(TAG, "Process: Pairing bond");
      pairing_process_bond_event(data, len);
    }
    break;
  case REM_PAIR_COMPLETE:
    if (connection_get_pairing_state() == PAIRING_STATE_PENDING && is_pairing_screen_active()) {
      ESP_LOGI(TAG, "Process: Pairing complete");
      pairing_process_completion_event(data, len);
    }
    break;
  case REM_SET_CORE_DATA:
    ESP_LOGD(TAG, "Rec: Set data");
    process_board_data(data, len);
    break;
  default:
    if (command != 135) { // Ignore VESC Lisp print command (COMM_LISP_PRINT = 135)
      ESP_LOGE(TAG, "Unknown command: %d", command);
    }
    break;
  }
}

#define CHANNEL_HOP_INTERVAL_MS 200
#define RECEIVER_TASK_DELAY_MS 5
// How long to stay on the saved channel after losing a connection before
// sweeping other channels (the board may have moved, e.g. its WiFi joined an
// AP on a different channel)
#define RECONNECT_HOP_GRACE_MS 3000
#define NUM_AVAIL_WIFI_CHANNELS 14

// Mutex to protect channel switching
static SemaphoreHandle_t channel_mutex;

bool receiver_lock_channel() {
  if (channel_mutex == NULL) {
    channel_mutex = xSemaphoreCreateMutex();
  }
  return xSemaphoreTake(channel_mutex, pdMS_TO_TICKS(1000)) == pdTRUE;
}

void receiver_unlock_channel() {
  if (channel_mutex == NULL) {
    channel_mutex = xSemaphoreCreateMutex();
  }

  xSemaphoreGive(channel_mutex);
}

static void change_channel(uint8_t chan, bool is_pairing) {
  ESP_LOGI(TAG, "Switching to channel %d", chan);
  if (!receiver_lock_channel()) {
    ESP_LOGW(TAG, "Channel lock busy, skipping switch to %d", chan);
    return;
  }

  // Only commit the channel if the radio accepted it - some channels (12-14)
  // are rejected under certain regulatory configs
  if (comms_set_channel(chan) == ESP_OK) {
    settings_set_channel(chan);

    if (!is_pairing) {
      // Add peer so we can send if we're already paired
      PairingSettings peer = settings_get_pairing();
      uint8_t *mac_addr = peer.remote_addr;
      comms_connect_peer(mac_addr, chan);
    }
  }
  else {
    ESP_LOGW(TAG, "Failed to set channel %d, skipping", chan);
  }

  receiver_unlock_channel();
}

static void receiver_task(void *pvParameters) {
  ESP_LOGI(TAG, "Registered RX callback");
  comms_event_t evt;
  // Hop through channels if in pairing mode, connecting, or stuck reconnecting
  uint64_t channel_switch_time_ms = 0;
  // Sweep cursor - kept separate from settings_get_pairing().channel so that
  // channels rejected by the radio (regulatory limits) don't wedge the sweep
  uint8_t hop_cursor = 0;

  // Subscribe to the task watchdog: a hung RX task means silent loss of
  // telemetry and channel management - panic and reboot instead
  ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

  while (!receiver_task_should_exit) {
    esp_task_wdt_reset();
    if (xQueueReceive(comms_queue, &evt, 0) == pdTRUE) {
      process_data(evt);
      free(evt.data);
      // reset channel switch time
      channel_switch_time_ms = 0;
    }
    else {
      bool is_pairing = connection_get_pairing_state() == PAIRING_STATE_UNPAIRED && is_pairing_screen_active();
      bool is_connecting = connection_get_state() == CONNECTION_STATE_CONNECTING;
      // If a reconnect has stalled, the board may have moved channels (e.g.
      // its WiFi joined an AP) - sweep for it after a grace period
      bool is_reconnect_stale = connection_get_state() == CONNECTION_STATE_RECONNECTING &&
                                get_current_time_ms() - stats_snapshot().lastUpdated > RECONNECT_HOP_GRACE_MS;
      // Nothing received while connecting or pairing - hop through channels (only for ESP-NOW)
      if ((is_connecting || is_pairing || is_reconnect_stale) && comms_get_active_type() == COMMS_TYPE_ESPNOW) {
        if (channel_switch_time_ms > CHANNEL_HOP_INTERVAL_MS) {
          // Hop to next channel
          if (hop_cursor == 0) {
            hop_cursor = settings_get_pairing().channel;
          }
          hop_cursor = (hop_cursor % NUM_AVAIL_WIFI_CHANNELS) + 1;
          change_channel(hop_cursor, is_pairing);
          channel_switch_time_ms = 0;
        }
        else {
          channel_switch_time_ms += RECEIVER_TASK_DELAY_MS;
        }
      }
      else {
        // reset channel switch time
        channel_switch_time_ms = 0;
        hop_cursor = 0;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(RECEIVER_TASK_DELAY_MS));
  }

  ESP_LOGI(TAG, "RX task ended");
  esp_task_wdt_delete(NULL);
  receiver_task_handle = NULL;
  vTaskDelete(NULL);
}

esp_err_t receiver_start(void) {
  if (receiver_task_handle != NULL) {
    return receiver_task_should_exit ? ESP_ERR_INVALID_STATE : ESP_OK;
  }
  if (!queue_mutex)
    queue_mutex = xSemaphoreCreateMutex();
  if (!queue_mutex)
    return ESP_ERR_NO_MEM;
  if (xSemaphoreTake(queue_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
    return ESP_ERR_TIMEOUT;
  if (!comms_queue)
    comms_queue = xQueueCreate(RX_QUEUE_SIZE, sizeof(comms_event_t));
  if (!comms_queue) {
    xSemaphoreGive(queue_mutex);
    return ESP_ERR_NO_MEM;
  }
  ESP_LOGI(TAG, "Starting receiver task");
  receiver_task_should_exit = false;
  TaskHandle_t handle = NULL;
  esp_err_t err = comms_register_recv_cb(on_comms_data_recv);
  if (err == ESP_OK && xTaskCreatePinnedToCore(receiver_task, "receiver_task", 4096, NULL, 20, &handle, 0) != pdPASS)
    err = ESP_ERR_NO_MEM;
  receiver_task_handle = handle;
  if (err != ESP_OK) {
    comms_register_recv_cb(NULL);
    vQueueDelete(comms_queue);
    comms_queue = NULL;
  }
  xSemaphoreGive(queue_mutex);
  return err;
}

void receiver_init(void) {
  ESP_ERROR_CHECK(receiver_start());
}

esp_err_t receiver_deinit(void) {
  // Stop the driver from posting to the queue before tearing it down
  comms_register_recv_cb(NULL);

  // Ask the task to exit at its loop boundary and wait for it (bounded).
  // Never force-delete: it could be holding channel_mutex inside
  // change_channel, and a killed owner leaks the mutex forever.
  if (receiver_task_handle != NULL) {
    receiver_task_should_exit = true;
    for (int i = 0; i < 200 && receiver_task_handle != NULL; i++) {
      vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (receiver_task_handle != NULL) {
      ESP_LOGE(TAG, "Receiver task did not exit in time");
      return ESP_ERR_TIMEOUT; // Leave the queue alive rather than free it under the task
    }
  }
  if (!queue_mutex)
    return ESP_OK;
  if (xSemaphoreTake(queue_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
    return ESP_ERR_TIMEOUT;
  if (comms_queue != NULL) {
    // Drain any queued events so their payloads don't leak
    comms_event_t evt;
    while (xQueueReceive(comms_queue, &evt, 0) == pdTRUE) {
      free(evt.data);
    }
    QueueHandle_t queue = comms_queue;
    comms_queue = NULL;
    vQueueDelete(queue);
  }
  xSemaphoreGive(queue_mutex);
  return ESP_OK;
}
