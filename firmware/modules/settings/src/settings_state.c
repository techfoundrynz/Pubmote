#include "remote/settings_state.h"
#include "freertos/FreeRTOS.h"
#include <math.h>
#include <string.h>
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static SettingsSnapshot state;
void settings_state_init(const SettingsSnapshot *initial) {
  if (!initial)
    return;
  portENTER_CRITICAL(&state_lock);
  state = *initial;
  portEXIT_CRITICAL(&state_lock);
}
SettingsSnapshot settings_snapshot(void) {
  portENTER_CRITICAL(&state_lock);
  SettingsSnapshot snapshot = state;
  portEXIT_CRITICAL(&state_lock);
  return snapshot;
}
#define GETTER(name, type, member)                                                                                     \
  type settings_get_##name(void) {                                                                                     \
    portENTER_CRITICAL(&state_lock);                                                                                   \
    type snapshot = state.member;                                                                                      \
    portEXIT_CRITICAL(&state_lock);                                                                                    \
    return snapshot;                                                                                                   \
  }
GETTER(device, DeviceSettings, device)
GETTER(pins, InputPinSettings, pins)
GETTER(calibration, CalibrationSettings, calibration)
GETTER(imu, ImuCalibrationSettings, imu)
GETTER(pairing, PairingSettings, pairing)

