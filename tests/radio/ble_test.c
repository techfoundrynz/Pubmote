#include "common.h"
#define BLE_HS_CONN_HANDLE_NONE UINT16_MAX
#define BLE_ERR_REM_USER_CONN_TERM 1
#define BLE_HS_ENOTCONN 2
#define COMMS_TYPE_BLE 1
static atomic_bool is_initialized, is_synced, shutting_down, has_target_peer;
static atomic_uint session_generation;
static _Atomic uint16_t ble_conn_handle, nus_tx_handle, nus_rx_handle;
static bool host_stopped, cccd_subscribed, discovery_complete;
static unsigned rx_stream_len;
static fake_mutex_t tx_lock, host_lock;
static SemaphoreHandle_t tx_mutex = &tx_lock, host_queue_mutex = &host_lock;
static void *reconnect_timer, *rssi_poll_task_handle;
static bool rssi_poll_should_exit;
static unsigned stop_calls, deinit_calls, timer_deletes, stop_failures, deinit_failures;
static unsigned writes, callbacks, successful_callbacks, dummy_writes, terminate_calls;
static unsigned write_behavior;
struct ble_npl_event {
  int unused;
};
static struct ble_npl_event reconnect_event;
static unsigned event_deinits;
static void ble_npl_event_deinit(struct ble_npl_event *event) {
  (void)event;
  assert(host_stopped);
  ++event_deinits;
}
static void sent(const uint8_t *mac, bool success) {
  (void)mac;
  ++callbacks;
  successful_callbacks += success;
}
static void (*registered_send_cb)(const uint8_t *, bool) = sent;
static void send_connection_init_dummy(void) {
  ++dummy_writes;
}
static int esp_timer_stop(void *timer) {
  (void)timer;
  return 0;
}
static int esp_timer_delete(void *timer) {
  (void)timer;
  ++timer_deletes;
  return 0;
}
static int ble_gap_terminate(uint16_t conn, int reason) {
  (void)conn;
  (void)reason;
  ++terminate_calls;
  return 0;
}
static int ble_gap_conn_active(void) {
  return 0;
}
static int ble_gap_conn_cancel(void) {
  return 0;
}
static int ble_gap_disc_cancel(void) {
  return 0;
}
static int nimble_port_stop(void) {
  ++stop_calls;
  if (stop_failures) {
    --stop_failures;
    return 1;
  }
  return 0;
}
static int nimble_port_deinit(void) {
  ++deinit_calls;
  if (deinit_failures) {
    --deinit_failures;
    return ESP_FAIL;
  }
  return 0;
}
static uint16_t ble_att_mtu(uint16_t conn) {
  (void)conn;
  return 23;
}
static size_t comms_write_headers(uint8_t *out, size_t capacity, const uint8_t *data, size_t len, int type) {
  (void)type;
  if (len + 2 > capacity)
    return 0;
  out[0] = 36;
  out[1] = 169;
  memcpy(out + 2, data, len);
  return len + 2;
}
static size_t vesc_wrap_packet(const uint8_t *data, size_t len, uint8_t *out, size_t capacity) {
  assert(len <= capacity);
  memcpy(out, data, len);
  return len;
}
static esp_err_t ble_driver_send(const uint8_t *, const uint8_t *, size_t);
static int ble_gattc_write_no_rsp_flat(uint16_t conn, uint16_t handle, const void *data, size_t len) {
  (void)data;
  assert(conn == 7);
  assert(handle == 9);
  assert(len <= 20);
  assert(tx_lock.held);
  ++writes;
  if (write_behavior == 1) {
    atomic_fetch_add(&session_generation, 1);
    ble_conn_handle = 8;
  }
  if (write_behavior == 2 && writes == 1) {
    uint8_t nested[1] = {0};
    assert(ble_driver_send(nested, nested, sizeof(nested)) == ESP_ERR_INVALID_STATE);
  }
  if (write_behavior == 3) {
    shutting_down = true;
    atomic_fetch_add(&session_generation, 1);
  }
  if (write_behavior == 4) {
    ble_conn_handle = 8;
    nus_tx_handle = 10;
    return BLE_HS_ENOTCONN;
  }
  return 0;
}
struct ble_gatt_error {
  int status;
};
struct ble_gatt_attr {
  int unused;
};
#include "ble_functions.inc"

typedef int CommsType;
typedef struct {
  CommsType type;
} CommsDriver;
static CommsDriver espnow = {0}, ble = {1};
static const CommsDriver *drivers[] = {&espnow, &ble};
static const CommsDriver *active_driver;
#define NUM_DRIVERS 2
static unsigned driver_inits;
static bool comms_is_initialized(void) {
  return is_initialized;
}
static esp_err_t comms_deinit(void) {
  return ble_driver_deinit();
}
static esp_err_t comms_init(void) {
  ++driver_inits;
  return ESP_OK;
}
#include "comms_functions.inc"

