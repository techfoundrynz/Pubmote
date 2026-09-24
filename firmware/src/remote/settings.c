#include "settings.h"
#include "config.h"
#include "connection.h"
#include "display.h"
#include "esp_log.h"
#include "esp_system.h"
#include "espnow.h"
#include "input_settings.h"
#include "nvs_flash.h"
#include "powermanagement.h"
#include "remote/adc.h"
#include "remote/remoteinputs.h"
#include "settings_api.h"
#include "stats.h"
#include "string.h"
#include <colors.h>
#include <stdio.h>

_Static_assert(PAIRED_MAC_BYTES == ESP_NOW_ETH_ALEN, "Paired device layout is persisted as a blob");

static const char *TAG = "PUBREMOTE-SETTINGS";

// Define the NVS namespace
#define STORAGE_NAMESPACE "nvs"
#define BL_LEVEL_KEY "bl_level"
#define BL_LEVEL_DEFAULT 200
#define SCREEN_ROTATION_KEY "screen_rotation"
#define AUTO_OFF_TIME_KEY "auto_off_time"

static const AutoOffOptions DEFAULT_AUTO_OFF_TIME = AUTO_OFF_5_MINUTES;
static const uint8_t DEFAULT_PEER_ADDR[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static const BoardBatteryDisplayOption DEFAULT_BATTERY_DISPLAY = BATTERY_DISPLAY_PERCENT;
static const PocketModeOptions DEFAULT_POCKET_MODE = POCKET_MODE_DISABLED;
static const StatsDoublePressAction DEFAULT_DOUBLE_PRESS_ACTION = DOUBLE_PRESS_ACTION_NONE;
// Solid theme colour - the behaviour before led_mode was configurable
static const LedModeOptions DEFAULT_LED_MODE = LED_MODE_SOLID;

DeviceSettings device_settings = {
    .bl_level = BL_LEVEL_DEFAULT,
    .screen_rotation = SCREEN_ROTATION_0,
    .auto_off_time = DEFAULT_AUTO_OFF_TIME,
    .temp_units = TEMP_UNITS_CELSIUS,
    .distance_units = DISTANCE_UNITS_METRIC,
    .startup_sound = STARTUP_SOUND_BEEP,
    .theme_color = COLOR_PRIMARY,
    .battery_display = DEFAULT_BATTERY_DISPLAY,
    .secondary_stat_display = SECONDARY_STAT_DUTY,
    .pocket_mode = DEFAULT_POCKET_MODE,
    .double_press_action = DEFAULT_DOUBLE_PRESS_ACTION,
    .hbm_mode = HBM_MODE_OFF,
    .led_mode = DEFAULT_LED_MODE,
};

CalibrationSettings calibration_settings = {
    .x_min = STICK_MIN_VAL,
    .x_max = STICK_MAX_VAL,
    .y_min = STICK_MIN_VAL,
    .y_max = STICK_MAX_VAL,
    .x_center = STICK_MID_VAL,
    .y_center = STICK_MID_VAL,
    .deadband = STICK_DEADBAND,
    .expo = STICK_EXPO,
    .invert_x = INVERT_X_AXIS,
    .invert_y = INVERT_Y_AXIS,
};

InputPinSettings input_pin_settings = {
    .js_x_gpio = INPUT_PIN_DISABLED,
    .js_y_gpio = INPUT_PIN_DISABLED,
    .btn1_gpio = INPUT_PIN_DISABLED,
    .btn1_active_level = JOYSTICK_BUTTON_LEVEL,
};

ImuCalibrationSettings imu_calibration = {
    .accel_x_offset = 0.0f,
    .accel_y_offset = 0.0f,
    .accel_z_offset = 0.0f,
    .invert_x = IMU_INVERT_X,
    .invert_y = IMU_INVERT_Y,
    .invert_z = IMU_INVERT_Z,
    .swap_xy = IMU_SWAP_XY,
};

PairingSettings pairing_settings = {
    .secret_code = DEFAULT_PAIRING_SECRET_CODE,
    .remote_addr = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, // Use 0xFF for -1 as uint8_t is unsigned
    .channel = 1,
    .default_index = -1,
};

static int find_paired_index(const uint8_t mac[ESP_NOW_ETH_ALEN]) {
  for (int i = 0; i < pairing_settings.device_count; i++) {
    if (is_same_mac(pairing_settings.devices[i].mac, (uint8_t *)mac)) {
      return i;
    }
  }
  return -1;
}

bool is_paired_mac(const uint8_t *mac) {
  return find_paired_index(mac) >= 0;
}

void set_default_device_index(int8_t idx) {
  if (idx >= 0 && idx < pairing_settings.device_count) {
    pairing_settings.default_index = idx;
    memcpy(pairing_settings.remote_addr, pairing_settings.devices[idx].mac, ESP_NOW_ETH_ALEN);
    pairing_settings.channel = pairing_settings.devices[idx].channel;
    pairing_settings.secret_code = pairing_settings.devices[idx].secret_code;
    remoteStats.vehicleType = pairing_settings.devices[idx].vehicle_type;
    stats_update();
  }
}

static void ensure_current_in_device_list_and_set_default() {
  if (is_same_mac(pairing_settings.remote_addr, (uint8_t *)DEFAULT_PEER_ADDR)) {
    return;
  }

  // Ensure current remote_addr/channel are in the devices list and set as default
  if (pairing_settings.device_count > MAX_PAIRED_DEVICES) {
    pairing_settings.device_count = MAX_PAIRED_DEVICES;
  }
  int idx = find_paired_index(pairing_settings.remote_addr);
  if (idx < 0) {
    // Add new if space, else replace the oldest (index 0)
    if (pairing_settings.device_count < MAX_PAIRED_DEVICES) {
      idx = pairing_settings.device_count++;
    }
    else {
      idx = 0;
    }
    memcpy(pairing_settings.devices[idx].mac, pairing_settings.remote_addr, ESP_NOW_ETH_ALEN);
    pairing_settings.devices[idx].vehicle_type = 0;
  }
  pairing_settings.devices[idx].secret_code = pairing_settings.secret_code;
  pairing_settings.devices[idx].channel = pairing_settings.channel;
  set_default_device_index(idx);
}

bool delete_paired_device_index(uint8_t idx) {
  if (idx >= pairing_settings.device_count) {
    return false;
  }

  // Delete from ESP-NOW peer list if it exists
  esp_now_del_peer(pairing_settings.devices[idx].mac);

  // Shift elements left to remove idx
  if (idx < pairing_settings.device_count - 1) {
    memmove(&pairing_settings.devices[idx], &pairing_settings.devices[idx + 1],
            (pairing_settings.device_count - idx - 1) * sizeof(PairedDevice));
  }
  pairing_settings.device_count--;

  // Adjust default index
  if (pairing_settings.default_index == (int8_t)idx) {
    if (pairing_settings.device_count > 0) {
      set_default_device_index(0);
    }
    else {
      pairing_settings.default_index = -1;
      memcpy(pairing_settings.remote_addr, DEFAULT_PEER_ADDR, sizeof(DEFAULT_PEER_ADDR));
      pairing_settings.channel = 1;
      pairing_settings.secret_code = DEFAULT_PAIRING_SECRET_CODE;
      pairing_state = PAIRING_STATE_UNPAIRED;
      connection_update_state(CONNECTION_STATE_DISCONNECTED);
    }
  }
  else if (pairing_settings.default_index > (int8_t)idx) {
    pairing_settings.default_index -= 1;
  }

  // Persist changes
  save_pairing_data();
  return true;
}

uint8_t get_paired_device_count() {
  return pairing_settings.device_count;
}

int8_t get_default_device_index() {
  return pairing_settings.default_index;
}

bool get_paired_device(uint8_t idx, PairedDevice *out_device) {
  if (out_device == NULL) {
    return false;
  }
  if (idx >= pairing_settings.device_count) {
    return false;
  }
  *out_device = pairing_settings.devices[idx];
  return true;
}

bool set_active_paired_device(uint8_t idx) {
  if (idx >= pairing_settings.device_count) {
    return false;
  }
  set_default_device_index((int8_t)idx);
  save_pairing_data();
  return true;
}

static esp_err_t nvs_write(const char *key, void *value, nvs_type_t type, size_t length) {
  nvs_handle_t nvs_handle;
  esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &nvs_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
    return err;
  }

  switch (type) {
  case NVS_TYPE_I8:
    err = nvs_set_i8(nvs_handle, key, *(int8_t *)value);
    break;
  case NVS_TYPE_U8:
    err = nvs_set_u8(nvs_handle, key, *(uint8_t *)value);
    break;
  case NVS_TYPE_I16:
    err = nvs_set_i16(nvs_handle, key, *(int16_t *)value);
    break;
  case NVS_TYPE_U16:
    err = nvs_set_u16(nvs_handle, key, *(uint16_t *)value);
    break;
  case NVS_TYPE_I32:
    err = nvs_set_i32(nvs_handle, key, *(int32_t *)value);
    break;
  case NVS_TYPE_U32:
    err = nvs_set_u32(nvs_handle, key, *(uint32_t *)value);
    break;
  case NVS_TYPE_I64:
    err = nvs_set_i64(nvs_handle, key, *(int64_t *)value);
    break;
  case NVS_TYPE_U64:
    err = nvs_set_u64(nvs_handle, key, *(uint64_t *)value);
    break;
  case NVS_TYPE_STR:
    err = nvs_set_str(nvs_handle, key, (char *)value);
    break;
  case NVS_TYPE_BLOB:
    err = nvs_set_blob(nvs_handle, key, value, length);
    break;
  default:
    ESP_LOGE(TAG, "Unsupported data type!");
    err = ESP_ERR_INVALID_ARG;
  }

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to write!");
    nvs_close(nvs_handle);
    return err;
  }
  else {
    ESP_LOGI(TAG, "Write done");
  }

  err = nvs_commit(nvs_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to commit!");
    nvs_close(nvs_handle);
    return err;
  }
  else {
    ESP_LOGI(TAG, "Commit done");
  }

  nvs_close(nvs_handle);
  return err;
}

