#include "settings_api.h"
#include "../config.h"
#include "cJSON.h"
#include "input_settings.h"
#include "powermanagement.h"
#include "remoteinputs.h"
#include "settings.h"
#include <ctype.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Dropdown option labels. Each table is indexed by its enum value and asserted
// against that enum's _COUNT, so extending an enum without adding a label fails
// the build instead of silently shifting what the UI saves.
#define DEFINE_SETTING_OPTIONS(fn_name, table, count_sentinel)                                                         \
  _Static_assert(sizeof(table) / sizeof((table)[0]) == (count_sentinel), #table " out of sync with " #count_sentinel); \
  SettingOptions fn_name(void) {                                                                                       \
    SettingOptions options = {.labels = table, .count = sizeof(table) / sizeof((table)[0])};                           \
    return options;                                                                                                    \
  }

static const char *const DOUBLE_PRESS_LABELS[] = {"None", "Open menu"};
DEFINE_SETTING_OPTIONS(settings_double_press_options, DOUBLE_PRESS_LABELS, DOUBLE_PRESS_ACTION_COUNT)

static const char *const ROTATION_LABELS[] = {"None", "90 degrees", "180 degrees", "270 degrees"};
DEFINE_SETTING_OPTIONS(settings_rotation_options, ROTATION_LABELS, SCREEN_ROTATION_COUNT)

static const char *const AUTO_OFF_LABELS[] = {"Disabled",   "2 minutes",  "5 minutes",
                                              "10 minutes", "20 minutes", "30 minutes"};
DEFINE_SETTING_OPTIONS(settings_auto_off_options, AUTO_OFF_LABELS, AUTO_OFF_COUNT)

static const char *const TEMP_UNITS_LABELS[] = {"Celsius", "Fahrenheit"};
DEFINE_SETTING_OPTIONS(settings_temp_units_options, TEMP_UNITS_LABELS, TEMP_UNITS_COUNT)

static const char *const DISTANCE_UNITS_LABELS[] = {"Kilometers", "Miles"};
DEFINE_SETTING_OPTIONS(settings_distance_units_options, DISTANCE_UNITS_LABELS, DISTANCE_UNITS_COUNT)

static const char *const STARTUP_SOUND_LABELS[] = {"Disabled", "Beep", "Melody"};
DEFINE_SETTING_OPTIONS(settings_startup_sound_options, STARTUP_SOUND_LABELS, STARTUP_SOUND_COUNT)

static const char *const HBM_LABELS[] = {"Off", "On", "Raised"};
_Static_assert(sizeof(HBM_LABELS) / sizeof(HBM_LABELS[0]) == HBM_MODE_COUNT, "HBM_LABELS out of sync");
static SettingOptions hbm_options(void) {
  return (SettingOptions){HBM_LABELS, display_supports_hbm() ? (IMU_ENABLED ? HBM_MODE_COUNT : 2) : 1};
}

static const char *const LED_LABELS[] = {"Off", "Solid", "Alerts"};
_Static_assert(sizeof(LED_LABELS) / sizeof(LED_LABELS[0]) == LED_MODE_COUNT, "LED_LABELS out of sync");
static SettingOptions led_options(void) {
  return (SettingOptions){LED_LABELS, led_is_supported() ? LED_MODE_COUNT : 1};
}

static const char *const BATTERY_LABELS[] = {"Percentage", "Voltage"};
DEFINE_SETTING_OPTIONS(battery_options, BATTERY_LABELS, BATTERY_DISPLAY_VOLTAGE + 1)
static const char *const SECONDARY_LABELS[] = {"Duty cycle", "Temperatures", "Distance"};
DEFINE_SETTING_OPTIONS(secondary_options, SECONDARY_LABELS, SECONDARY_STAT_DISTANCE + 1)
static const char *const POCKET_LABELS[] = {"Disabled", "Enabled"};
DEFINE_SETTING_OPTIONS(pocket_options, POCKET_LABELS, POCKET_MODE_ENABLED + 1)
static const char *const OFF_ON_LABELS[] = {"Off", "On"};
DEFINE_SETTING_OPTIONS(off_on_options, OFF_ON_LABELS, 2)
static const char *const TRANSPORT_LABELS[] = {"ESP-NOW", "BLE"};
DEFINE_SETTING_OPTIONS(transport_options, TRANSPORT_LABELS, 2)
static const char *const VEHICLE_LABELS[] = {"Unspecified", "Onewheel", "E-skate", "Scooter", "EUC"};
DEFINE_SETTING_OPTIONS(vehicle_options, VEHICLE_LABELS, 5)

// Typed accessors, so enum and byte members are never aliased through an int.
#define DEFINE_DEVICE_ACCESSORS(key, member, type)                                                                     \
  static uint32_t read_##key(void) {                                                                                   \
    return device_settings.member;                                                                                     \
  }                                                                                                                    \
  static void apply_##key(uint32_t value) {                                                                            \
    device_settings.member = (type)value;                                                                              \
  }
