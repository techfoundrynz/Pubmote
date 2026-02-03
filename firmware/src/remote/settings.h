#ifndef __SETTINGS_H
#define __SETTINGS_H
#include "display.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include <core/lv_obj.h>
#include <esp_now.h>
#include <remote/receiver.h>

// Function to initialize settings (and NVS)
esp_err_t settings_init();

// Function to write an integer to NVS
esp_err_t nvs_write_int(const char *key, uint32_t value);

// Function to read an integer from NVS
esp_err_t nvs_read_int(const char *key, uint32_t *value);

// Function to write a string to NVS
esp_err_t nvs_write_str(const char *key, const char *value);

// Function to read a string from NVS
esp_err_t nvs_read_str(const char *key, char *out_value, size_t *length);

// Function to write a byte array to NVS
esp_err_t nvs_write_blob(const char *key, void *value, size_t length);

// Function to read a byte array from NVS
esp_err_t nvs_read_blob(const char *key, void *value, size_t length);

void save_device_settings();

void save_calibration();

esp_err_t save_pairing_data();

esp_err_t reset_all_settings();

esp_err_t save_wifi_ssid(const char *ssid);

esp_err_t save_wifi_password(const char *password);

char *get_wifi_ssid();

char *get_wifi_password();

typedef enum {
  AUTO_OFF_DISABLED,
  AUTO_OFF_2_MINUTES,
  AUTO_OFF_5_MINUTES,
  AUTO_OFF_10_MINUTES,
  AUTO_OFF_20_MINUTES,
  AUTO_OFF_30_MINUTES,
} AutoOffOptions;

typedef enum {
  TEMP_UNITS_CELSIUS,
  TEMP_UNITS_FAHRENHEIT,
} TempUnits;

typedef enum {
  DISTANCE_UNITS_METRIC,
  DISTANCE_UNITS_IMPERIAL,
} DistanceUnits;

typedef enum {
  STARTUP_SOUND_DISABLED,
  STARTUP_SOUND_BEEP,
  STARTUP_SOUND_MELODY,
} StartupSoundOptions;

typedef enum {
  DARK_TEXT_DISABLED,
  DARK_TEXT_ENABLED,
} DarkTextOptions;

typedef enum {
  BATTERY_DISPLAY_PERCENT,
  BATTERY_DISPLAY_VOLTAGE,
  BATTERY_DISPLAY_ALL,
} BoardBatteryDisplayOption;

typedef enum {
  POCKET_MODE_DISABLED,
  POCKET_MODE_ENABLED,
} PocketModeOptions;

typedef enum {
  DOUBLE_PRESS_ACTION_NONE,
  DOUBLE_PRESS_ACTION_OPEN_MENU,
} StatsDoublePressAction;

#define DEFAULT_PAIRING_SECRET_CODE -1
#define MAX_PAIRED_DEVICES 5

typedef struct {
  uint8_t mac[ESP_NOW_ETH_ALEN];
  uint8_t channel;
  uint32_t secret_code;
} PairedDevice;

typedef struct {
  uint32_t secret_code;
  // Selected/default device (for compatibility with existing code paths)
  uint8_t remote_addr[ESP_NOW_ETH_ALEN];
  uint8_t channel;
  // Multi-device support
  PairedDevice devices[MAX_PAIRED_DEVICES];
  uint8_t device_count; // number of valid entries in devices
  int8_t default_index; // -1 if none selected
} PairingSettings;

typedef struct {
  uint16_t x_min;
  uint16_t x_max;
  uint16_t y_min;
  uint16_t y_max;
  uint16_t x_center;
  uint16_t y_center;
  uint16_t deadband;
  float expo;
  bool invert_y;
} CalibrationSettings;

typedef struct {
  uint8_t bl_level;
  ScreenRotation screen_rotation;
  AutoOffOptions auto_off_time;
  TempUnits temp_units;
  DistanceUnits distance_units;
  StartupSoundOptions startup_sound;
  uint32_t theme_color;
  bool dark_text;
  BoardBatteryDisplayOption battery_display;
  PocketModeOptions pocket_mode;
  StatsDoublePressAction double_press_action;
} DeviceSettings;

uint64_t get_auto_off_ms();
bool is_pocket_mode_enabled();

extern CalibrationSettings calibration_settings;
extern DeviceSettings device_settings;
extern PairingSettings pairing_settings;

// Returns true if the given mac matches any paired device
bool is_paired_mac(const uint8_t *mac);

// Set the default device by index (0..device_count-1); updates remote_addr/channel accordingly
void set_default_device_index(int8_t idx);

// Delete a paired device by index; compacts list and updates default selection/state
bool delete_paired_device_index(uint8_t idx);

// Helpers for multi-device management
uint8_t get_paired_device_count();
int8_t get_default_device_index();
bool get_paired_device(uint8_t idx, PairedDevice *out_device);

// Set active/default paired device and persist
bool set_active_paired_device(uint8_t idx);

// Update the secret code on the current default device
bool set_current_default_device_secret(uint32_t secret_code);

#endif