// Function to read from NVS
static esp_err_t nvs_read(const char *key, void *value, nvs_type_t type, size_t length) {
  nvs_handle_t nvs_handle;
  esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READONLY, &nvs_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
    return err;
  }

  switch (type) {
  case NVS_TYPE_I8:
    err = nvs_get_i8(nvs_handle, key, (int8_t *)value);
    if (err == ESP_OK) {
      ESP_LOGI(TAG, "Read done, value = %d", *(int8_t *)value);
    }
    break;
  case NVS_TYPE_U8:
    err = nvs_get_u8(nvs_handle, key, (uint8_t *)value);
    if (err == ESP_OK) {
      ESP_LOGI(TAG, "Read done, value = %u", *(uint8_t *)value);
    }
    break;
  case NVS_TYPE_I16:
    err = nvs_get_i16(nvs_handle, key, (int16_t *)value);
    if (err == ESP_OK) {
      ESP_LOGI(TAG, "Read done, value = %d", *(int16_t *)value);
    }
    break;
  case NVS_TYPE_U16:
    err = nvs_get_u16(nvs_handle, key, (uint16_t *)value);
    if (err == ESP_OK) {
      ESP_LOGI(TAG, "Read done, value = %u", *(uint16_t *)value);
    }
    break;
  case NVS_TYPE_I32:
    err = nvs_get_i32(nvs_handle, key, (int32_t *)value);
    if (err == ESP_OK) {
      ESP_LOGI(TAG, "Read done, value = %ld", *(int32_t *)value);
    }
    break;
  case NVS_TYPE_U32:
    err = nvs_get_u32(nvs_handle, key, (uint32_t *)value);
    if (err == ESP_OK) {
      ESP_LOGI(TAG, "Read done, value = %lu", *(uint32_t *)value);
    }
    break;
  case NVS_TYPE_I64:
    err = nvs_get_i64(nvs_handle, key, (int64_t *)value);
    if (err == ESP_OK) {
      ESP_LOGI(TAG, "Read done, value = %lld", *(int64_t *)value);
    }
    break;
  case NVS_TYPE_U64:
    err = nvs_get_u64(nvs_handle, key, (uint64_t *)value);
    if (err == ESP_OK) {
      ESP_LOGI(TAG, "Read done, value = %llu", *(uint64_t *)value);
    }
    break;
  case NVS_TYPE_STR: {
    size_t required_size;
    // Get the size of the string first
    err = nvs_get_str(nvs_handle, key, NULL, &required_size);
    if (err == ESP_OK) {
      // Guard the caller's buffer: the stored string length is independent of
      // whatever the caller sized its buffer from (e.g. a separate length key),
      // so a mismatch/corruption must not overflow it. length==0 means unknown
      // (legacy callers) - fall back to trusting NVS.
      if (length != 0 && required_size > length) {
        ESP_LOGE(TAG, "NVS string '%s' (%u bytes) exceeds caller buffer (%u) - refusing", key, (unsigned)required_size,
                 (unsigned)length);
        err = ESP_ERR_INVALID_SIZE;
        break;
      }
      // Read directly into the caller buffer - no intermediate alloc/copy
      err = nvs_get_str(nvs_handle, key, (char *)value, &required_size);
      if (err == ESP_OK) {
        ESP_LOGI(TAG, "Read done, value = %s", (char *)value);
      }
    }
    break;
  }
  case NVS_TYPE_BLOB:
    err = nvs_get_blob(nvs_handle, key, value, &length);
    break;
  default:
    ESP_LOGE(TAG, "Unsupported data type!");
    err = ESP_ERR_INVALID_ARG;
  }

  switch (err) {
  case ESP_OK:
    ESP_LOGI(TAG, "Read done");
    break;
  case ESP_ERR_NVS_NOT_FOUND: // Never saved; callers fall back to defaults
    break;
  default:
    ESP_LOGE(TAG, "Error (%s) reading!", esp_err_to_name(err));
  }

  nvs_close(nvs_handle);
  return err;
}