DEFINE_DEVICE_ACCESSORS(bl_level, bl_level, uint8_t)
DEFINE_DEVICE_ACCESSORS(screen_rotation, screen_rotation, ScreenRotation)
DEFINE_DEVICE_ACCESSORS(theme_color, theme_color, uint32_t)
DEFINE_DEVICE_ACCESSORS(battery_display, battery_display, BoardBatteryDisplayOption)
DEFINE_DEVICE_ACCESSORS(sec_stat_disp, secondary_stat_display, SecondaryStatDisplayOption)
DEFINE_DEVICE_ACCESSORS(hbm_mode, hbm_mode, HbmModeOptions)
DEFINE_DEVICE_ACCESSORS(auto_off_time, auto_off_time, AutoOffOptions)
DEFINE_DEVICE_ACCESSORS(pocket_mode, pocket_mode, PocketModeOptions)
DEFINE_DEVICE_ACCESSORS(temp_units, temp_units, TempUnits)
DEFINE_DEVICE_ACCESSORS(distance_units, distance_units, DistanceUnits)
DEFINE_DEVICE_ACCESSORS(startup_sound, startup_sound, StartupSoundOptions)
DEFINE_DEVICE_ACCESSORS(stats_dp, double_press_action, StatsDoublePressAction)
DEFINE_DEVICE_ACCESSORS(led_mode, led_mode, LedModeOptions)

typedef struct {
  CalibrationSettings calibration;
  ImuCalibrationSettings imu;
} SettingsRecords;

#define DEFINE_RECORD_ACCESSORS(name, record, member)                                                                  \
  static int64_t read_##name(const SettingsRecords *records) {                                                         \
    return records->record.member;                                                                                     \
  }                                                                                                                    \
  static void write_##name(SettingsRecords *records, int64_t value) {                                                  \
    records->record.member = value;                                                                                    \
  }
// Float members travel as integers in 1/scale units.
#define DEFINE_SCALED_ACCESSORS(name, record, member, scale)                                                           \
  static int64_t read_##name(const SettingsRecords *records) {                                                         \
    return llroundf(records->record.member * (scale));                                                                 \
  }                                                                                                                    \
  static void write_##name(SettingsRecords *records, int64_t value) {                                                  \
    records->record.member = (float)value / (scale);                                                                   \
  }
DEFINE_RECORD_ACCESSORS(stick_x_min, calibration, x_min)
DEFINE_RECORD_ACCESSORS(stick_x_max, calibration, x_max)
DEFINE_RECORD_ACCESSORS(stick_x_center, calibration, x_center)
DEFINE_RECORD_ACCESSORS(stick_y_min, calibration, y_min)
DEFINE_RECORD_ACCESSORS(stick_y_max, calibration, y_max)
DEFINE_RECORD_ACCESSORS(stick_y_center, calibration, y_center)
DEFINE_RECORD_ACCESSORS(stick_deadband, calibration, deadband)
DEFINE_SCALED_ACCESSORS(stick_expo, calibration, expo, 100.0f)
DEFINE_RECORD_ACCESSORS(stick_invert_x, calibration, invert_x)
DEFINE_RECORD_ACCESSORS(stick_invert_y, calibration, invert_y)
#if IMU_ENABLED
DEFINE_SCALED_ACCESSORS(imu_offset_x, imu, accel_x_offset, 1000.0f)
DEFINE_SCALED_ACCESSORS(imu_offset_y, imu, accel_y_offset, 1000.0f)
DEFINE_SCALED_ACCESSORS(imu_offset_z, imu, accel_z_offset, 1000.0f)
DEFINE_RECORD_ACCESSORS(imu_invert_x, imu, invert_x)
DEFINE_RECORD_ACCESSORS(imu_invert_y, imu, invert_y)
DEFINE_RECORD_ACCESSORS(imu_invert_z, imu, invert_z)
DEFINE_RECORD_ACCESSORS(imu_swap_xy, imu, swap_xy)
#endif

typedef struct {
  const char *key;
  const char *label;
  const char *group;
  const char *description;
  size_t max_bytes; // nonzero for strings
  bool secret;
  char *(*read_string)(void);
  const char *length_key; // NVS key holding a string's byte length
  uint32_t (*read_number)(void);
  void (*apply_number)(uint32_t value);
  SettingOptions (*options)(void);
  int64_t minimum;
  int64_t maximum; // bounded integer when options is NULL
  bool color;
  size_t pin_offset;
  uint64_t (*choices)(void);
  int64_t (*read_record)(const SettingsRecords *records);
  void (*write_record)(SettingsRecords *records, int64_t value);
  bool imu_record;
  bool read_only;
  bool paired_boards;
  bool default_board;
} SettingDescriptor;

typedef struct {
  const char *key;
  const char *label;
  size_t max_bytes;
  int64_t maximum;
  SettingOptions (*options)(void);
  bool secret;
} BoardField;

