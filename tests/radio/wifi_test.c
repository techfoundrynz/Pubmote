#include "common.h"
#define WIFI_AUTH_OPEN 0
#define WIFI_AUTH_WPA2_PSK 3
#define WIFI_IF_STA 0
#define WIFI_CONNECTED_BIT 1
#define WIFI_FAIL_BIT 2
#define pdFALSE 0
#define WIFI_STATE_DISCONNECTED 0
#define WIFI_STATE_CONNECTING 1
#define WIFI_STATE_RECONNECTING 2
#define WIFI_SCAN_MAX_RECORDS 40
typedef struct {
  uint8_t ssid[33];
  int8_t rssi;
  int authmode;
} wifi_ap_record_t;
typedef struct {
  char ssid[33];
  int rssi;
  bool password_protected;
} wifi_network_info_t;
static bool is_initialized = true;
static fake_mutex_t radio_lock;
static SemaphoreHandle_t radio_mutex = &radio_lock;
static atomic_bool connect_pending;
static int s_wifi_state;
static void *s_reconnect_timer;
static wifi_ap_record_t records[80];
static uint16_t record_count, requested_count;
static int scan_error, count_error, records_error, connect_error;
static unsigned scans, clears, scan_stops, disconnects, timer_stops, allocations, fail_allocation;
static bool cancel_scan;
static int esp_wifi_scan_start(void *config, bool blocking) {
  (void)config;
  assert(blocking && radio_lock.held);
  ++scans;
  if (cancel_scan)
    connect_pending = true;
  return scan_error;
}
static int esp_wifi_scan_get_ap_num(uint16_t *count) {
  *count = record_count;
  return count_error;
}
static int esp_wifi_scan_get_ap_records(uint16_t *count, wifi_ap_record_t *out) {
  requested_count = *count;
  assert(*count <= 40);
  memcpy(out, records, *count * sizeof(*out));
  return records_error;
}
static int esp_wifi_clear_ap_list(void) {
  ++clears;
  return 0;
}
static int esp_wifi_scan_stop(void) {
  ++scan_stops;
  radio_lock.held = false;
  return 0;
}
static int esp_wifi_disconnect(void) {
  ++disconnects;
  return 0;
}
static int xTimerStop(void *timer, unsigned wait) {
  (void)timer;
  (void)wait;
  ++timer_stops;
  return 0;
}
typedef bool (*wifi_cancel_fn)(void *context);
typedef unsigned EventBits_t;
typedef struct {
  struct {
    uint8_t ssid[32], password[64];
    struct {
      int authmode;
    } threshold;
    struct {
      bool capable, required;
    } pmf_cfg;
  } sta;
} wifi_config_t;
static void *s_wifi_event_group = (void *)1;
static char s_stored_ssid[33], s_stored_password[65];
static int s_retry_num;
static wifi_config_t configured;
static unsigned result_bits = WIFI_CONNECTED_BIT, waits;
static bool cancelled_join, cancel_after_wait;
static int esp_wifi_set_config(int interface, const wifi_config_t *config) {
  (void)interface;
  configured = *config;
  return ESP_OK;
}
static int esp_wifi_connect(void) {
  return connect_error;
}
static unsigned xEventGroupClearBits(void *group, unsigned bits) {
  (void)group;
  (void)bits;
  return 0;
}
static unsigned xEventGroupWaitBits(void *group, unsigned bits, int clear, int all, unsigned timeout) {
  (void)group;
  (void)bits;
  (void)clear;
  (void)all;
  ++waits;
  ticks += timeout;
  if (cancel_after_wait)
    cancelled_join = true;
  return result_bits;
}
static bool cancelled(void *context) {
  return *(bool *)context;
}
static void *test_calloc(size_t count, size_t size) {
  ++allocations;
  if (allocations == fail_allocation)
    return NULL;
  return calloc(count, size);
}
#define calloc test_calloc
#include "wifi_functions.inc"
#undef calloc
#define wifi_connect_to_network(ssid, password) wifi_connect_to_network_cancellable(ssid, password, NULL, NULL)

