#include "receiver.h"
#include "config.h"
#include "commands.h"
#include "comms.h"
#include "connection.h"
#include "display.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "pairing.h"
#include "peers.h"
#include "powermanagement.h"
#include "screens/pairing_screen.h"
#include "settings.h"
#include "stats.h"
#include "time.h"
#include "utilities/conversion_utils.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "PUBREMOTE-RECEIVER";
#define RX_QUEUE_SIZE 10

static TaskHandle_t receiver_task_handle = NULL;
static QueueHandle_t comms_queue = NULL;

static void on_comms_data_recv(const uint8_t *src_mac, const uint8_t *data, int len, uint8_t channel, int rssi) {
  // This callback runs in WiFi task context!
  ESP_LOGD(TAG, "RECEIVED");
  comms_event_t evt;
  memcpy(evt.mac_addr, src_mac, COMMS_MAC_LEN);
  evt.data = malloc(len);
  memcpy(evt.data, data, len);
  evt.len = len;
  evt.chan = channel;
  remoteStats.signalStrength = rssi;

#if RX_QUEUE_SIZE > 1
  // Send to queue for processing in application task
  if (uxQueueSpacesAvailable(comms_queue) == 0) {
    // reset the queue
    xQueueReset(comms_queue);
  }
  if (xQueueSend(comms_queue, &evt, portMAX_DELAY) != pdTRUE) {
#else
  // overwrite the previous data
  if (xQueueOverwrite(comms_queue, &evt) != pdTRUE) {
#endif
    ESP_LOGE(TAG, "Queue send failed");
    free(evt.data);
  }
}

static void process_data(comms_event_t evt) {
  uint8_t *data = evt.data;
  int len = evt.len;

  bool is_pairing_start = pairing_state == PAIRING_STATE_UNPAIRED && is_pairing_screen_active();
  // Check mac for security on anything other than initial pairing
  if (!comms_is_same_mac(evt.mac_addr, pairing_settings.remote_addr) && !is_pairing_start) {
    ESP_LOGD(TAG, "Ignoring data from unknown MAC");
    return;
  }

  const uint8_t *payload_data = data;
  int payload_len = len;
  if (!comms_strip_headers(&payload_data, &payload_len, comms_get_active_type())) {
    ESP_LOGD(TAG, "Ignoring data: missing required magic headers");
    return;
  }

  // Update variables for subsequent use in the function
  data = (uint8_t *)payload_data;
  len = payload_len;

  RemoteCommands command = (RemoteCommands)data[0];
  len -= 1; // Remove command byte from length
  if (len < 0) {
    ESP_LOGE(TAG, "Invalid data length: %d", len);
    return;
  }

  data += 1; // Move data pointer to the actual data

  ESP_LOGD(TAG, "Command: %d", command);

  switch (command) {
  case REM_VERSION:
    ESP_LOGI(TAG, "Rec: Version: %d", data[1]);
    // TODO - send back receiver version
    break;
  case REM_RECEIVER_VERSION: {
    if (len >= 5) {
      uint16_t api_version = data[4];
      if (len >= 6) {
        api_version |= (data[5] << 8);
      }
      ESP_LOGI(TAG, "Rec: Receiver API version: %d", api_version);

      // Check if the receiver API version is below the minimum required version
      if (api_version < MIN_RCV_API_VERSION) {
        handle_receiver_api_version_too_low(api_version);
      }

      if (len >= 7) {
        uint8_t vehicle_type = data[6];
        ESP_LOGI(TAG, "Rec: Vehicle type: %d", vehicle_type);
        remoteStats.vehicleType = vehicle_type;

        // Persist if it changed for the current default device
        int default_idx = get_default_device_index();
        if (default_idx >= 0 && default_idx < pairing_settings.device_count) {
          if (pairing_settings.devices[default_idx].vehicle_type != vehicle_type) {
            pairing_settings.devices[default_idx].vehicle_type = vehicle_type;
            save_pairing_data();
          }
        }
      } else {
        remoteStats.vehicleType = VEHICLE_TYPE_UNSPECIFIED;
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
    if (pairing_state == PAIRING_STATE_PAIRING && is_pairing_screen_active()) {
      ESP_LOGI(TAG, "Process: Pairing bond");
      pairing_process_bond_event(data, len);
    }
    break;
  case REM_PAIR_COMPLETE:
    if (pairing_state == PAIRING_STATE_PENDING && is_pairing_screen_active()) {
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
  receiver_lock_channel();

  comms_set_channel(chan);
  pairing_settings.channel = chan;

  if (!is_pairing) {
    // Add peer so we can send if we're already paired
    uint8_t *mac_addr = pairing_settings.remote_addr;
    comms_connect_peer(mac_addr, chan);
  }

  receiver_unlock_channel();
}

static void receiver_task(void *pvParameters) {
  comms_queue = xQueueCreate(RX_QUEUE_SIZE, sizeof(comms_event_t));
  ESP_ERROR_CHECK(comms_register_recv_cb(on_comms_data_recv));
  ESP_LOGI(TAG, "Registered RX callback");
  comms_event_t evt;
  // Hop through channels if in pairing mode or connecting
  uint64_t channel_switch_time_ms = 0;

  while (1) {
    if (xQueueReceive(comms_queue, &evt, 0) == pdTRUE) {
      process_data(evt);
      free(evt.data);
      // reset channel switch time
      channel_switch_time_ms = 0;
    }
    else {
      bool is_pairing = pairing_state == PAIRING_STATE_UNPAIRED && is_pairing_screen_active();
      bool is_connecting = connection_state == CONNECTION_STATE_CONNECTING;
      // Nothing received while connecting or pairing - hop through channels (only for ESP-NOW)
      if ((is_connecting || is_pairing) && comms_get_active_type() == COMMS_TYPE_ESPNOW) {
        if (channel_switch_time_ms > CHANNEL_HOP_INTERVAL_MS) {
// Hop to next channel
#define NUM_AVAIL_WIFI_CHANNELS 14

          uint8_t next_channel = (pairing_settings.channel % NUM_AVAIL_WIFI_CHANNELS) + 1;
          change_channel(next_channel, is_pairing);
          channel_switch_time_ms = 0;
        }
        else {
          channel_switch_time_ms += RECEIVER_TASK_DELAY_MS;
        }
      }
      else {
        // reset channel switch time
        channel_switch_time_ms = 0;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(RECEIVER_TASK_DELAY_MS));
  }

  // The task will not reach this point as it runs indefinitely
  ESP_LOGI(TAG, "RX task ended");
  vTaskDelete(NULL);
  receiver_task_handle = NULL;
}

void receiver_init() {
  ESP_LOGI(TAG, "Starting receiver task");
  ESP_ERROR_CHECK(xTaskCreatePinnedToCore(receiver_task, "receiver_task", 4096, NULL, 20, &receiver_task_handle, 0) ==
                          pdPASS
                      ? ESP_OK
                      : ESP_FAIL);
}

void receiver_deinit() {
  if (receiver_task_handle != NULL) {
    vTaskDelete(receiver_task_handle);
    receiver_task_handle = NULL;
  }

  if (comms_queue != NULL) {
    vQueueDelete(comms_queue);
    comms_queue = NULL;
  }
}