static const BoardField board_fields[] = {
    {.key = "mac", .label = "MAC address", .max_bytes = 17},
    {.key = "transport", .label = "Connection", .options = transport_options},
    {.key = "channel", .label = "Channel", .maximum = 127},
    {.key = "secret", .label = "Pairing code", .maximum = UINT32_MAX, .secret = true},
    {.key = "vehicle", .label = "Vehicle", .options = vehicle_options},
};
#define BOARD_FIELD_COUNT (sizeof(board_fields) / sizeof(board_fields[0]))

#define RECORD_FIELD(key_, label_, group_, name_)                                                                      \
  .key = key_, .label = label_, .group = group_, .read_record = read_##name_, .write_record = write_##name_,           \
  .read_only = true
#define STICK_FIELD(key_, label_, name_)                                                                               \
  RECORD_FIELD(key_, label_, "Joystick calibration", name_), .description = "Set by joystick calibration"
#define IMU_FIELD(key_, label_, name_)                                                                                 \
  RECORD_FIELD(key_, label_, "IMU calibration", name_), .description = "Set by IMU calibration", .imu_record = true

static uint64_t axis_choices(void) {
  return input_pins_adc_capable_mask() & input_pins_assignable_mask();
}

static const SettingDescriptor fields[] = {
    {.key = "wifi_ssid",
     .label = "Network name",
     .group = "Wi-Fi",
     .description = "Leave blank to keep the remote offline",
     .max_bytes = WIFI_SSID_MAX_BYTES,
     .read_string = get_wifi_ssid,
     .length_key = "wifi_ssid_l"},
    {.key = "wifi_password",
     .label = "Password",
     .group = "Wi-Fi",
     .description = "Used for over-the-air firmware updates",
     .max_bytes = WIFI_PASSWORD_MAX_BYTES,
     .secret = true,
     .read_string = get_wifi_password,
     .length_key = "wifi_key_l"},
    {.key = "js_x_gpio",
     .label = "Joystick X",
     .group = "Input pins",
     .description = "Changing this pin clears X-axis calibration",
     .pin_offset = offsetof(InputPinSettings, js_x_gpio),
     .choices = axis_choices},
    {.key = "js_y_gpio",
     .label = "Joystick Y",
     .group = "Input pins",
     .description = "Changing this pin clears Y-axis calibration",
     .pin_offset = offsetof(InputPinSettings, js_y_gpio),
     .choices = axis_choices},
    {.key = "btn1_gpio",
     .label = "Primary button",
     .group = "Input pins",
     .description = "With no button, waking from deep sleep requires a reset",
     .pin_offset = offsetof(InputPinSettings, btn1_gpio),
     .choices = input_pins_button_capable_mask},
    {.key = "btn1_level",
     .label = "Button active level",
     .group = "Input pins",
     .description = "Electrical level when the button is pressed",
     .pin_offset = offsetof(InputPinSettings, btn1_active_level)},
    {.key = "bl_level",
     .label = "Brightness",
     .group = "Display",
     .description = "Screen brightness from 10 to 255",
     .read_number = read_bl_level,
     .apply_number = apply_bl_level,
     .minimum = 10,
     .maximum = 255},
    {.key = "screen_rotation",
     .label = "Screen rotation",
     .group = "Display",
     .description = "Orientation of the display",
     .read_number = read_screen_rotation,
     .apply_number = apply_screen_rotation,
     .options = settings_rotation_options},
    {.key = "theme_color",
     .label = "Theme colour",
     .group = "Display",
     .description = "Accent colour used by the display and LEDs",
     .read_number = read_theme_color,
     .apply_number = apply_theme_color,
     .maximum = 16777215,
     .color = true},
    {.key = "battery_display",
     .label = "Board battery display",
     .group = "Display",
     .description = "Show the board battery as a percentage or voltage",
     .read_number = read_battery_display,
     .apply_number = apply_battery_display,
     .options = battery_options},
    {.key = "sec_stat_disp",
     .label = "Secondary statistic",
     .group = "Display",
     .description = "Additional information shown on the stats screen",
     .read_number = read_sec_stat_disp,
     .apply_number = apply_sec_stat_disp,
     .options = secondary_options},
    {.key = "hbm_mode",
     .label = "High brightness mode",
     .group = "Display",
     .description = "Raised mode follows the raise-to-view gesture",
     .read_number = read_hbm_mode,
     .apply_number = apply_hbm_mode,
     .options = hbm_options},
    {.key = "auto_off_time",
     .label = "Auto-off timeout",
     .group = "Power",
     .description = "Shut down after this period of inactivity",
     .read_number = read_auto_off_time,
     .apply_number = apply_auto_off_time,
     .options = settings_auto_off_options},
    {.key = "pocket_mode",
     .label = "Pocket mode",
     .group = "Power",
     .description = "Enable pocket mode",
     .read_number = read_pocket_mode,
     .apply_number = apply_pocket_mode,
     .options = pocket_options},
    {.key = "temp_units",
     .label = "Temperature units",
     .group = "Units",
     .description = "Temperature display units",
     .read_number = read_temp_units,
     .apply_number = apply_temp_units,
     .options = settings_temp_units_options},
    {.key = "distance_units",
     .label = "Distance units",
     .group = "Units",
     .description = "Distance display units",
     .read_number = read_distance_units,
     .apply_number = apply_distance_units,
     .options = settings_distance_units_options},
    {.key = "startup_sound",
     .label = "Startup sound",
     .group = "Sound",
     .description = "Sound played the next time the remote starts",
     .read_number = read_startup_sound,
     .apply_number = apply_startup_sound,
     .options = settings_startup_sound_options},
    {.key = "stats_dp",
     .label = "Double-press action",
     .group = "Buttons",
     .description = "Action when double-pressing the button on the stats screen",
     .read_number = read_stats_dp,
     .apply_number = apply_stats_dp,
     .options = settings_double_press_options},
    {.key = "led_mode",
     .label = "LED behaviour",
     .group = "LEDs",
     .description = "LED behaviour between temporary alerts and animations",
     .read_number = read_led_mode,
     .apply_number = apply_led_mode,
     .options = led_options},
    {STICK_FIELD("stick_x_min", "X minimum", stick_x_min), .maximum = UINT16_MAX},
    {STICK_FIELD("stick_x_max", "X maximum", stick_x_max), .maximum = UINT16_MAX},
    {STICK_FIELD("stick_x_center", "X centre", stick_x_center), .maximum = UINT16_MAX},
    {STICK_FIELD("stick_y_min", "Y minimum", stick_y_min), .maximum = UINT16_MAX},
    {STICK_FIELD("stick_y_max", "Y maximum", stick_y_max), .maximum = UINT16_MAX},
    {STICK_FIELD("stick_y_center", "Y centre", stick_y_center), .maximum = UINT16_MAX},
    {STICK_FIELD("stick_deadband", "Deadband", stick_deadband), .maximum = UINT16_MAX},
    {STICK_FIELD("stick_expo", "Expo (hundredths)", stick_expo), .maximum = 10000},
    {STICK_FIELD("stick_invert_x", "Invert X", stick_invert_x), .options = off_on_options},
    {STICK_FIELD("stick_invert_y", "Invert Y", stick_invert_y), .options = off_on_options},
#if IMU_ENABLED
    {IMU_FIELD("imu_offset_x", "X offset (thousandths)", imu_offset_x), .minimum = -1000000, .maximum = 1000000},
    {IMU_FIELD("imu_offset_y", "Y offset (thousandths)", imu_offset_y), .minimum = -1000000, .maximum = 1000000},
    {IMU_FIELD("imu_offset_z", "Z offset (thousandths)", imu_offset_z), .minimum = -1000000, .maximum = 1000000},
    {IMU_FIELD("imu_invert_x", "Invert X", imu_invert_x), .options = off_on_options},
    {IMU_FIELD("imu_invert_y", "Invert Y", imu_invert_y), .options = off_on_options},
    {IMU_FIELD("imu_invert_z", "Invert Z", imu_invert_z), .options = off_on_options},
    {IMU_FIELD("imu_swap_xy", "Swap X and Y", imu_swap_xy), .options = off_on_options},
#endif
    {.key = "paired_boards",
     .label = "Paired boards",
     .group = "Pairing",
     .description = "Boards this remote can connect to",
     .paired_boards = true,
     .read_only = true},
    {.key = "default_board",
     .label = "Default board",
     .group = "Pairing",
     .description = "Index of the board to connect to, or -1 for none",
     .default_board = true,
     .minimum = -1,
     .maximum = MAX_PAIRED_DEVICES - 1,
     .read_only = true},
};
#define FIELD_COUNT (sizeof(fields) / sizeof(fields[0]))