// Function to write an integer to NVS
esp_err_t nvs_write_int(const char *key, uint32_t value) {
  return nvs_write(key, &value, NVS_TYPE_U32, 0);
}

esp_err_t nvs_write_str(const char *key, const char *value) {
  return nvs_write(key, (void *)value, NVS_TYPE_STR, strlen(value) + 1);
}

esp_err_t nvs_read_str(const char *key, char *out_value, size_t *length) {
  return nvs_read(key, out_value, NVS_TYPE_STR, *length);
}

// Function to write a blob to NVS
esp_err_t nvs_write_blob(const char *key, void *value, size_t length) {
  return nvs_write(key, value, NVS_TYPE_BLOB, length);
}

// Function to read an integer from NVS
esp_err_t nvs_read_int(const char *key, uint32_t *value) {
  return nvs_read(key, value, NVS_TYPE_U32, 0);
}

// Function to read a blob from NVS
esp_err_t nvs_read_blob(const char *key, void *value, size_t length) {
  return nvs_read(key, value, NVS_TYPE_BLOB, length);
}

esp_err_t reset_all_settings() {
  return nvs_flash_erase();
}

static uint8_t get_auto_off_time_minutes() {
  switch (device_settings.auto_off_time) {
  case AUTO_OFF_DISABLED:
    return 0;
  case AUTO_OFF_2_MINUTES:
    return 2;
  case AUTO_OFF_5_MINUTES:
    return 5;
  case AUTO_OFF_10_MINUTES:
    return 10;
  case AUTO_OFF_20_MINUTES:
    return 20;
  case AUTO_OFF_30_MINUTES:
    return 30;
  default:
    return 0;
  }
}