bool settings_update_device(const DeviceSettings *patch, uint32_t mask) {
  if (!patch || (mask & ~((1u << 13) - 1)))
    return false;
  if ((mask & SETTINGS_DEVICE_SCREEN_ROTATION) && (uint32_t)patch->screen_rotation > SCREEN_ROTATION_COUNT - 1)
    return false;
  if ((mask & SETTINGS_DEVICE_AUTO_OFF_TIME) && (uint32_t)patch->auto_off_time > AUTO_OFF_COUNT - 1)
    return false;
  if ((mask & SETTINGS_DEVICE_TEMP_UNITS) && (uint32_t)patch->temp_units > TEMP_UNITS_COUNT - 1)
    return false;
  if ((mask & SETTINGS_DEVICE_DISTANCE_UNITS) && (uint32_t)patch->distance_units > DISTANCE_UNITS_COUNT - 1)
    return false;
  if ((mask & SETTINGS_DEVICE_STARTUP_SOUND) && (uint32_t)patch->startup_sound > STARTUP_SOUND_COUNT - 1)
    return false;
  if ((mask & SETTINGS_DEVICE_BATTERY_DISPLAY) && (uint32_t)patch->battery_display > BATTERY_DISPLAY_VOLTAGE)
    return false;
  if ((mask & SETTINGS_DEVICE_SECONDARY_STAT_DISPLAY) &&
      (uint32_t)patch->secondary_stat_display > SECONDARY_STAT_DISTANCE)
    return false;
  if ((mask & SETTINGS_DEVICE_POCKET_MODE) && (uint32_t)patch->pocket_mode > POCKET_MODE_ENABLED)
    return false;
  if ((mask & SETTINGS_DEVICE_DOUBLE_PRESS_ACTION) &&
      (uint32_t)patch->double_press_action > DOUBLE_PRESS_ACTION_COUNT - 1)
    return false;
  if ((mask & SETTINGS_DEVICE_HBM_MODE) && (uint32_t)patch->hbm_mode > HBM_MODE_COUNT - 1)
    return false;
  if ((mask & SETTINGS_DEVICE_LED_MODE) && (uint32_t)patch->led_mode > LED_MODE_COUNT - 1)
    return false;
  portENTER_CRITICAL(&state_lock);
  if (mask & SETTINGS_DEVICE_BL_LEVEL)
    state.device.bl_level = patch->bl_level;
  if (mask & SETTINGS_DEVICE_SCREEN_ROTATION)
    state.device.screen_rotation = patch->screen_rotation;
  if (mask & SETTINGS_DEVICE_AUTO_OFF_TIME)
    state.device.auto_off_time = patch->auto_off_time;
  if (mask & SETTINGS_DEVICE_TEMP_UNITS)
    state.device.temp_units = patch->temp_units;
  if (mask & SETTINGS_DEVICE_DISTANCE_UNITS)
    state.device.distance_units = patch->distance_units;
  if (mask & SETTINGS_DEVICE_STARTUP_SOUND)
    state.device.startup_sound = patch->startup_sound;
  if (mask & SETTINGS_DEVICE_THEME_COLOR)
    state.device.theme_color = patch->theme_color;
  if (mask & SETTINGS_DEVICE_BATTERY_DISPLAY)
    state.device.battery_display = patch->battery_display;
  if (mask & SETTINGS_DEVICE_SECONDARY_STAT_DISPLAY)
    state.device.secondary_stat_display = patch->secondary_stat_display;
  if (mask & SETTINGS_DEVICE_POCKET_MODE)
    state.device.pocket_mode = patch->pocket_mode;
  if (mask & SETTINGS_DEVICE_DOUBLE_PRESS_ACTION)
    state.device.double_press_action = patch->double_press_action;
  if (mask & SETTINGS_DEVICE_HBM_MODE)
    state.device.hbm_mode = patch->hbm_mode;
  if (mask & SETTINGS_DEVICE_LED_MODE)
    state.device.led_mode = patch->led_mode;
  portEXIT_CRITICAL(&state_lock);
  return true;
}
bool settings_set_calibration(const CalibrationSettings *calibration) {
  if (!calibration || !isfinite(calibration->expo) || calibration->expo < 0 || calibration->expo > 100)
    return false;
  portENTER_CRITICAL(&state_lock);
  state.calibration = *calibration;
  portEXIT_CRITICAL(&state_lock);
  return true;
}
bool settings_set_imu(const ImuCalibrationSettings *imu) {
  if (!imu || !isfinite(imu->accel_x_offset) || !isfinite(imu->accel_y_offset) || !isfinite(imu->accel_z_offset) ||
      fabsf(imu->accel_x_offset) > 1000 || fabsf(imu->accel_y_offset) > 1000 || fabsf(imu->accel_z_offset) > 1000)
    return false;
  portENTER_CRITICAL(&state_lock);
  state.imu = *imu;
  portEXIT_CRITICAL(&state_lock);
  return true;
}
void settings_publish_input(const InputPinSettings *pins, const CalibrationSettings *calibration) {
  if (!pins || !calibration)
    return;
  portENTER_CRITICAL(&state_lock);
  state.pins = *pins;
  state.calibration = *calibration;
  portEXIT_CRITICAL(&state_lock);
}
bool settings_publish_pairing(const PairingSettings *pairing) {
  if (!pairing || pairing->device_count > MAX_PAIRED_DEVICES || pairing->default_index < -1 ||
      pairing->default_index >= (int8_t)pairing->device_count)
    return false;
  portENTER_CRITICAL(&state_lock);
  state.pairing = *pairing;
  portEXIT_CRITICAL(&state_lock);
  return true;
}
static int find_peer(const uint8_t *mac) {
  for (unsigned i = 0; i < state.pairing.device_count; ++i) {
    if (!memcmp(state.pairing.devices[i].mac, mac, PAIRED_MAC_BYTES))
      return i;
  }
  return -1;
}
static void select_peer(int8_t index) {
  PairedDevice *peer = &state.pairing.devices[index];
  state.pairing.default_index = index;
  memcpy(state.pairing.remote_addr, peer->mac, PAIRED_MAC_BYTES);
  state.pairing.channel = peer->channel;
  state.pairing.secret_code = peer->secret_code;
}
bool settings_select_device(int8_t index) {
  portENTER_CRITICAL(&state_lock);
  bool valid = index >= 0 && index < state.pairing.device_count;
  if (valid)
    select_peer(index);
  portEXIT_CRITICAL(&state_lock);
  return valid;
}
bool settings_remove_device(uint8_t index) {
  portENTER_CRITICAL(&state_lock);
  bool valid = index < state.pairing.device_count;
  if (valid) {
    PairingSettings *p = &state.pairing;
    memmove(&p->devices[index], &p->devices[index + 1], (p->device_count - index - 1) * sizeof(PairedDevice));
    --p->device_count;
    if (p->default_index == (int8_t)index) {
      if (p->device_count)
        select_peer(0);
      else {
        p->default_index = -1;
        memset(p->remote_addr, 0xff, PAIRED_MAC_BYTES);
        p->channel = 1;
        p->secret_code = UINT32_MAX;
      }
    }
    else if (p->default_index > (int8_t)index)
      --p->default_index;
  }
  portEXIT_CRITICAL(&state_lock);
  return valid;
}
void settings_remember_active_peer(void) {
  const uint8_t unset[PAIRED_MAC_BYTES] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  portENTER_CRITICAL(&state_lock);
  PairingSettings *p = &state.pairing;
  if (memcmp(p->remote_addr, unset, PAIRED_MAC_BYTES)) {
    int index = find_peer(p->remote_addr);
    if (index < 0) {
      index = p->device_count < MAX_PAIRED_DEVICES ? p->device_count++ : 0;
      memcpy(p->devices[index].mac, p->remote_addr, PAIRED_MAC_BYTES);
      p->devices[index].vehicle_type = 0;
    }
    p->devices[index].secret_code = p->secret_code;
    p->devices[index].channel = p->channel;
    select_peer(index);
  }
  portEXIT_CRITICAL(&state_lock);
}
void settings_set_peer(const uint8_t mac[PAIRED_MAC_BYTES], uint8_t channel) {
  if (!mac)
    return;
  portENTER_CRITICAL(&state_lock);
  memcpy(state.pairing.remote_addr, mac, PAIRED_MAC_BYTES);
  state.pairing.channel = channel;
  portEXIT_CRITICAL(&state_lock);
}
void settings_set_channel(uint8_t channel) {
  portENTER_CRITICAL(&state_lock);
  state.pairing.channel = channel;
  portEXIT_CRITICAL(&state_lock);
}
void settings_set_pairing_secret(uint32_t secret) {
  portENTER_CRITICAL(&state_lock);
  state.pairing.secret_code = secret;
  portEXIT_CRITICAL(&state_lock);
}
bool settings_set_saved_secret(uint32_t secret) {
  portENTER_CRITICAL(&state_lock);
  int index = state.pairing.default_index;
  if (index < 0 || index >= state.pairing.device_count)
    index = find_peer(state.pairing.remote_addr);
  if (index >= 0) {
    state.pairing.devices[index].secret_code = secret;
    state.pairing.secret_code = secret;
  }
  portEXIT_CRITICAL(&state_lock);
  return index >= 0;
}
bool settings_set_saved_vehicle(const uint8_t mac[PAIRED_MAC_BYTES], uint8_t vehicle) {
  if (!mac)
    return false;
  portENTER_CRITICAL(&state_lock);
  int index = find_peer(mac);
  bool changed = index >= 0 && state.pairing.devices[index].vehicle_type != vehicle;
  if (changed)
    state.pairing.devices[index].vehicle_type = vehicle;
  portEXIT_CRITICAL(&state_lock);
  return changed;
}