int settings_save_string(const char *key, const char *value) {
  if (!key || !value) {
    return ESP_ERR_INVALID_ARG;
  }
  for (size_t i = 0; i < FIELD_COUNT; ++i) {
    const SettingDescriptor *field = &fields[i];
    if (strcmp(key, field->key) != 0) {
      continue;
    }
    if (!field->max_bytes || strlen(value) > field->max_bytes) {
      return ESP_ERR_INVALID_ARG;
    }
    esp_err_t result = nvs_write_str(field->key, value);
    if (result != ESP_OK) {
      return result;
    }
    return nvs_write_int(field->length_key, (uint32_t)strlen(value));
  }
  return ESP_ERR_INVALID_ARG;
}

static int pin_value(const SettingDescriptor *field, const InputPinSettings *pins) {
  const uint8_t *value = (const uint8_t *)pins + field->pin_offset;
  return field->choices ? (int)(int8_t)*value : *value;
}

static bool allowed_number(const SettingDescriptor *field, double value) {
  if (!isfinite(value) || floor(value) != value) {
    return false;
  }
  if (field->read_number || field->read_record || field->default_board) {
    return value >= (double)field->minimum &&
           (field->options ? value < field->options().count : value <= (double)field->maximum);
  }
  if (!field->choices) {
    return value == 0 || value == 1;
  }
  return value == INPUT_PIN_DISABLED || (value >= 0 && value < 64 && (field->choices() & (1ULL << (int)value)));
}