uint64_t get_auto_off_ms() {
  return get_auto_off_time_minutes() * 60 * 1000;
}

bool is_pocket_mode_enabled() {
  return device_settings.pocket_mode == POCKET_MODE_ENABLED;
}

void save_device_settings() {
  reset_sleep_timer();
  esp_err_t result = settings_save_device_preferences();
  if (result != ESP_OK) {
    ESP_LOGE(TAG, "Failed to save device settings: %s", esp_err_to_name(result));
  }
}

esp_err_t save_wifi_ssid(const char *ssid) {
  return settings_save_string("wifi_ssid", ssid);
}

bool set_current_default_device_secret(uint32_t secret_code) {
  if (pairing_settings.default_index >= 0 && pairing_settings.default_index < pairing_settings.device_count) {
    pairing_settings.devices[pairing_settings.default_index].secret_code = secret_code;
    pairing_settings.secret_code = secret_code; // keep legacy field in sync
    return true;
  }
  int idx = find_paired_index(pairing_settings.remote_addr);
  if (idx >= 0) {
    pairing_settings.devices[idx].secret_code = secret_code;
    pairing_settings.secret_code = secret_code;
    return true;
  }
  return false;
}

esp_err_t save_wifi_password(const char *password) {
  return settings_save_string("wifi_password", password);
}

