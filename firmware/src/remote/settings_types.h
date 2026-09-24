#pragma once
#include <stdbool.h>
#include <stdint.h>

#define SETTINGS_SCHEMA_VERSION 2

#define WIFI_SSID_MAX_BYTES 32
#define WIFI_PASSWORD_MAX_BYTES 64

#define INPUT_PIN_DISABLED (-1)

// Runtime input pins, defaulting to the board's build flags
typedef struct InputPinSettings {
  int8_t js_x_gpio;          // GPIO used for the X axis (must be ADC capable)
  int8_t js_y_gpio;          // GPIO used for the Y axis (must be ADC capable)
  int8_t btn1_gpio;          // GPIO used for the primary button
  uint8_t btn1_active_level; // 0: active low (switch), 1: active high (ps5)
} InputPinSettings;

typedef enum {
  SCREEN_ROTATION_0,
  SCREEN_ROTATION_90,
  SCREEN_ROTATION_180,
  SCREEN_ROTATION_270,
  SCREEN_ROTATION_COUNT // Sentinel - keep last
} ScreenRotation;

// High Brightness Mode, persisted in device_settings.hbm_mode and cycled from
// the main menu. Order is the cycle order shown to the user.
typedef enum {
  HBM_MODE_OFF,
  HBM_MODE_ON,
  HBM_MODE_RAISED, // Driven by the raise-to-view gesture, requires an IMU
  HBM_MODE_COUNT   // Sentinel - keep last
} HbmModeOptions;

// User-selectable LED behaviour, persisted in device_settings.led_mode and
// cycled from the main menu. Order is the cycle order shown to the user, and
// the values are persisted in NVS - append rather than renumber.
typedef enum {
  LED_MODE_OFF,    // Always dark, alerts included
  LED_MODE_SOLID,  // Solid theme colour
  LED_MODE_ALERTS, // Dark unless a duty alert is active
  LED_MODE_COUNT   // Sentinel - keep last
} LedModeOptions;

typedef enum {
  AUTO_OFF_DISABLED,
  AUTO_OFF_2_MINUTES,
  AUTO_OFF_5_MINUTES,
  AUTO_OFF_10_MINUTES,
  AUTO_OFF_20_MINUTES,
  AUTO_OFF_30_MINUTES,
  AUTO_OFF_COUNT // Sentinel - keep last
} AutoOffOptions;

typedef enum {
  TEMP_UNITS_CELSIUS,
  TEMP_UNITS_FAHRENHEIT,
  TEMP_UNITS_COUNT // Sentinel - keep last
} TempUnits;

typedef enum {
  DISTANCE_UNITS_METRIC,
  DISTANCE_UNITS_IMPERIAL,
  DISTANCE_UNITS_COUNT // Sentinel - keep last
} DistanceUnits;

typedef enum {
  STARTUP_SOUND_DISABLED,
  STARTUP_SOUND_BEEP,
  STARTUP_SOUND_MELODY,
  STARTUP_SOUND_COUNT // Sentinel - keep last
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
  DOUBLE_PRESS_ACTION_COUNT // Sentinel - keep last
} StatsDoublePressAction;

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
  HbmModeOptions hbm_mode;
  LedModeOptions led_mode;
} DeviceSettings;

// The option list backing a settings dropdown. `labels` is indexed by the
// setting's enum value, so the UI never needs to know the enum ordering.
typedef struct {
  const char *const *labels;
  uint8_t count;
} SettingOptions;

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

#define MAX_PAIRED_DEVICES 5
#define PAIRED_MAC_BYTES 6

typedef struct {
  uint8_t mac[PAIRED_MAC_BYTES];
  uint8_t channel; // Bit 7 set: BLE
  uint32_t secret_code;
  uint8_t vehicle_type;
} PairedDevice;

typedef struct {
  uint32_t secret_code;
  // Selected/default device (for compatibility with existing code paths)
  uint8_t remote_addr[PAIRED_MAC_BYTES];
  uint8_t channel;
  // Multi-device support
  PairedDevice devices[MAX_PAIRED_DEVICES];
  uint8_t device_count; // number of valid entries in devices
  int8_t default_index; // -1 if none selected
} PairingSettings;

typedef struct {
  float accel_x_offset;
  float accel_y_offset;
  float accel_z_offset;
  bool invert_x;
  bool invert_y;
  bool invert_z;
  bool swap_xy;
} ImuCalibrationSettings;