int settings_save_device_preferences(void) {
  for (size_t i = 0; i < FIELD_COUNT; ++i) {
    if (fields[i].read_number && !allowed_number(&fields[i], fields[i].read_number())) {
      // Unsupported optional hardware has no effect, but keep its stored mode.
      if (!((fields[i].options == hbm_options && fields[i].read_number() < HBM_MODE_COUNT) ||
            (fields[i].options == led_options && fields[i].read_number() < LED_MODE_COUNT))) {
        return ESP_ERR_INVALID_ARG;
      }
    }
  }
  for (size_t i = 0; i < FIELD_COUNT; ++i) {
    if (fields[i].read_number) {
      int result = nvs_write_int(fields[i].key, fields[i].read_number());
      if (result != ESP_OK) {
        return result;
      }
    }
  }
  return ESP_OK;
}

static bool add_option(cJSON *options, int value, const char *label) {
  cJSON *option = cJSON_CreateObject();
  if (!option) {
    return false;
  }
  if (!cJSON_AddNumberToObject(option, "value", value) || !cJSON_AddStringToObject(option, "label", label) ||
      !cJSON_AddItemToArray(options, option)) {
    cJSON_Delete(option);
    return false;
  }
  return true;
}

static int current_default_board(void) {
  int index = pairing_settings.default_index;
  return index >= 0 && index < pairing_settings.device_count ? index : -1;
}

static cJSON *board_json(const PairedDevice *board) {
  char mac[18];
  snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", board->mac[0], board->mac[1], board->mac[2],
           board->mac[3], board->mac[4], board->mac[5]);
  cJSON *item = cJSON_CreateObject();
  if (item && cJSON_AddStringToObject(item, "mac", mac) &&
      cJSON_AddNumberToObject(item, "transport", (board->channel & 0x80) ? 1 : 0) &&
      cJSON_AddNumberToObject(item, "channel", board->channel & 0x7F) &&
      cJSON_AddNumberToObject(item, "secret", board->secret_code) &&
      cJSON_AddNumberToObject(item, "vehicle",
                              board->vehicle_type < vehicle_options().count ? board->vehicle_type : 0)) {
    return item;
  }
  cJSON_Delete(item);
  return NULL;
}

static bool allowed_board_number(const BoardField *field, const cJSON *value) {
  if (!cJSON_IsNumber(value) || !isfinite(value->valuedouble) || floor(value->valuedouble) != value->valuedouble ||
      value->valuedouble < 0) {
    return false;
  }
  return field->options ? value->valuedouble < field->options().count : value->valuedouble <= (double)field->maximum;
}

static bool parse_board(const cJSON *item, PairedDevice *board) {
  // A duplicate or unknown key leaves a known one missing.
  if (!cJSON_IsObject(item) || cJSON_GetArraySize(item) != (int)BOARD_FIELD_COUNT) {
    return false;
  }
  const cJSON *mac = cJSON_GetObjectItemCaseSensitive(item, "mac");
  if (!cJSON_IsString(mac) || strlen(mac->valuestring) != 17) {
    return false;
  }
  for (int i = 0; i < 17; ++i) {
    char c = mac->valuestring[i];
    if (i % 3 == 2 ? c != ':' : !isxdigit((unsigned char)c)) {
      return false;
    }
  }
  for (size_t i = 1; i < BOARD_FIELD_COUNT; ++i) {
    if (!allowed_board_number(&board_fields[i], cJSON_GetObjectItemCaseSensitive(item, board_fields[i].key))) {
      return false;
    }
  }
  memset(board, 0, sizeof(*board));
  for (int i = 0; i < PAIRED_MAC_BYTES; ++i) {
    char byte[3] = {mac->valuestring[3 * i], mac->valuestring[3 * i + 1], '\0'};
    board->mac[i] = (uint8_t)strtoul(byte, NULL, 16);
  }
  board->channel = (uint8_t)cJSON_GetObjectItemCaseSensitive(item, "channel")->valuedouble |
                   (cJSON_GetObjectItemCaseSensitive(item, "transport")->valuedouble ? 0x80 : 0);
  board->secret_code = (uint32_t)cJSON_GetObjectItemCaseSensitive(item, "secret")->valuedouble;
  board->vehicle_type = (uint8_t)cJSON_GetObjectItemCaseSensitive(item, "vehicle")->valuedouble;
  return true;
}