char *get_wifi_ssid() {
  int ssid_length = 0;
  esp_err_t err = nvs_read_int("wifi_ssid_l", (uint32_t *)&ssid_length);
  if (err == ESP_ERR_NVS_NOT_FOUND || (err == ESP_OK && ssid_length == 0)) {
    return NULL;
  }
  if (err != ESP_OK || ssid_length < 0 || ssid_length > WIFI_SSID_MAX_BYTES) {
    ESP_LOGE(TAG, "Error reading SSID length: %s", esp_err_to_name(err));
    return NULL;
  }

  char ssid[ssid_length + 1];
  size_t required_size = sizeof(ssid);
  err = nvs_read_str("wifi_ssid", ssid, &required_size);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error reading SSID: %s", esp_err_to_name(err));
    return NULL;
  }

  static char final_ssid[WIFI_SSID_MAX_BYTES + 1]; // Static to ensure it remains valid after function returns
  if (required_size > sizeof(final_ssid)) {
    ESP_LOGE(TAG, "SSID size exceeds buffer size!");
    return NULL;
  }
  strncpy(final_ssid, ssid, sizeof(final_ssid) - 1);
  final_ssid[sizeof(final_ssid) - 1] = '\0'; // Ensure null termination
  ESP_LOGI(TAG, "Retrieved Wi-Fi SSID successfully.");
  return final_ssid;
}

char *get_wifi_password() {
  int password_length = 0;
  esp_err_t err = nvs_read_int("wifi_key_l", (uint32_t *)&password_length);
  if (err == ESP_ERR_NVS_NOT_FOUND || (err == ESP_OK && password_length == 0)) {
    return NULL;
  }
  if (err != ESP_OK || password_length < 0 || password_length > WIFI_PASSWORD_MAX_BYTES) {
    ESP_LOGE(TAG, "Error reading password length: %s", esp_err_to_name(err));
    return NULL;
  }

  char password[password_length + 1];
  size_t required_size = sizeof(password);
  err = nvs_read_str("wifi_password", password, &required_size);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error reading password: %s", esp_err_to_name(err));
    return NULL;
  }

  static char final_password[WIFI_PASSWORD_MAX_BYTES + 1]; // Static to ensure it remains valid after function returns
  if (required_size > sizeof(final_password)) {
    ESP_LOGE(TAG, "Password size exceeds buffer size!");
    return NULL;
  }
  strncpy(final_password, password, sizeof(final_password) - 1);
  final_password[sizeof(final_password) - 1] = '\0'; // Ensure null termination
  ESP_LOGI(TAG, "Retrieved Wi-Fi password successfully.");
  return final_password;
}

esp_err_t save_pairing_data() {
  ESP_LOGI(TAG, "Saving pairing data...");
  // Ensure device list reflects current default
  ensure_current_in_device_list_and_set_default();

  // Save multi-device info
  esp_err_t err = nvs_write_int("paired_count", pairing_settings.device_count);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error saving paired device count!");
    return err;
  }

  err = nvs_write_int("default_index", (int32_t)pairing_settings.default_index);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error saving default device index!");
    return err;
  }

  // Version the blob format to support future migrations
  err = nvs_write_int("paired_blob_ver", 3);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error saving paired devices blob version!");
    return err;
  }

  // Store full fixed-size array for simplicity
  err = nvs_write_blob("paired_devices", pairing_settings.devices, sizeof(pairing_settings.devices));
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error saving paired devices blob!");
    return err;
  }

  ESP_LOGI(TAG, "Pairing data saved successfully.");
  return ESP_OK;
}

void save_input_calibration() {
  esp_err_t result = settings_store_input_state(&input_pin_settings, &calibration_settings);
  if (result != ESP_OK) {
    ESP_LOGE(TAG, "Failed to save input calibration: %s", esp_err_to_name(result));
  }
}

