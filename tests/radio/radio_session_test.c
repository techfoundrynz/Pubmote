#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef int esp_err_t;
typedef int CommsType;
typedef int wifi_mode_t;
enum {
  ESP_OK,
  ESP_ERR_INVALID_STATE,
  ESP_ERR_TIMEOUT,
  ESP_ERR_WIFI_NOT_STARTED,
  ESP_ERR_WIFI_NOT_INIT,
  ESP_ERR_NO_MEM
};
enum {
  COMMS_TYPE_ESPNOW,
  COMMS_TYPE_BLE
};
enum {
  CONNECTION_STATE_DISCONNECTED,
  CONNECTION_STATE_CONNECTED
};
#define pdMS_TO_TICKS(ms) (ms)

static bool owned, prepared, reconnect, auto_reconnect;
static CommsType transport;
static int connection_state, active_transport, reconnects;
static bool automatic, comms_ready, wifi_ready, driver_ready, rx, tx, manager;
static const char *fail;
static char calls[1024];
static esp_err_t record(const char *name) {
  strcat(calls, name);
  strcat(calls, " ");
  return fail && strcmp(fail, name) == 0 ? ESP_ERR_TIMEOUT : ESP_OK;
}
static CommsType comms_get_active_type(void) {
  return active_transport;
}
static bool connection_get_auto_reconnect(void) {
  return automatic;
}
static void connection_set_auto_reconnect(bool value) {
  automatic = value;
}
static void connection_update_state(int value) {
  connection_state = value;
}
static esp_err_t connection_deinit(void) {
  int err = record("stop-manager");
  if (!err)
    manager = false;
  return err;
}
static esp_err_t transmitter_deinit(void) {
  int err = record("stop-tx");
  if (!err)
    tx = false;
  return err;
}
static esp_err_t receiver_deinit(void) {
  int err = record("stop-rx");
  if (!err)
    rx = false;
  return err;
}
static esp_err_t comms_prepare_wifi(void) {
  assert(!rx && !tx && !manager);
  int err = record("prepare");
  if (!err)
    comms_ready = false;
  return err;
}
static esp_err_t wifi_init(void) {
  assert(!comms_ready && !rx && !tx && !manager);
  int err = record("wifi-init");
  if (!err)
    wifi_ready = driver_ready = true;
  return err;
}
static void wifi_set_auto_reconnect(bool value) {
  assert(!value);
}
static bool wifi_is_initialized(void) {
  return wifi_ready;
}
static esp_err_t wifi_uninit(void) {
  int err = record("wifi-stop");
  if (!err)
    wifi_ready = driver_ready = false;
  return err;
}
static bool comms_is_initialized(void) {
  return comms_ready;
}
static esp_err_t comms_deinit(void) {
  int err = record("comms-stop");
  if (!err)
    comms_ready = driver_ready = false;
  return err;
}
static esp_err_t esp_wifi_get_mode(wifi_mode_t *mode) {
  *mode = 1;
  return driver_ready ? ESP_OK : ESP_ERR_WIFI_NOT_INIT;
}
static esp_err_t esp_wifi_stop(void) {
  return record("raw-stop");
}
static esp_err_t esp_wifi_deinit(void) {
  int err = record("raw-deinit");
  if (!err)
    driver_ready = false;
  return err;
}
static esp_err_t comms_select_driver(CommsType type) {
  active_transport = type;
  return ESP_OK;
}
static esp_err_t comms_init(void) {
  assert(!wifi_ready);
  int err = record("comms-init");
  if (!err)
    comms_ready = true;
  return err;
}
static esp_err_t receiver_start(void) {
  assert(comms_ready);
  int err = record("start-rx");
  if (!err)
    rx = true;
  return err;
}
static esp_err_t transmitter_start(void) {
  assert(comms_ready && rx);
  int err = record("start-tx");
  if (!err)
    tx = true;
  return err;
}
static esp_err_t connection_start(void) {
  assert(comms_ready && rx && tx);
  int err = record("start-manager");
  if (!err)
    manager = true;
  return err;
}
static void connection_connect_to_default_peer(void) {
  assert(comms_ready && rx && tx && manager && automatic);
  record("connect");
  ++reconnects;
}
static void vTaskDelay(int ms) {
  assert(ms == 20);
}
#include "radio_session_functions.inc"

static void reset(int type, bool connected, bool intent) {
  owned = prepared = reconnect = auto_reconnect = false;
  connection_state = connected ? CONNECTION_STATE_CONNECTED : CONNECTION_STATE_DISCONNECTED;
  active_transport = type;
  automatic = intent;
  comms_ready = rx = tx = manager = true;
  wifi_ready = driver_ready = false;
  reconnects = 0;
  fail = NULL;
  calls[0] = 0;
}
int main(void) {
  for (int type = COMMS_TYPE_ESPNOW; type <= COMMS_TYPE_BLE; ++type) {
    reset(type, true, true);
    assert(begin_session() == ESP_OK);
    assert(strstr(calls, "stop-manager stop-tx stop-rx prepare wifi-init"));
    assert(begin_session() == ESP_ERR_INVALID_STATE);
    assert(end_session() == ESP_OK);
    assert(active_transport == type && reconnects == 1 && !owned);
    assert(strstr(calls, "wifi-stop comms-init start-rx start-tx start-manager connect"));
    assert(end_session() == ESP_OK && reconnects == 1);
    reset(type, false, true);
    assert(begin_session() == ESP_OK && end_session() == ESP_OK);
    assert(reconnects == 0 && automatic && connection_state == CONNECTION_STATE_DISCONNECTED);
    reset(type, false, false);
    assert(begin_session() == ESP_OK && end_session() == ESP_OK);
    assert(reconnects == 0 && !automatic);
  }
  const char *begin_failures[] = {"stop-manager", "stop-tx", "stop-rx", "prepare", "wifi-init"};
  for (unsigned i = 0; i < sizeof(begin_failures) / sizeof(*begin_failures); ++i) {
    reset(COMMS_TYPE_BLE, true, true);
    fail = begin_failures[i];
    assert(begin_session() == ESP_ERR_TIMEOUT && !wifi_ready);
    if (i < 4)
      assert(!strstr(calls, "wifi-init"));
    fail = NULL;
    assert(end_session() == ESP_OK && reconnects == 1 && !owned);
  }
  const char *end_failures[] = {"wifi-stop", "comms-init", "start-rx", "start-tx", "start-manager"};
  for (unsigned i = 0; i < sizeof(end_failures) / sizeof(*end_failures); ++i) {
    reset(COMMS_TYPE_BLE, true, true);
    assert(begin_session() == ESP_OK);
    fail = end_failures[i];
    assert(end_session() == ESP_ERR_TIMEOUT && owned && reconnects == 0);
    if (i == 0)
      assert(!comms_ready && !strstr(calls, "comms-init"));
    fail = NULL;
    assert(end_session() == ESP_OK && reconnects == 1 && !owned);
  }
  reset(COMMS_TYPE_ESPNOW, true, true);
  fail = "wifi-init";
  assert(begin_session() == ESP_ERR_TIMEOUT);
  driver_ready = true; // A failed IP handoff retained raw Wi-Fi allocations.
  fail = "raw-deinit";
  assert(end_session() == ESP_ERR_TIMEOUT && !comms_ready);
  fail = NULL;
  assert(end_session() == ESP_OK && reconnects == 1);
  puts("radio session handoff, restoration, cancellation recovery and retries passed");
}
