#pragma once
#include "remote/settings_snapshot.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
  // Boot composition only: publish loaded records before starting workers.
  void settings_state_init(const SettingsSnapshot *initial);
  // Masked updates preserve preferences changed by another task.
  bool settings_update_device(const DeviceSettings *patch, uint32_t mask);
  bool settings_set_calibration(const CalibrationSettings *calibration);
  bool settings_set_imu(const ImuCalibrationSettings *imu);
  void settings_publish_input(const InputPinSettings *pins, const CalibrationSettings *calibration);
  bool settings_publish_pairing(const PairingSettings *pairing);
  bool settings_select_device(int8_t index);
  bool settings_remove_device(uint8_t index);
  void settings_remember_active_peer(void);
  void settings_set_peer(const uint8_t mac[PAIRED_MAC_BYTES], uint8_t channel);
  void settings_set_channel(uint8_t channel);
  void settings_set_pairing_secret(uint32_t secret);
  bool settings_set_saved_secret(uint32_t secret);
  bool settings_set_saved_vehicle(const uint8_t mac[PAIRED_MAC_BYTES], uint8_t vehicle);
#define SETTINGS_DEVICE_BL_LEVEL (1u << 0)
  bool settings_set_bl_level(uint8_t value);
#define SETTINGS_DEVICE_SCREEN_ROTATION (1u << 1)
  bool settings_set_screen_rotation(ScreenRotation value);
#define SETTINGS_DEVICE_AUTO_OFF_TIME (1u << 2)
  bool settings_set_auto_off_time(AutoOffOptions value);
#define SETTINGS_DEVICE_TEMP_UNITS (1u << 3)
  bool settings_set_temp_units(TempUnits value);
#define SETTINGS_DEVICE_DISTANCE_UNITS (1u << 4)
  bool settings_set_distance_units(DistanceUnits value);
#define SETTINGS_DEVICE_STARTUP_SOUND (1u << 5)
  bool settings_set_startup_sound(StartupSoundOptions value);
#define SETTINGS_DEVICE_THEME_COLOR (1u << 6)
  bool settings_set_theme_color(uint32_t value);
#define SETTINGS_DEVICE_BATTERY_DISPLAY (1u << 7)
  bool settings_set_battery_display(BoardBatteryDisplayOption value);
#define SETTINGS_DEVICE_SECONDARY_STAT_DISPLAY (1u << 8)
  bool settings_set_secondary_stat_display(SecondaryStatDisplayOption value);
#define SETTINGS_DEVICE_POCKET_MODE (1u << 9)
  bool settings_set_pocket_mode(PocketModeOptions value);
#define SETTINGS_DEVICE_DOUBLE_PRESS_ACTION (1u << 10)
  bool settings_set_double_press_action(StatsDoublePressAction value);
#define SETTINGS_DEVICE_HBM_MODE (1u << 11)
  bool settings_set_hbm_mode(HbmModeOptions value);
#define SETTINGS_DEVICE_LED_MODE (1u << 12)
  bool settings_set_led_mode(LedModeOptions value);
#ifdef __cplusplus
}
#endif
