#ifndef __SETTINGS_H
#define __SETTINGS_H
#include "display.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include <esp_now.h>
#include <remote/receiver.h>

#ifdef __cplusplus
extern "C"
{
#endif

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

  void save_input_calibration();

  void save_imu_calibration();

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
    BATTERY_DISPLAY_PERCENT,
    BATTERY_DISPLAY_VOLTAGE,
  } BoardBatteryDisplayOption;

  typedef enum {
    SECONDARY_STAT_DUTY,
    SECONDARY_STAT_TEMPS,
    SECONDARY_STAT_DISTANCE,
  } SecondaryStatDisplayOption;

  typedef enum {
    POCKET_MODE_DISABLED,
    POCKET_MODE_ENABLED,
  } PocketModeOptions;

  typedef enum {
    DOUBLE_PRESS_ACTION_NONE,
    DOUBLE_PRESS_ACTION_OPEN_MENU,
  } StatsDoublePressAction;

#define DEFAULT_PAIRING_SECRET_CODE -1

  typedef struct {
    uint32_t secret_code;
    uint8_t remote_addr[ESP_NOW_ETH_ALEN];
    uint8_t channel;
  } PairingSettings;

  typedef struct {
    uint16_t x_min;
    uint16_t x_max;
    uint16_t y_min;
    uint16_t y_max;
    uint16_t x_center;
    uint16_t y_center;
    uint16_t deadband;
    bool invert_y;
    bool invert_x;
    float expo;
  } CalibrationSettings;

  typedef struct {
    float accel_x_offset;
    float accel_y_offset;
    float accel_z_offset;
    bool invert_x;
    bool invert_y;
    bool invert_z;
    bool swap_xy;
  } ImuCalibrationSettings;

  typedef struct {
    uint8_t bl_level;
    ScreenRotation screen_rotation;
    AutoOffOptions auto_off_time;
    TempUnits temp_units;
    DistanceUnits distance_units;
    StartupSoundOptions startup_sound;
    uint32_t theme_color;

    BoardBatteryDisplayOption battery_display;
    SecondaryStatDisplayOption secondary_stat_display;
    PocketModeOptions pocket_mode;
    StatsDoublePressAction double_press_action;
    bool raise_to_hbm;
  } DeviceSettings;

  uint64_t get_auto_off_ms();
  bool is_pocket_mode_enabled();

  extern CalibrationSettings calibration_settings;
  extern DeviceSettings device_settings;
  extern PairingSettings pairing_settings;
  extern ImuCalibrationSettings imu_calibration;

  void save_imu_calibration();

#ifdef __cplusplus
}
#endif

#endif