static void reset(void) {
  is_initialized = true;
  radio_lock.held = false;
  connect_pending = false;
  deny_take = false;
  take_failures = 0;
  s_wifi_state = WIFI_STATE_DISCONNECTED;
  s_reconnect_timer = (void *)1;
  record_count = requested_count = scan_error = count_error = records_error = connect_error = 0;
  scans = clears = scan_stops = disconnects = timer_stops = allocations = fail_allocation = 0;
  cancel_scan = false;
  cancelled_join = cancel_after_wait = false;
  result_bits = WIFI_CONNECTED_BIT;
  waits = ticks = 0;
  memset(&configured, 0, sizeof(configured));
  memset(records, 0, sizeof(records));
}
int main(void) {
  host_test_init();
  wifi_network_info_t *out;
  uint16_t count;
  reset();
  record_count = 4;
  strcpy((char *)records[0].ssid, "mesh");
  records[0].rssi = -30;
  strcpy((char *)records[1].ssid, "mesh");
  records[1].rssi = -40;
  records[1].authmode = 1;
  records[2].rssi = -50;
  strcpy((char *)records[3].ssid, "other");
  records[3].rssi = -60;
  assert(wifi_scan_networks(&out, &count) == ESP_OK && count == 2 && clears == 1);
  assert(!strcmp(out[0].ssid, "mesh") && out[0].rssi == -30 && out[0].password_protected);
  assert(!strcmp(out[1].ssid, "other") && !radio_lock.held);
  free(out);
  reset();
  record_count = 80;
  for (unsigned i = 0; i < record_count; ++i) {
    snprintf((char *)records[i].ssid, 33, "ap%u", i);
    records[i].rssi = -30 - i;
  }
  assert(wifi_scan_networks(&out, &count) == ESP_OK && count == 40 && requested_count == 40);
  free(out);
  for (unsigned failure = 1; failure <= 2; ++failure) {
    reset();
    record_count = 1;
    strcpy((char *)records[0].ssid, "ap");
    fail_allocation = failure;
    assert(wifi_scan_networks(&out, &count) == ESP_ERR_NO_MEM);
    assert(!out && !count && clears == 1 && !radio_lock.held);
  }
  reset();
  assert(wifi_scan_networks(&out, &count) == ESP_OK && !out && !count && clears == 1);
  reset();
  record_count = 1;
  records_error = ESP_FAIL;
  assert(wifi_scan_networks(&out, &count) == ESP_FAIL && !out && !count && clears == 1 && !radio_lock.held);
  reset();
  count_error = ESP_FAIL;
  assert(wifi_scan_networks(&out, &count) == ESP_FAIL && clears == 1 && !radio_lock.held);
  reset();
  scan_error = ESP_FAIL;
  assert(wifi_scan_networks(&out, &count) == ESP_FAIL && clears == 1 && !radio_lock.held);
  reset();
  cancel_scan = true;
  assert(wifi_scan_networks(&out, &count) == ESP_ERR_INVALID_STATE && !out && !count && clears == 1);
  reset();
  connect_pending = true;
  assert(wifi_scan_networks(&out, &count) == ESP_ERR_INVALID_STATE && !scans);
  assert(wifi_connect_to_network("ap", "password") == ESP_ERR_INVALID_STATE);
  reset();
  radio_lock.held = true;
  assert(wifi_connect_to_network("ap", "password") == ESP_OK && scan_stops == 1 && !connect_pending &&
         !radio_lock.held);
  reset();
  connect_error = ESP_ERR_TIMEOUT;
  assert(wifi_connect_to_network("ap", "password") == ESP_ERR_TIMEOUT);
  assert(disconnects == 1 && timer_stops == 1 && !connect_pending && !radio_lock.held);
  reset();
  deny_take = true;
  assert(wifi_connect_to_network("ap", "password") == ESP_ERR_TIMEOUT && !connect_pending);
  reset();
  is_initialized = false;
  assert(wifi_scan_networks(&out, &count) == ESP_ERR_WIFI_NOT_INIT && !out && !count && !scans);
  reset();
  char ssid[33], password[65];
  memset(ssid, 's', 32);
  ssid[32] = 0;
  memset(password, 'a', 64);
  password[64] = 0;
  assert(wifi_connect_to_network(ssid, password) == ESP_OK);
  assert(memcmp(configured.sta.ssid, ssid, 32) == 0);
  assert(memcmp(configured.sta.password, password, 64) == 0);
  assert(wifi_connect_to_network("", password) == ESP_ERR_INVALID_ARG);
  assert(wifi_connect_to_network("ap", "") == ESP_OK && configured.sta.threshold.authmode == WIFI_AUTH_OPEN);
  reset();
  cancelled_join = true;
  assert(wifi_connect_to_network_cancellable("ap", "password", cancelled, &cancelled_join) == ESP_ERR_TIMEOUT);
  assert(!connect_pending && !radio_lock.held && disconnects == 1);
  reset();
  cancelled_join = false;
  cancel_after_wait = true;
  result_bits = 0;
  waits = 0;
  assert(wifi_connect_to_network_cancellable("ap", "password", cancelled, &cancelled_join) == ESP_ERR_TIMEOUT);
  assert(waits == 1 && !connect_pending && !radio_lock.held && disconnects == 1);
  reset();
  cancelled_join = false;
  result_bits = WIFI_CONNECTED_BIT;
  cancel_after_wait = true;
  assert(wifi_connect_to_network_cancellable("ap", "password", cancelled, &cancelled_join) == ESP_ERR_TIMEOUT);
  assert(disconnects == 1); // cancellation wins over an IP event in the same wait
  puts("WiFi failure paths passed");
  return 0;
}
