#include "common.h"
#define ESP_ERR_NVS_NO_FREE_PAGES -10
#define ESP_ERR_NVS_NEW_VERSION_FOUND -11
#define ESP_ERR_ESPNOW_NOT_INIT -12
#define ESP_ERR_WIFI_NOT_STARTED -13
#define WIFI_INIT_CONFIG_DEFAULT() {0}
#define WIFI_MODE_STA 1
#define WIFI_PS_NONE 0
#define WIFI_COUNTRY_POLICY_MANUAL 0
#define WIFI_SECOND_CHAN_NONE 0
typedef int wifi_init_config_t;
typedef int wifi_mode_t;
typedef struct {
  char cc[3];
  int schan, nchan, policy;
} wifi_country_t;
static struct {
  unsigned channel;
} pairing_settings = {1};
typedef uint8_t (*comms_channel_get_cb_t)(void);
typedef void (*comms_channel_set_cb_t)(uint8_t channel);
static comms_channel_get_cb_t channel_get;
static comms_channel_set_cb_t channel_set;
static uint8_t saved_channel_get(void) {
  return pairing_settings.channel;
}
static void saved_channel_set(uint8_t channel) {
  pairing_settings.channel = channel;
}
static bool is_initialized, shutting_down, now_ready, wifi_ready;
static int stop_error, deinit_error, now_error, starts, selected_channel, rejected_channel;
static int on_espnow_recv, on_espnow_sent;
static int nvs_flash_init(void) {
  return ESP_OK;
}
static int nvs_flash_erase(void) {
  return ESP_OK;
}
static int esp_event_loop_create_default(void) {
  return ESP_OK;
}
static int esp_event_loop_delete_default(void) {
  return ESP_OK;
}
static int esp_wifi_get_mode(wifi_mode_t *mode) {
  *mode = 1;
  return wifi_ready ? ESP_OK : ESP_ERR_WIFI_NOT_INIT;
}
static int esp_wifi_init(const wifi_init_config_t *cfg) {
  (void)cfg;
  wifi_ready = true;
  return ESP_OK;
}
static int esp_wifi_set_mode(int mode) {
  (void)mode;
  return ESP_OK;
}
static int esp_wifi_start(void) {
  ++starts;
  return ESP_OK;
}
static int esp_wifi_set_ps(int mode) {
  (void)mode;
  return ESP_OK;
}
static int esp_wifi_set_max_tx_power(int power) {
  (void)power;
  return ESP_OK;
}
static int esp_wifi_set_country(const wifi_country_t *country) {
  (void)country;
  return ESP_OK;
}
static int esp_wifi_set_channel(int channel, int secondary) {
  (void)secondary;
  selected_channel = channel;
  if (channel == rejected_channel)
    return ESP_FAIL;
  return ESP_OK;
}
static int esp_now_init(void) {
  now_ready = true;
  return ESP_OK;
}
static int esp_now_register_recv_cb(int cb) {
  (void)cb;
  return ESP_OK;
}
static int esp_now_register_send_cb(int cb) {
  (void)cb;
  return ESP_OK;
}
static void esp_now_unregister_recv_cb(void) {
}
static void esp_now_unregister_send_cb(void) {
}
static int esp_now_deinit(void) {
  if (now_error)
    return now_error;
  if (!now_ready)
    return ESP_ERR_ESPNOW_NOT_INIT;
  now_ready = false;
  return ESP_OK;
}
static int esp_wifi_stop(void) {
  return stop_error;
}
static int esp_wifi_deinit(void) {
  if (deinit_error)
    return deinit_error;
  wifi_ready = false;
  return ESP_OK;
}
#include "espnow_functions.inc"

static void reset(void) {
  is_initialized = now_ready = wifi_ready = true;
  shutting_down = false;
  stop_error = deinit_error = now_error = starts = 0;
  selected_channel = rejected_channel = 0;
  pairing_settings.channel = 6;
  comms_bind_channel_config(saved_channel_get, saved_channel_set);
}
int main(void) {
  host_test_init();
  for (int failure = 0; failure < 3; ++failure) {
    reset();
    if (failure == 0)
      now_error = ESP_FAIL;
    if (failure == 1)
      stop_error = ESP_FAIL;
    if (failure == 2)
      deinit_error = ESP_FAIL;
    assert(espnow_driver_deinit() == ESP_FAIL);
    assert(is_initialized && shutting_down);
    assert(espnow_driver_init() == ESP_ERR_INVALID_STATE && starts == 0);
    if (failure > 0)
      assert(!now_ready);
    stop_error = deinit_error = now_error = 0;
    assert(espnow_driver_deinit() == ESP_OK);
    assert(!is_initialized && !shutting_down);
    assert(espnow_driver_init() == ESP_OK && now_ready && starts == 1);
  }
  reset();
  stop_error = ESP_FAIL;
  assert(espnow_driver_deinit() == ESP_FAIL);
  stop_error = 0;
  assert(espnow_prepare_wifi() == ESP_OK && !is_initialized && !shutting_down && !wifi_ready);
  reset();
  assert(espnow_prepare_wifi() == ESP_OK && !is_initialized && !now_ready && wifi_ready);
  for (int channel = 0; channel <= 15; ++channel) {
    reset();
    is_initialized = now_ready = wifi_ready = false;
    pairing_settings.channel = channel;
    assert(espnow_driver_init() == ESP_OK);
    int expected = channel >= 1 && channel <= 14 ? channel : 1;
    assert(selected_channel == expected && pairing_settings.channel == expected);
  }
  reset();
  is_initialized = now_ready = wifi_ready = false;
  rejected_channel = 6;
  assert(espnow_driver_init() == ESP_OK);
  assert(selected_channel == 1 && pairing_settings.channel == 1);
  reset();
  is_initialized = now_ready = wifi_ready = false;
  comms_bind_channel_config(NULL, NULL);
  assert(espnow_driver_init() == ESP_OK && selected_channel == 1 && pairing_settings.channel == 6);
  puts("ESP-NOW partial teardown refuses false initialization and recovers on retry");
}