void input_pins_load_defaults(InputPinSettings *out) {
  if (out == NULL) {
    return;
  }

  out->js_x_gpio = INPUT_PIN_DISABLED;
  out->js_y_gpio = INPUT_PIN_DISABLED;
  out->btn1_gpio = INPUT_PIN_DISABLED;
  out->btn1_active_level = JOYSTICK_BUTTON_LEVEL;

#if JOYSTICK_X_ENABLED
  out->js_x_gpio = (int8_t)JOYSTICK_X;
#endif
#if JOYSTICK_Y_ENABLED
  out->js_y_gpio = (int8_t)JOYSTICK_Y;
#endif
#if JOYSTICK_BUTTON_ENABLED
  out->btn1_gpio = (int8_t)PRIMARY_BUTTON;
#endif
}

void reset_axis_calibration(bool reset_x, bool reset_y) {
  settings_reset_calibration(&calibration_settings, reset_x, reset_y);
}

void save_imu_calibration() {
  nvs_write_int("imu_off_x", (int32_t)(imu_calibration.accel_x_offset * 1000.0f));
  nvs_write_int("imu_off_y", (int32_t)(imu_calibration.accel_y_offset * 1000.0f));
  nvs_write_int("imu_off_z", (int32_t)(imu_calibration.accel_z_offset * 1000.0f));
  nvs_write_int("imu_inv_x", imu_calibration.invert_x ? 1 : 0);
  nvs_write_int("imu_inv_y", imu_calibration.invert_y ? 1 : 0);
  nvs_write_int("imu_inv_z", imu_calibration.invert_z ? 1 : 0);
  nvs_write_int("imu_swap_xy", imu_calibration.swap_xy ? 1 : 0);
}

void settings_apply_imu_calibration(const ImuCalibrationSettings *imu) {
  imu_calibration = *imu;
  save_imu_calibration();
}

esp_err_t settings_replace_pairing(const PairedDevice *devices, uint8_t count, int8_t default_index) {
  for (uint8_t i = 0; i < pairing_settings.device_count; ++i) {
    bool kept = false;
    for (uint8_t j = 0; j < count && !kept; ++j) {
      kept = is_same_mac(pairing_settings.devices[i].mac, (uint8_t *)devices[j].mac);
    }
    if (!kept) {
      esp_now_del_peer(pairing_settings.devices[i].mac);
    }
  }
  memset(pairing_settings.devices, 0, sizeof(pairing_settings.devices));
  memcpy(pairing_settings.devices, devices, count * sizeof(PairedDevice));
  pairing_settings.device_count = count;
  if (default_index >= 0) {
    set_default_device_index(default_index);
  }
  else {
    pairing_settings.default_index = -1;
    memcpy(pairing_settings.remote_addr, DEFAULT_PEER_ADDR, sizeof(DEFAULT_PEER_ADDR));
    pairing_settings.channel = 1;
    pairing_settings.secret_code = DEFAULT_PAIRING_SECRET_CODE;
  }
  esp_err_t err = save_pairing_data();
  connection_refresh_pairing_state();
  if (pairing_state == PAIRING_STATE_PAIRED) {
    connection_switch_comms_mode(settings_get_active_comms_mode());
    connection_connect_to_default_peer();
  }
  else {
    connection_update_state(CONNECTION_STATE_DISCONNECTED);
  }
  return err;
}

// Function to initialize NVS
static esp_err_t init_nvs() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // NVS partition was truncated and needs to be erased
    // Retry nvs_flash_init
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
  return ESP_OK;
}

