#include "settings.h"
#include "display.h"
#include "esp_log.h"
#include "esp_system.h"
#include "espnow.h"
#include "nvs_flash.h"
#include "remote/adc.h"
#include "string.h"
#include <colors.h>
#include <stdio.h>

static const char *TAG = "PUBREMOTE-SETTINGS";

// Define the NVS namespace
#define STORAGE_NAMESPACE "nvs"
#define BL_LEVEL_KEY "bl_level"
#define BL_LEVEL_DEFAULT 200
#define SCREEN_ROTATION_KEY "screen_rotation"
#define AUTO_OFF_TIME_KEY "auto_off_time"
#define EXPO_ADJUST_FACTOR 100 // Stored as 2dp int

static const AutoOffOptions DEFAULT_AUTO_OFF_TIME = AUTO_OFF_5_MINUTES;
static const uint8_t DEFAULT_PEER_ADDR[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static const DarkTextOptions DEFAULT_DARK_TEXT = DARK_TEXT_DISABLED;
static const BoardBatteryDisplayOption DEFAULT_BATTERY_DISPLAY = BATTERY_DISPLAY_PERCENT;
static const PocketModeOptions DEFAULT_POCKET_MODE = POCKET_MODE_DISABLED;
static const StatsDoublePressAction DEFAULT_DOUBLE_PRESS_ACTION = DOUBLE_PRESS_ACTION_NONE;

DeviceSettings device_settings = {
    .bl_level = BL_LEVEL_DEFAULT,
    .screen_rotation = SCREEN_ROTATION_0,
    .auto_off_time = DEFAULT_AUTO_OFF_TIME,
    .temp_units = TEMP_UNITS_CELSIUS,
    .distance_units = DISTANCE_UNITS_METRIC,
    .startup_sound = STARTUP_SOUND_BEEP,
    .theme_color = COLOR_PRIMARY,
    .dark_text = DEFAULT_DARK_TEXT,
    .battery_display = DEFAULT_BATTERY_DISPLAY,
    .pocket_mode = DEFAULT_POCKET_MODE,
    .double_press_action = DEFAULT_DOUBLE_PRESS_ACTION,
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
    // .invert_x = INVERT_X_AXIS,
    .invert_y = INVERT_Y_AXIS,
    // .invert_xy = INVERT_XY_AXIS,
};

PairingSettings pairing_settings = {
    .remote_addr = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, // Use 0xFF for -1 as uint8_t is unsigned
    .secret_code = DEFAULT_PAIRING_SECRET_CODE,
    .channel = 1,
    .devices = {0},
    .device_count = 0,
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
  }
}

static void ensure_current_in_device_list_and_set_default() {
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
    pairing_settings.devices[idx].secret_code = pairing_settings.secret_code;
  }
  pairing_settings.devices[idx].channel = pairing_settings.channel;
  set_default_device_index(idx);
}