static void reset(void) {
  is_initialized = true;
  is_synced = true;
  has_target_peer = true;
  shutting_down = false;
  session_generation = 42;
  ble_conn_handle = 7;
  nus_tx_handle = 9;
  nus_rx_handle = 11;
  host_stopped = cccd_subscribed = discovery_complete = false;
  tx_lock.held = host_lock.held = deny_take = false;
  take_failures = 0;
  reconnect_timer = (void *)1;
  rssi_poll_task_handle = NULL;
  stop_calls = deinit_calls = stop_failures = deinit_failures = timer_deletes = 0;
  writes = callbacks = successful_callbacks = dummy_writes = terminate_calls = write_behavior = 0;
  event_deinits = driver_inits = 0;
  active_driver = &ble;
}
int main(void) {
  host_test_init();
  uint8_t payload[60] = {0};
  reset();
  assert(ble_driver_send(payload, payload, sizeof(payload)) == ESP_OK);
  assert(writes == 4 && callbacks == 1 && successful_callbacks == 1 && !tx_lock.held);
  reset();
  write_behavior = 1;
  assert(ble_driver_send(payload, payload, sizeof(payload)) == ESP_ERR_INVALID_STATE);
  assert(writes == 1 && callbacks == 1 && !successful_callbacks);
  reset();
  write_behavior = 1;
  assert(ble_driver_send(payload, payload, 1) == ESP_ERR_INVALID_STATE);
  assert(writes == 1 && callbacks == 1 && !successful_callbacks);
  reset();
  write_behavior = 2;
  assert(ble_driver_send(payload, payload, sizeof(payload)) == ESP_OK);
  assert(writes == 4 && callbacks == 2 && successful_callbacks == 1);
  reset();
  write_behavior = 3;
  assert(ble_driver_send(payload, payload, sizeof(payload)) == ESP_ERR_INVALID_STATE);
  assert(writes == 1 && callbacks == 1);
  reset();
  write_behavior = 4;
  assert(ble_driver_send(payload, payload, sizeof(payload)) == ESP_FAIL);
  assert(ble_conn_handle == 8 && nus_tx_handle == 10);
  reset();
  struct ble_gatt_error error = {0};
  discovery_complete = true;
  ble_on_write_cccd(7, &error, NULL, (void *)(uintptr_t)41);
  assert(!cccd_subscribed && !dummy_writes);
  ble_on_write_cccd(7, &error, NULL, (void *)(uintptr_t)42);
  assert(cccd_subscribed && dummy_writes == 1);
  reset();
  stop_failures = 3;
  int shutdown_result = ble_driver_deinit();
  if (shutdown_result != ESP_FAIL)
    fprintf(stderr, "shutdown result=%d stop=%u deinit=%u initialized=%d txheld=%d hostheld=%d\n", shutdown_result,
            stop_calls, deinit_calls, (int)is_initialized, tx_lock.held, host_lock.held);
  assert(shutdown_result == ESP_FAIL);
  assert(stop_calls == 3 && deinit_calls == 0 && !timer_deletes && !event_deinits && is_initialized && shutting_down);
  assert(ble_driver_send(payload, payload, 1) == ESP_ERR_INVALID_STATE);
  assert(ble_driver_deinit() == ESP_OK && deinit_calls == 1 && timer_deletes == 1 && !is_initialized);
  assert(event_deinits == 1);
  reset();
  stop_failures = 2;
  assert(ble_driver_deinit() == ESP_OK && stop_calls == 3);
  reset();
  deinit_failures = 1;
  assert(ble_driver_deinit() == ESP_FAIL && host_stopped);
  assert(ble_driver_deinit() == ESP_OK && stop_calls == 1 && deinit_calls == 2);
  reset();
  rssi_poll_task_handle = (void *)1;
  assert(ble_driver_deinit() == ESP_ERR_TIMEOUT);
  assert(!stop_calls && !deinit_calls && rssi_poll_should_exit);
  reset();
  tx_lock.held = true;
  assert(ble_driver_deinit() == ESP_ERR_TIMEOUT);
  assert(!stop_calls && !deinit_calls);
  reset();
  stop_failures = 3;
  assert(comms_select_driver(0) == ESP_FAIL && active_driver == &ble && !driver_inits);
  assert(comms_select_driver(0) == ESP_OK && active_driver == &espnow && driver_inits == 1);
  puts("BLE failure paths passed");
  return 0;
}