// Function to initialize settings object, reading from NVS
esp_err_t settings_init() {
  ESP_LOGI(TAG, "Initializing settings...");
  esp_err_t err = init_nvs();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error initializing NVS!");
    return err;
  }

  // Temporary value to store read settings
  uint32_t temp_setting_value;
  // Migrate stored settings here when the schema changes.
  if (nvs_read_int("settings_schema", &temp_setting_value) != ESP_OK || temp_setting_value != SETTINGS_SCHEMA_VERSION) {
    nvs_write_int("settings_schema", SETTINGS_SCHEMA_VERSION);
  }
  device_settings.bl_level =
      nvs_read_int(BL_LEVEL_KEY, &temp_setting_value) == ESP_OK ? (uint8_t)temp_setting_value : BL_LEVEL_DEFAULT;

  device_settings.screen_rotation = nvs_read_int(SCREEN_ROTATION_KEY, &temp_setting_value) == ESP_OK
                                        ? (uint8_t)temp_setting_value
                                        : SCREEN_ROTATION_0;

  device_settings.auto_off_time = nvs_read_int("auto_off_time", &temp_setting_value) == ESP_OK
                                      ? (AutoOffOptions)temp_setting_value
                                      : DEFAULT_AUTO_OFF_TIME;

  device_settings.temp_units =
      nvs_read_int("temp_units", &temp_setting_value) == ESP_OK ? (TempUnits)temp_setting_value : TEMP_UNITS_CELSIUS;

  device_settings.distance_units = nvs_read_int("distance_units", &temp_setting_value) == ESP_OK
                                       ? (DistanceUnits)temp_setting_value
                                       : DISTANCE_UNITS_METRIC;

  device_settings.startup_sound = nvs_read_int("startup_sound", &temp_setting_value) == ESP_OK
                                      ? (StartupSoundOptions)temp_setting_value
                                      : STARTUP_SOUND_BEEP;

  device_settings.theme_color =
      nvs_read_int("theme_color", &device_settings.theme_color) == ESP_OK ? device_settings.theme_color : COLOR_PRIMARY;

  device_settings.battery_display = nvs_read_int("battery_display", &temp_setting_value) == ESP_OK
                                        ? (BoardBatteryDisplayOption)temp_setting_value
                                        : BATTERY_DISPLAY_PERCENT;

  device_settings.secondary_stat_display = nvs_read_int("sec_stat_disp", &temp_setting_value) == ESP_OK
                                               ? (SecondaryStatDisplayOption)temp_setting_value
                                               : SECONDARY_STAT_DUTY;

  device_settings.pocket_mode =
      nvs_read_int("pocket_mode", &temp_setting_value) == ESP_OK ? (bool)temp_setting_value : POCKET_MODE_DISABLED;

  device_settings.double_press_action = nvs_read_int("stats_dp", &temp_setting_value) == ESP_OK
                                            ? (StatsDoublePressAction)temp_setting_value
                                            : DEFAULT_DOUBLE_PRESS_ACTION;

  device_settings.hbm_mode =
      nvs_read_int("hbm_mode", &temp_setting_value) == ESP_OK ? (HbmModeOptions)temp_setting_value : HBM_MODE_OFF;
  if (device_settings.hbm_mode >= HBM_MODE_COUNT) {
    device_settings.hbm_mode = HBM_MODE_OFF;
  }
#if !IMU_ENABLED
  if (device_settings.hbm_mode == HBM_MODE_RAISED) {
    device_settings.hbm_mode = HBM_MODE_OFF;
  }
