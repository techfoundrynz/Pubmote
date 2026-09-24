#pragma once
#include "display.h"
#include "esp_system.h"
#include "led.h"
#include "nvs_flash.h"
#include "settings_types.h"

#include "comms.h"
#include <esp_now.h>
#include <esp_wifi.h>
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

  // Restore the default range for axes whose pin changed
  void reset_axis_calibration(bool reset_x, bool reset_y);

  void save_imu_calibration();

  esp_err_t save_pairing_data();

  esp_err_t reset_all_settings();

  esp_err_t save_wifi_ssid(const char *ssid);

  esp_err_t save_wifi_password(const char *password);

  char *get_wifi_ssid();

  char *get_wifi_password();

#define DEFAULT_PAIRING_SECRET_CODE -1
  // The assignment baked in at build time
  void input_pins_load_defaults(InputPinSettings *out);

  uint64_t get_auto_off_ms();
  bool is_pocket_mode_enabled();

  SettingOptions settings_double_press_options();
  SettingOptions settings_rotation_options();
  SettingOptions settings_auto_off_options();
  SettingOptions settings_temp_units_options();
  SettingOptions settings_distance_units_options();
  SettingOptions settings_startup_sound_options();

  extern CalibrationSettings calibration_settings;
  extern InputPinSettings input_pin_settings;
  extern DeviceSettings device_settings;
  extern PairingSettings pairing_settings;
  extern ImuCalibrationSettings imu_calibration;

  void save_imu_calibration();
  void settings_apply_imu_calibration(const ImuCalibrationSettings *imu);
  // Replaces the paired boards, persists them and reconnects to the default board.
  esp_err_t settings_replace_pairing(const PairedDevice *devices, uint8_t count, int8_t default_index);

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

  CommsType settings_get_board_comms_mode(int8_t index);
  CommsType settings_get_active_comms_mode(void);

#ifdef __cplusplus
}
#endif