static bool pairing_changed(const PairedDevice *boards, uint8_t count, int default_board) {
  if (count != pairing_settings.device_count || default_board != current_default_board()) {
    return true;
  }
  for (uint8_t i = 0; i < count; ++i) {
    const PairedDevice *a = &boards[i], *b = &pairing_settings.devices[i];
    if (memcmp(a->mac, b->mac, PAIRED_MAC_BYTES) || a->channel != b->channel || a->secret_code != b->secret_code ||
        a->vehicle_type != b->vehicle_type) {
      return true;
    }
  }
  return false;
}

static cJSON *describe_board_fields(void) {
  cJSON *items = cJSON_CreateArray();
  if (!items) {
    return NULL;
  }
  for (size_t i = 0; i < BOARD_FIELD_COUNT; ++i) {
    const BoardField *field = &board_fields[i];
    cJSON *meta = cJSON_CreateObject();
    if (!meta || !cJSON_AddItemToArray(items, meta)) {
      cJSON_Delete(meta);
      goto fail;
    }
    const char *type = field->max_bytes ? "string" : field->options ? "integer" : "range";
    if (!cJSON_AddStringToObject(meta, "key", field->key) || !cJSON_AddStringToObject(meta, "label", field->label) ||
        !cJSON_AddStringToObject(meta, "type", type) || !cJSON_AddBoolToObject(meta, "secret", field->secret)) {
      goto fail;
    }
    if (field->max_bytes) {
      if (!cJSON_AddNumberToObject(meta, "maxBytes", (double)field->max_bytes)) {
        goto fail;
      }
    }
    else if (field->options) {
      cJSON *options = cJSON_AddArrayToObject(meta, "options");
      if (!options) {
        goto fail;
      }
      SettingOptions choices = field->options();
      for (int j = 0; j < choices.count; ++j) {
        if (!add_option(options, j, choices.labels[j])) {
          goto fail;
        }
      }
    }
    else if (!cJSON_AddNumberToObject(meta, "min", 0) ||
             !cJSON_AddNumberToObject(meta, "max", (double)field->maximum) ||
             !cJSON_AddBoolToObject(meta, "color", false)) {
      goto fail;
    }
  }
  return items;
fail:
  cJSON_Delete(items);
  return NULL;
}

static cJSON *describe_field(const SettingDescriptor *field) {
  cJSON *meta = cJSON_CreateObject();
  if (!meta) {
    return NULL;
  }
  bool numeric = field->read_number || field->read_record || field->default_board;
  const char *type = "integer";
  if (field->max_bytes) {
    type = "string";
  }
  else if (field->paired_boards) {
    type = "list";
  }
  else if (numeric && !field->options) {
    type = "range";
  }
  if (!cJSON_AddStringToObject(meta, "key", field->key) || !cJSON_AddStringToObject(meta, "label", field->label) ||
      !cJSON_AddStringToObject(meta, "group", field->group) ||
      !cJSON_AddStringToObject(meta, "description", field->description) ||
      !cJSON_AddStringToObject(meta, "type", type) || !cJSON_AddBoolToObject(meta, "readOnly", field->read_only)) {
    goto fail;
  }
  if (field->max_bytes) {
    if (!cJSON_AddNumberToObject(meta, "maxBytes", (double)field->max_bytes) ||
        !cJSON_AddBoolToObject(meta, "secret", field->secret)) {
      goto fail;
    }
  }
  else if (field->paired_boards) {
    cJSON *items = describe_board_fields();
    if (!items) {
      goto fail;
    }
    if (!cJSON_AddItemToObject(meta, "items", items)) {
      cJSON_Delete(items);
      goto fail;
    }
    if (!cJSON_AddNumberToObject(meta, "maxItems", MAX_PAIRED_DEVICES)) {
      goto fail;
    }
  }
  else if (numeric && !field->options) {
    if (!cJSON_AddNumberToObject(meta, "min", (double)field->minimum) ||
        !cJSON_AddNumberToObject(meta, "max", (double)field->maximum) ||
        !cJSON_AddBoolToObject(meta, "color", field->color)) {
      goto fail;
    }
  }
  else {
    cJSON *options = cJSON_AddArrayToObject(meta, "options");
    if (!options) {
      goto fail;
    }
    if (field->options) {
      SettingOptions choices = field->options();
      for (int i = 0; i < choices.count; ++i) {
        if (!add_option(options, i, choices.labels[i])) {
          goto fail;
        }
      }
    }
    else if (field->choices) {
      if (!add_option(options, INPUT_PIN_DISABLED, "Not used")) {
        goto fail;
      }
      uint64_t mask = field->choices();
      for (int pin = 0; pin < 64; ++pin) {
        if (!(mask & (1ULL << pin))) {
          continue;
        }
        char label[16];
        snprintf(label, sizeof(label), "GPIO %d", pin);
        if (!add_option(options, pin, label)) {
          goto fail;
        }
      }
    }
    else {
      if (!add_option(options, 0, "Active low") || !add_option(options, 1, "Active high")) {
        goto fail;
      }
    }
  }
  return meta;
fail:
  cJSON_Delete(meta);
  return NULL;
}

