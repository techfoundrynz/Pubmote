#include "remote/radio_session.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "remote/comms.h"
#include "remote/connection.h"
#include "remote/receiver.h"
#include "remote/settings_snapshot.h"
#include "remote/settings_store.h"
#include "remote/transmitter.h"
#include "remote/wifi.h"

typedef struct {
  bool begin;
  radio_session_callback_t callback;
  void *context;
  radio_session_work_t work;
} Request;
static QueueHandle_t requests;
static const char *TAG = "PUBREMOTE-RADIO_SESSION";
static bool owned, prepared, reconnect, auto_reconnect;
static CommsType transport;

static esp_err_t stop_workers(void) {
  esp_err_t err = connection_deinit();
  if (err == ESP_OK)
    err = transmitter_deinit();
  if (err == ESP_OK)
    err = receiver_deinit();
  return err;
}

static esp_err_t start_workers(void) {
  esp_err_t err = receiver_start();
  if (err == ESP_OK)
    err = transmitter_start();
  if (err == ESP_OK)
    err = connection_start();
  return err;
}

esp_err_t radio_session_reset_settings(void) {
  // Quiesce board workers so telemetry/channel persistence cannot recreate
  // settings between the erase and the delayed reboot.
  esp_err_t result = stop_workers();
  if (result == ESP_OK)
    result = reset_all_settings();
  if (result != ESP_OK) {
    vTaskDelay(pdMS_TO_TICKS(20));
    esp_err_t restored = start_workers();
    if (restored != ESP_OK)
      ESP_LOGE(TAG, "Reset recovery failed: %s", esp_err_to_name(restored));
  }
  return result;
}
static esp_err_t begin_session(void) {
  if (owned)
    return ESP_ERR_INVALID_STATE;
  transport = comms_get_active_type();
  auto_reconnect = connection_get_auto_reconnect();
  reconnect = auto_reconnect && connection_get_state() != CONNECTION_STATE_DISCONNECTED;
  owned = true;
  esp_err_t err = stop_workers();
  if (err != ESP_OK)
    return err;
  connection_set_auto_reconnect(false);
  connection_update_state(CONNECTION_STATE_DISCONNECTED);
  prepared = true;
  err = comms_prepare_wifi();
  if (err != ESP_OK)
    return err;
  vTaskDelay(pdMS_TO_TICKS(20));
  return wifi_init();
}

static esp_err_t end_session(void) {
  if (!owned)
    return ESP_OK;
  esp_err_t err = stop_workers();
  if (err != ESP_OK)
    return err;
  connection_set_auto_reconnect(false);
  connection_update_state(CONNECTION_STATE_DISCONNECTED);
  if (prepared) {
    wifi_set_auto_reconnect(false);
    if (wifi_is_initialized()) {
      err = wifi_uninit();
      if (err != ESP_OK)
        return err;
    }
    // A failed handoff can retain the raw ESP-NOW Wi-Fi driver or BLE host.
    if (comms_is_initialized()) {
      err = comms_deinit();
      if (err != ESP_OK)
        return err;
    }
    wifi_mode_t mode;
    err = esp_wifi_get_mode(&mode);
    if (err == ESP_OK) {
      err = esp_wifi_stop();
      if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED)
        return err;
      err = esp_wifi_deinit();
      if (err != ESP_OK)
        return err;
    }
    else if (err != ESP_ERR_WIFI_NOT_INIT)
      return err;
  }
  vTaskDelay(pdMS_TO_TICKS(20));
  if (comms_get_active_type() != transport) {
    err = comms_select_driver(transport);
    if (err != ESP_OK)
      return err;
  }
  err = comms_init();
  if (err == ESP_OK)
    err = start_workers();
  if (err != ESP_OK)
    return err;
  connection_set_auto_reconnect(auto_reconnect);
  if (reconnect)
    connection_connect_to_default_peer();
  owned = prepared = false;
  return ESP_OK;
}

static void worker(void *unused) {
  (void)unused;
  Request request;
  while (true) {
    if (xQueueReceive(requests, &request, portMAX_DELAY) == pdTRUE) {
      esp_err_t result = request.work ? request.work(request.context) : request.begin ? begin_session() : end_session();
      if (result != ESP_OK)
        ESP_LOGE(TAG, "%s failed: %s",
                 request.work    ? "Maintenance"
                 : request.begin ? "Wi-Fi handoff"
                                 : "Board restoration",
                 esp_err_to_name(result));
      else if (!request.work)
        ESP_LOGI(TAG, "%s complete", request.begin ? "Wi-Fi handoff" : "Board restoration");
      request.callback(result, request.context);
    }
  }
}
esp_err_t radio_session_init(void) {
  if (requests)
    return ESP_OK;
  requests = xQueueCreate(4, sizeof(Request));
  if (!requests)
    return ESP_ERR_NO_MEM;
  // NVS and flash operations require an internal RAM stack.
  if (xTaskCreate(worker, "radio-session", 4096, NULL, 4, NULL) != pdPASS) {
    vQueueDelete(requests);
    requests = NULL;
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}
static esp_err_t submit(bool begin, radio_session_callback_t callback, void *context) {
  if (!requests || !callback)
    return ESP_ERR_INVALID_STATE;
  Request request = {begin, callback, context, NULL};
  return xQueueSend(requests, &request, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}
esp_err_t radio_session_begin(radio_session_callback_t callback, void *context) {
  return submit(true, callback, context);
}
esp_err_t radio_session_end(radio_session_callback_t callback, void *context) {
  return submit(false, callback, context);
}
esp_err_t radio_session_run(radio_session_work_t work, radio_session_callback_t callback, void *context) {
  if (!requests || !work || !callback)
    return ESP_ERR_INVALID_STATE;
  Request request = {false, callback, context, work};
  return xQueueSend(requests, &request, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}
