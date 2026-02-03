#include "receiver.h"
#include "commands.h"
#include "connection.h"
#include "display.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "espnow.h"
#include "pairing.h"
#include "peers.h"
#include "powermanagement.h"
#include "screens/pairing_screen.h"
#include "stats.h"
#include "time.h"
#include "utilities/conversion_utils.h"
#include <freertos/queue.h>
#include <math.h>
#include <remote/settings.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "PUBREMOTE-RECEIVER";
#define RX_QUEUE_SIZE 10

static TaskHandle_t receiver_task_handle = NULL;
static QueueHandle_t espnow_queue;

static void on_data_recv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
  // This callback runs in WiFi task context!
  ESP_LOGD(TAG, "RECEIVED");
  esp_now_event_t evt;
  memcpy(evt.mac_addr, recv_info->src_addr, ESP_NOW_ETH_ALEN);
  evt.data = malloc(len);
  memcpy(evt.data, data, len);
  evt.len = len;
  evt.chan = recv_info->rx_ctrl->channel;
  remoteStats.signalStrength = recv_info->rx_ctrl->rssi;

#if RX_QUEUE_SIZE > 1
  // Send to queue for processing in application task
  if (uxQueueSpacesAvailable(espnow_queue) == 0) {
    // reset the queue
    xQueueReset(espnow_queue);
  }
  if (xQueueSend(espnow_queue, &evt, portMAX_DELAY) != pdTRUE) {
#else
  // overwrite the previous data
  if (xQueueOverwrite(espnow_queue, &evt) != pdTRUE) {
#endif
    ESP_LOGE(TAG, "Queue send failed");
    free(evt.data);
  }
}

static void process_data(esp_now_event_t evt) {
  uint8_t *data = evt.data;
  int len = evt.len;

  bool is_pairing_start = pairing_state == PAIRING_STATE_UNPAIRED && is_pairing_screen_active();
  // Check mac for security on anything other than initial pairing
  if (!is_same_mac(evt.mac_addr, pairing_settings.remote_addr) && !is_pairing_start) {
    ESP_LOGD(TAG, "Ignoring data from unknown MAC");
    return;
  }

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
  case REM_RECEIVER_VERSION:
    ESP_LOGI(TAG, "Rec: Receiver version: %d", data[1]);
    break;
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
    ESP_LOGE(TAG, "Unknown command: %d", command);
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

  esp_wifi_set_channel(chan, WIFI_SECOND_CHAN_NONE);
  pairing_settings.channel = chan;

  if (!is_pairing) {
    // Add peer so we can send if we're already paired
    uint8_t *mac_addr = pairing_settings.remote_addr;

    if (esp_now_is_peer_exist(mac_addr)) {
      esp_now_del_peer(mac_addr);
    }

    esp_now_peer_info_t peerInfo = {};
    peerInfo.channel = chan;
    peerInfo.encrypt = false;
    memcpy(peerInfo.peer_addr, mac_addr, ESP_NOW_ETH_ALEN);
    esp_now_add_peer(&peerInfo);
  }

  receiver_unlock_channel();
}

static void receiver_task(void *pvParameters) {
  espnow_queue = xQueueCreate(RX_QUEUE_SIZE, sizeof(esp_now_event_t));
  ESP_ERROR_CHECK(esp_now_register_recv_cb(on_data_recv));
  ESP_LOGI(TAG, "Registered RX callback");
  esp_now_event_t evt;
  // Hop through channels if in pairing mode or connecting
  uint64_t channel_switch_time_ms = 0;

  while (1) {
    if (xQueueReceive(espnow_queue, &evt, 0) == pdTRUE) {
      process_data(evt);
      free(evt.data);
      // reset channel switch time
      channel_switch_time_ms = 0;
    }
    else {
      bool is_pairing = pairing_state == PAIRING_STATE_UNPAIRED && is_pairing_screen_active();
      bool is_connecting = connection_state == CONNECTION_STATE_CONNECTING;
      // Nothing received while connecting or pairing - hop through channels
      if (is_connecting || is_pairing) {
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
}

void receiver_init() {
  ESP_LOGI(TAG, "Starting receiver task");
  xTaskCreatePinnedToCore(receiver_task, "receiver_task", 4096, NULL, 20, &receiver_task_handle, 0);
}

void receiver_deinit() {
  if (receiver_task_handle != NULL) {
    vTaskDelete(receiver_task_handle);
    receiver_task_handle = NULL;
  }

  if (espnow_queue != NULL) {
    vQueueDelete(espnow_queue);
    espnow_queue = NULL;
  }
}