cJSON *settings_describe_json(void) {
  cJSON *reply = cJSON_CreateObject();
  if (!reply) {
    return NULL;
  }
  if (!cJSON_AddStringToObject(reply, "kind", "settings") ||
      !cJSON_AddNumberToObject(reply, "version", SETTINGS_VERSION)) {
    goto fail;
  }
  SettingsRecords live = {calibration_settings, imu_calibration};
  cJSON *metadata = cJSON_AddArrayToObject(reply, "fields");
  cJSON *values = cJSON_AddObjectToObject(reply, "values");
  if (!metadata || !values) {
    goto fail;
  }
  for (size_t i = 0; i < FIELD_COUNT; ++i) {
    const SettingDescriptor *field = &fields[i];
    if ((field->options == hbm_options && !display_supports_hbm()) ||
        (field->options == led_options && !led_is_supported())) {
      continue;
    }
    cJSON *meta = describe_field(field);
    if (!meta) {
      goto fail;
    }
    if (!cJSON_AddItemToArray(metadata, meta)) {
      cJSON_Delete(meta);
      goto fail;
    }
    if (field->max_bytes) {
      const char *value = field->read_string();
      if (!cJSON_AddStringToObject(values, field->key, value ? value : "")) {
        goto fail;
      }
    }
    else if (field->paired_boards) {
      cJSON *boards = cJSON_AddArrayToObject(values, field->key);
      if (!boards) {
        goto fail;
      }
      for (uint8_t b = 0; b < pairing_settings.device_count && b < MAX_PAIRED_DEVICES; ++b) {
        cJSON *board = board_json(&pairing_settings.devices[b]);
        if (!board || !cJSON_AddItemToArray(boards, board)) {
          cJSON_Delete(board);
          goto fail;
        }
      }
    }
    else {
      double number = field->read_number     ? (double)field->read_number()
                      : field->read_record   ? (double)field->read_record(&live)
                      : field->default_board ? (double)current_default_board()
                                             : (double)pin_value(field, &input_pin_settings);
      if (!cJSON_AddNumberToObject(values, field->key, number)) {
        goto fail;
      }
    }
  }
  char warning[192] = {0};
  input_pins_warnings(&input_pin_settings, warning, sizeof(warning));
  if (!cJSON_AddStringToObject(reply, "warning", warning)) {
    goto fail;
  }
  return reply;
fail:
  cJSON_Delete(reply);
  return NULL;
}

static int settings_error(char *error, size_t error_size, const char *message) {
  if (error && error_size) {
    snprintf(error, error_size, "%s", message);
  }
  return -1;
}