bool delete_paired_device_index(uint8_t idx) {
  if (idx >= pairing_settings.device_count) {
    return false;
  }

  // Shift elements left to remove idx
  if (idx < pairing_settings.device_count - 1) {
    memmove(&pairing_settings.devices[idx], &pairing_settings.devices[idx + 1],
            (pairing_settings.device_count - idx - 1) * sizeof(PairedDevice));
  }
  pairing_settings.device_count--;

  // Adjust default index
  if (pairing_settings.default_index == (int8_t)idx) {
    if (pairing_settings.device_count > 0) {
      pairing_settings.default_index = 0;
      memcpy(pairing_settings.remote_addr, pairing_settings.devices[0].mac, ESP_NOW_ETH_ALEN);
      pairing_settings.channel = pairing_settings.devices[0].channel;
    }
    else {
      pairing_settings.default_index = -1;
      memcpy(pairing_settings.remote_addr, DEFAULT_PEER_ADDR, sizeof(DEFAULT_PEER_ADDR));
      pairing_settings.channel = 1;
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
    return err;
  }
  else {
    ESP_LOGI(TAG, "Write done");
  }

  err = nvs_commit(nvs_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to commit!");
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
      char *str_value = (char *)malloc(required_size);
      if (str_value == NULL) {
        ESP_LOGE(TAG, "Memory allocation failed!");
        err = ESP_ERR_NO_MEM;
      }
      else {
        err = nvs_get_str(nvs_handle, key, str_value, &required_size);
        if (err == ESP_OK) {
          strcpy((char *)value, str_value);
          ESP_LOGI(TAG, "Read done, value = %s", str_value);
        }
        free(str_value);
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
  case ESP_ERR_NVS_NOT_FOUND:
    ESP_LOGE(TAG, "The value is not initialized yet!");
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
  nvs_write_int(BL_LEVEL_KEY, device_settings.bl_level);
  nvs_write_int(SCREEN_ROTATION_KEY, device_settings.screen_rotation);
  nvs_write_int(AUTO_OFF_TIME_KEY, device_settings.auto_off_time);
  nvs_write_int("temp_units", device_settings.temp_units);
  nvs_write_int("distance_units", device_settings.distance_units);
  nvs_write_int("startup_sound", device_settings.startup_sound);
  nvs_write_int("theme_color", device_settings.theme_color);
  nvs_write_int("dark_text", device_settings.dark_text);
  nvs_write_int("battery_display", device_settings.battery_display);
  nvs_write_int("pocket_mode", device_settings.pocket_mode);
  nvs_write_int("stats_dp", device_settings.double_press_action);
}

esp_err_t save_wifi_ssid(const char *ssid) {
  ESP_LOGI(TAG, "Saving Wi-Fi SSID: %s", ssid);
  int ssid_length = strlen(ssid);

  esp_err_t err = ESP_OK;

  if (ssid_length > 32) {
    ESP_LOGE(TAG, "SSID must be less than 33 characters");
    return ESP_ERR_INVALID_ARG;
  }

  err = nvs_write_str("wifi_ssid", ssid);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error saving SSID! SSID: %s", ssid);
    return err;
  }

  err = nvs_write_int("wifi_ssid_l", ssid_length);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error saving SSID length! Length: %d", ssid_length);
    return err;
  }

  ESP_LOGI(TAG, "Wi-Fi credentials saved successfully.");

  return ESP_OK;
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
  ESP_LOGI(TAG, "Saving Wi-Fi password: %s", password);
  int password_length = strlen(password);
  esp_err_t err = ESP_OK;

  if (password_length > 64) {
    ESP_LOGE(TAG, "SSID must be less than 65 characters");
    return ESP_ERR_INVALID_ARG;
  }

  err = nvs_write_str("wifi_password", password);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error saving password! Password: %s", password);
    return err;
  }

  err = nvs_write_int("wifi_key_l", password_length);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error saving password length! Length: %d", password_length);
    return err;
  }

  ESP_LOGI(TAG, "Wi-Fi credentials saved successfully.");

  return ESP_OK;
}

char *get_wifi_ssid() {
  int ssid_length = 0;
  esp_err_t err = nvs_read_int("wifi_ssid_l", (uint32_t *)&ssid_length);
  if (err != ESP_OK || ssid_length <= 0 || ssid_length > 32) {
    ESP_LOGE(TAG, "Error reading SSID length: %s", esp_err_to_name(err));
    return NULL;
  }

  char ssid[ssid_length];
  size_t required_size = sizeof(ssid);
  err = nvs_read_str("wifi_ssid", ssid, &required_size);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error reading SSID: %s", esp_err_to_name(err));
    return NULL;
  }

  static char final_ssid[32]; // Static to ensure it remains valid after function returns
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
  if (err != ESP_OK || password_length <= 0 || password_length > 64) {
    ESP_LOGE(TAG, "Error reading password length: %s", esp_err_to_name(err));
    return NULL;
  }

  char password[password_length];
  size_t required_size = sizeof(password);
  err = nvs_read_str("wifi_password", password, &required_size);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error reading password: %s", esp_err_to_name(err));
    return NULL;
  }

  static char final_password[64]; // Static to ensure it remains valid after function returns
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
  err = nvs_write_int("paired_blob_ver", 2);
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