#endif

  device_settings.led_mode =
      nvs_read_int("led_mode", &temp_setting_value) == ESP_OK ? (LedModeOptions)temp_setting_value : DEFAULT_LED_MODE;
  if (device_settings.led_mode >= LED_MODE_COUNT) {
    device_settings.led_mode = DEFAULT_LED_MODE;
  }

  calibration_settings.expo = STICK_EXPO;
  calibration_settings.invert_x = INVERT_X_AXIS;
  calibration_settings.invert_y = INVERT_Y_AXIS;
  settings_reset_calibration(&calibration_settings, true, true);

  // Adopt a stored assignment only if it still validates, so a stale mapping
  // can't leave the remote without inputs
  InputPinSettings stored_pins;
  input_pins_load_defaults(&stored_pins);
  input_pin_settings = stored_pins;

  CalibrationSettings stored_calibration;
  if (settings_load_input_state(&stored_pins, &stored_calibration) == ESP_OK) {
    calibration_settings = stored_calibration;
  }

  char pin_err[96];
  if (input_pins_validate(&stored_pins, pin_err, sizeof(pin_err)) == ESP_OK) {
    input_pin_settings = stored_pins;
  }
  else {
    ESP_LOGE(TAG, "Stored input pins rejected (%s) - using board defaults", pin_err);
    settings_reset_calibration(&calibration_settings, true, true);
  }
  ESP_LOGI(TAG, "Input pins: js_x=%d js_y=%d btn=%d (level %u)", input_pin_settings.js_x_gpio,
           input_pin_settings.js_y_gpio, input_pin_settings.btn1_gpio, input_pin_settings.btn1_active_level);

  uint32_t temp_val;
  imu_calibration.accel_x_offset =
      nvs_read_int("imu_off_x", &temp_val) == ESP_OK ? ((float)(int32_t)temp_val / 1000.0f) : 0.0f;
  imu_calibration.accel_y_offset =
      nvs_read_int("imu_off_y", &temp_val) == ESP_OK ? ((float)(int32_t)temp_val / 1000.0f) : 0.0f;
  imu_calibration.accel_z_offset =
      nvs_read_int("imu_off_z", &temp_val) == ESP_OK ? ((float)(int32_t)temp_val / 1000.0f) : 0.0f;
  imu_calibration.invert_x =
      nvs_read_int("imu_inv_x", &temp_setting_value) == ESP_OK ? (bool)temp_setting_value : IMU_INVERT_X;
  imu_calibration.invert_y =
      nvs_read_int("imu_inv_y", &temp_setting_value) == ESP_OK ? (bool)temp_setting_value : IMU_INVERT_Y;
  imu_calibration.invert_z =
      nvs_read_int("imu_inv_z", &temp_setting_value) == ESP_OK ? (bool)temp_setting_value : IMU_INVERT_Z;
  imu_calibration.swap_xy =
      nvs_read_int("imu_swap_xy", &temp_setting_value) == ESP_OK ? (bool)temp_setting_value : IMU_SWAP_XY;

  // Reading pairing settings
  uint32_t count_read = 0;
  if (nvs_read_int("paired_count", &count_read) == ESP_OK && count_read <= MAX_PAIRED_DEVICES) {
    pairing_settings.device_count = (uint8_t)count_read;
  }
  else {
    pairing_settings.device_count = 0;
  }

  uint32_t di_val = 0;
  if (nvs_read_int("default_index", &di_val) == ESP_OK) {
    pairing_settings.default_index = (int32_t)di_val;
  }
  else {
    pairing_settings.default_index = pairing_settings.device_count > 0 ? 0 : -1;
  }

  uint32_t blob_ver = 0;
  if (nvs_read_int("paired_blob_ver", &blob_ver) != ESP_OK || blob_ver != 3) {
    pairing_settings.device_count = 0;
    pairing_settings.default_index = -1;
  }
  else {
    PairedDevice devices_tmp[MAX_PAIRED_DEVICES];
    esp_err_t derr = nvs_read_blob("paired_devices", devices_tmp, sizeof(devices_tmp));
    if (derr == ESP_OK) {
      memcpy(pairing_settings.devices, devices_tmp, sizeof(devices_tmp));
    }
    else {
      pairing_settings.device_count = 0;
      pairing_settings.default_index = -1;
    }
  }

  if (pairing_settings.default_index >= 0 && pairing_settings.default_index < pairing_settings.device_count) {
    memcpy(pairing_settings.remote_addr, pairing_settings.devices[pairing_settings.default_index].mac,
           ESP_NOW_ETH_ALEN);
    pairing_settings.channel = pairing_settings.devices[pairing_settings.default_index].channel;
    pairing_settings.secret_code = pairing_settings.devices[pairing_settings.default_index].secret_code;
    remoteStats.vehicleType = pairing_settings.devices[pairing_settings.default_index].vehicle_type;
  }
  else {
    memcpy(pairing_settings.remote_addr, DEFAULT_PEER_ADDR, sizeof(DEFAULT_PEER_ADDR));
    pairing_settings.channel = 1;
    pairing_settings.secret_code = DEFAULT_PAIRING_SECRET_CODE;
  }

  return ESP_OK;
}

CommsType settings_get_board_comms_mode(int8_t index) {
  if (index >= 0 && index < pairing_settings.device_count) {
    if (pairing_settings.devices[index].channel & 0x80) {
      return COMMS_TYPE_BLE;
    }
  }
  return COMMS_TYPE_ESPNOW;
}

CommsType settings_get_active_comms_mode(void) {
  return settings_get_board_comms_mode(pairing_settings.default_index);
}