int settings_apply_json(const char *json, char *error_out, size_t error_size) {
  if (error_out && error_size) {
    error_out[0] = '\0';
  }
  if (!json) {
    return settings_error(error_out, error_size, "Expected a JSON object");
  }
  if (strlen(json) >= 2048) {
    return settings_error(error_out, error_size, "Settings payload too large");
  }
  // cJSON strings end at NUL, so reject an escaped NUL instead of truncating.
  bool in_string = false;
  int depth = 0;
  for (const char *p = json; *p; ++p) {
    if (*p == '\\' && p[1]) {
      if (strncmp(p + 1, "u0000", 5) == 0) {
        return settings_error(error_out, error_size, "NUL is not allowed in settings");
      }
      ++p;
    }
    else if (*p == '"') {
      in_string = !in_string;
    }
    else if (!in_string) {
      // Bound nesting (lists of flat objects) before cJSON's recursive parser.
      if (*p == '{' || *p == '[') {
        if (*p != (depth == 1 ? '[' : '{') || ++depth > 3) {
          return settings_error(error_out, error_size, "Unsupported settings nesting");
        }
      }
      else if ((*p == '}' || *p == ']') && depth > 0) {
        --depth;
      }
    }
  }
  // Reject trailing garbage.
  cJSON *patch = cJSON_ParseWithOpts(json, NULL, true);
  const char *error = NULL;
  InputPinSettings pending = input_pin_settings;
  DeviceSettings previous_preferences = device_settings;
  bool preferences_dirty = false;
  bool pins_dirty = false;
  bool seen[FIELD_COUNT] = {false};
  SettingsRecords staged = {calibration_settings, imu_calibration};
  PairedDevice boards[MAX_PAIRED_DEVICES];
  memcpy(boards, pairing_settings.devices, sizeof(boards));
  uint8_t board_count =
      pairing_settings.device_count < MAX_PAIRED_DEVICES ? pairing_settings.device_count : MAX_PAIRED_DEVICES;
  int default_board = current_default_board();
  bool pairing_seen = false, default_seen = false;
  if (!cJSON_IsObject(patch)) {
    error = "Expected a JSON object";
  }
  cJSON *value;
  if (!error) {
    cJSON_ArrayForEach(value, patch) {
      size_t i;
      for (i = 0; i < FIELD_COUNT; ++i) {
        if (!strcmp(value->string, fields[i].key)) {
          break;
        }
      }
      if (i == FIELD_COUNT) {
        error = "Unknown setting";
        break;
      }
      if (seen[i]) {
        error = "Duplicate setting";
        break;
      }
      seen[i] = true;
      const SettingDescriptor *field = &fields[i];
      if (field->paired_boards) {
        pairing_seen = true;
        if (!cJSON_IsArray(value) || cJSON_GetArraySize(value) > MAX_PAIRED_DEVICES) {
          error = "Invalid paired boards";
          break;
        }
        board_count = 0;
        const cJSON *item;
        cJSON_ArrayForEach(item, value) {
          if (!parse_board(item, &boards[board_count])) {
            error = "Invalid paired board";
            break;
          }
          for (uint8_t j = 0; j < board_count && !error; ++j) {
            if (!memcmp(boards[j].mac, boards[board_count].mac, PAIRED_MAC_BYTES)) {
              error = "Duplicate paired board";
            }
          }
          if (error) {
            break;
          }
          ++board_count;
        }
        if (error) {
          break;
        }
        continue;
      }
      if (field->max_bytes) {
        if (!cJSON_IsString(value) || strlen(value->valuestring) > field->max_bytes) {
          error = "Invalid string or UTF-8 byte length";
          break;
        }
      }
      else {
        if (!cJSON_IsNumber(value) || !allowed_number(field, value->valuedouble)) {
          error = "Invalid setting choice";
          break;
        }
        if (field->read_number) {
          continue;
        }
        if (field->default_board) {
          pairing_seen = default_seen = true;
          default_board = (int)value->valuedouble;
          continue;
        }
        if (field->write_record) {
          field->write_record(&staged, (int64_t)value->valuedouble);
          continue;
        }
        if (pin_value(field, &pending) != value->valueint) {
          pins_dirty = true;
        }
        *((uint8_t *)&pending + field->pin_offset) = (uint8_t)value->valueint;
      }
    }
  }
  if (!error && pairing_seen && !default_seen && default_board >= board_count) {
    default_board = board_count ? 0 : -1;
  }
  if (!error && default_board >= board_count) {
    error = "Default board is not in the paired list";
  }
  char pin_error[128] = {0};
  // Validate the whole patch before persisting or applying anything.
  if (!error && pins_dirty && input_pins_validate(&pending, pin_error, sizeof(pin_error)) != ESP_OK) {
    error = pin_error;
  }
  if (!error && pins_dirty && input_pins_apply(&pending, pin_error, sizeof(pin_error)) != ESP_OK) {
    error = pin_error[0] ? pin_error : "Failed to apply input pins";
  }
  // After pins, so restored calibration overrides a remap's reset.
  if (!error) {
    SettingsRecords applied = {calibration_settings, imu_calibration};
    bool calibration_changed = false, imu_changed = false;
    for (size_t i = 0; i < FIELD_COUNT; ++i) {
      if (seen[i] && fields[i].write_record && fields[i].read_record(&applied) != fields[i].read_record(&staged)) {
        fields[i].write_record(&applied, fields[i].read_record(&staged));
        if (fields[i].imu_record) {
          imu_changed = true;
        }
        else {
          calibration_changed = true;
        }
      }
    }
    if (calibration_changed) {
      if (settings_store_input_state(&input_pin_settings, &applied.calibration) != ESP_OK) {
        error = "Failed to persist settings; reload to check applied values";
      }
      else {
        calibration_settings = applied.calibration;
      }
    }
    if (!error && imu_changed) {
      settings_apply_imu_calibration(&applied.imu);
    }
  }
  if (!error && pairing_seen && pairing_changed(boards, board_count, default_board) &&
      settings_replace_pairing(boards, board_count, (int8_t)default_board) != ESP_OK) {
    error = "Failed to persist settings; reload to check applied values";
  }
  if (!error) {
    for (size_t i = 0; i < FIELD_COUNT; ++i) {
      if (seen[i] && fields[i].read_number) {
        uint32_t value = (uint32_t)cJSON_GetObjectItemCaseSensitive(patch, fields[i].key)->valuedouble;
        if (value != fields[i].read_number()) {
          if (nvs_write_int(fields[i].key, value) != ESP_OK) {
            error = "Failed to persist settings; reload to check applied values";
            break;
          }
          fields[i].apply_number(value);
          if (!strcmp(fields[i].key, "auto_off_time")) {
            reset_sleep_timer();
          }
          preferences_dirty = true;
        }
      }
      if (seen[i] && fields[i].max_bytes &&
          settings_save_string(fields[i].key, cJSON_GetObjectItemCaseSensitive(patch, fields[i].key)->valuestring) !=
              ESP_OK) {
        error = "Failed to persist settings; reload to check applied values";
        break;
      }
    }
  }
  // Refresh successful changes even when a later write fails.
  if (preferences_dirty) {
    display_refresh_device_settings(&previous_preferences);
  }
  cJSON_Delete(patch);
  if (error) {
    return settings_error(error_out, error_size, error);
  }
  return 0;
}