bool settings_set_bl_level(uint8_t value) {
  DeviceSettings patch = {0};
  patch.bl_level = value;
  return settings_update_device(&patch, SETTINGS_DEVICE_BL_LEVEL);
}
bool settings_set_screen_rotation(ScreenRotation value) {
  DeviceSettings patch = {0};
  patch.screen_rotation = value;
  return settings_update_device(&patch, SETTINGS_DEVICE_SCREEN_ROTATION);
}
bool settings_set_auto_off_time(AutoOffOptions value) {
  DeviceSettings patch = {0};
  patch.auto_off_time = value;
  return settings_update_device(&patch, SETTINGS_DEVICE_AUTO_OFF_TIME);
}
bool settings_set_temp_units(TempUnits value) {
  DeviceSettings patch = {0};
  patch.temp_units = value;
  return settings_update_device(&patch, SETTINGS_DEVICE_TEMP_UNITS);
}
bool settings_set_distance_units(DistanceUnits value) {
  DeviceSettings patch = {0};
  patch.distance_units = value;
  return settings_update_device(&patch, SETTINGS_DEVICE_DISTANCE_UNITS);
}
bool settings_set_startup_sound(StartupSoundOptions value) {
  DeviceSettings patch = {0};
  patch.startup_sound = value;
  return settings_update_device(&patch, SETTINGS_DEVICE_STARTUP_SOUND);
}
bool settings_set_theme_color(uint32_t value) {
  DeviceSettings patch = {0};
  patch.theme_color = value;
  return settings_update_device(&patch, SETTINGS_DEVICE_THEME_COLOR);
}
bool settings_set_battery_display(BoardBatteryDisplayOption value) {
  DeviceSettings patch = {0};
  patch.battery_display = value;
  return settings_update_device(&patch, SETTINGS_DEVICE_BATTERY_DISPLAY);
}
bool settings_set_secondary_stat_display(SecondaryStatDisplayOption value) {
  DeviceSettings patch = {0};
  patch.secondary_stat_display = value;
  return settings_update_device(&patch, SETTINGS_DEVICE_SECONDARY_STAT_DISPLAY);
}
bool settings_set_pocket_mode(PocketModeOptions value) {
  DeviceSettings patch = {0};
  patch.pocket_mode = value;
  return settings_update_device(&patch, SETTINGS_DEVICE_POCKET_MODE);
}
bool settings_set_double_press_action(StatsDoublePressAction value) {
  DeviceSettings patch = {0};
  patch.double_press_action = value;
  return settings_update_device(&patch, SETTINGS_DEVICE_DOUBLE_PRESS_ACTION);
}
bool settings_set_hbm_mode(HbmModeOptions value) {
  DeviceSettings patch = {0};
  patch.hbm_mode = value;
  return settings_update_device(&patch, SETTINGS_DEVICE_HBM_MODE);
}
bool settings_set_led_mode(LedModeOptions value) {
  DeviceSettings patch = {0};
  patch.led_mode = value;
  return settings_update_device(&patch, SETTINGS_DEVICE_LED_MODE);
}