void save_calibration() {
  nvs_write_int("x_min", calibration_settings.x_min);
  nvs_write_int("x_max", calibration_settings.x_max);
  nvs_write_int("y_min", calibration_settings.y_min);
  nvs_write_int("y_max", calibration_settings.y_max);
  nvs_write_int("x_center", calibration_settings.x_center);
  nvs_write_int("y_center", calibration_settings.y_center);
  nvs_write_int("deadband", calibration_settings.deadband);
  nvs_write_int("expo", (int)(calibration_settings.expo * EXPO_ADJUST_FACTOR));
  nvs_write_int("invert_y", calibration_settings.invert_y);
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

  device_settings.dark_text =
      nvs_read_int("dark_text", &temp_setting_value) == ESP_OK ? (bool)temp_setting_value : DARK_TEXT_DISABLED;

  device_settings.battery_display = nvs_read_int("battery_display", &temp_setting_value) == ESP_OK
                                        ? (BoardBatteryDisplayOption)temp_setting_value
                                        : BATTERY_DISPLAY_PERCENT;

  device_settings.pocket_mode =
      nvs_read_int("pocket_mode", &temp_setting_value) == ESP_OK ? (bool)temp_setting_value : POCKET_MODE_DISABLED;

  device_settings.double_press_action = nvs_read_int("stats_dp", &temp_setting_value) == ESP_OK
                                            ? (StatsDoublePressAction)temp_setting_value
                                            : DEFAULT_DOUBLE_PRESS_ACTION;

  // Reading calibration settings
  calibration_settings.x_min =
      nvs_read_int("x_min", &temp_setting_value) == ESP_OK ? (uint16_t)temp_setting_value : STICK_MIN_VAL;
  calibration_settings.x_max =
      nvs_read_int("x_max", &temp_setting_value) == ESP_OK ? (uint16_t)temp_setting_value : STICK_MAX_VAL;

  calibration_settings.y_min =
      nvs_read_int("y_min", &temp_setting_value) == ESP_OK ? (uint16_t)temp_setting_value : STICK_MIN_VAL;

  calibration_settings.y_max =
      nvs_read_int("y_max", &temp_setting_value) == ESP_OK ? (uint16_t)temp_setting_value : STICK_MAX_VAL;

  calibration_settings.x_center =
      nvs_read_int("x_center", &temp_setting_value) == ESP_OK ? (uint16_t)temp_setting_value : STICK_MID_VAL;

  calibration_settings.y_center =
      nvs_read_int("y_center", &temp_setting_value) == ESP_OK ? (uint16_t)temp_setting_value : STICK_MID_VAL;

  calibration_settings.deadband =
      nvs_read_int("deadband", &temp_setting_value) == ESP_OK ? (uint16_t)temp_setting_value : STICK_DEADBAND;

  calibration_settings.expo = nvs_read_int("expo", &temp_setting_value) == ESP_OK
                                  ? (float)(temp_setting_value / EXPO_ADJUST_FACTOR)
                                  : STICK_EXPO;

  calibration_settings.invert_y =
      nvs_read_int("invert_y", &temp_setting_value) == ESP_OK ? (bool)temp_setting_value : INVERT_Y_AXIS;

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
  if (nvs_read_int("paired_blob_ver", &blob_ver) != ESP_OK || blob_ver != 2) {
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
  }
  else {
    memcpy(pairing_settings.remote_addr, DEFAULT_PEER_ADDR, sizeof(DEFAULT_PEER_ADDR));
    pairing_settings.channel = 1;
  }

  return ESP_OK;
}