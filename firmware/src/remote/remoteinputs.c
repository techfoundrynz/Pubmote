#include "remoteinputs.h"
#include "adc.h"
#include "config.h"
#include "driver/rtc_io.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_private/esp_gpio_reserve.h"
#include "esp_sleep.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "iot_button.h"
#include "powermanagement.h"
#include "remote/display.h"
#include "remote/haptic.h"
#include "rom/gpio.h"
#include "settings.h"
#include "time.h"
#include <button_gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>

static const char *TAG = "PUBREMOTE-REMOTEINPUTS";

#ifndef JOYSTICK_BUTTON_LEVEL
  #error "JOYSTICK_BUTTON_LEVEL must be defined"
#endif

RemoteData remote_data;
JoystickData joystick_data;
static button_handle_t gpio_btn_handle = NULL;

static TaskHandle_t thumbstick_task_handle = NULL;
// should_run requests a stop; running is cleared by the task as it exits
static volatile bool thumbstick_should_run = false;
static volatile bool thumbstick_running = false;

bool input_pins_x_enabled() {
  return input_pin_settings.js_x_gpio > INPUT_PIN_DISABLED;
}

bool input_pins_y_enabled() {
  return input_pin_settings.js_y_gpio > INPUT_PIN_DISABLED;
}

bool input_pins_joystick_enabled() {
  return input_pins_x_enabled() || input_pins_y_enabled();
}

bool input_pins_button_enabled() {
  return input_pin_settings.btn1_gpio > INPUT_PIN_DISABLED;
}

// Release a pad an input no longer uses. RTC pulls are cleared explicitly
// because enable_wake() sets them where gpio_reset_pin can't reach.
static void release_pin(int8_t gpio) {
  if (gpio <= INPUT_PIN_DISABLED) {
    return;
  }

  if (rtc_gpio_is_valid_gpio((gpio_num_t)gpio)) {
    rtc_gpio_pullup_dis((gpio_num_t)gpio);
    rtc_gpio_pulldown_dis((gpio_num_t)gpio);
  }

  gpio_reset_pin((gpio_num_t)gpio);
}

static bool resolve_axis(int8_t gpio, adc_oneshot_unit_handle_t *out_handle, adc_channel_t *out_channel) {
  if (gpio <= INPUT_PIN_DISABLED) {
    return false;
  }

  adc_unit_t unit;
  adc_channel_t channel;
  esp_err_t err = adc_oneshot_io_to_channel(gpio, &unit, &channel);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "GPIO %d is not an ADC input", gpio);
    return false;
  }

  adc_oneshot_unit_handle_t handle = (unit == ADC_UNIT_1) ? adc1_handle : adc2_handle;
  if (handle == NULL) {
    ESP_LOGE(TAG, "ADC unit for GPIO %d not initialized", gpio);
    return false;
  }

  *out_handle = handle;
  *out_channel = channel;
  return true;
}

float convert_adc_to_axis(int adc_value, int min_val, int mid_val, int max_val, int deadband, float expo, bool invert) {
  float axis = 0;

  int mid_val_lower = mid_val - deadband;
  int mid_val_upper = mid_val + deadband;

  if (adc_value > mid_val_lower && adc_value < mid_val_upper) {
    // Within deadband
    return 0;
  }

  // Apply across adjusted mid vals so we get smooth ramping outside of deadband
  if (adc_value > mid_val) {
    axis = (float)(adc_value - mid_val_upper) / (max_val - mid_val_upper);
  }
  else {
    axis = (float)(adc_value - mid_val_lower) / (mid_val_lower - min_val);
  }

  // Apply expo
  if (expo > 1) {
    bool negative = axis < 0;
    axis = pow(axis, expo);
    if (negative) {
      axis = -axis;
    }
  }

  // clamp between -1 and 1
  if (axis > 1) {
    axis = 1;
  }
  else if (axis < -1) {
    axis = -1;
  }

  // Round to 2 decimal places
  axis = roundf(axis * 100) / 100;

  return invert ? -axis : axis;
}

static void thumbstick_task(void *pvParameters) {
  // Read once: input_pins_apply() restarts this task to change the assignment
  adc_oneshot_unit_handle_t x_adc_handle = NULL;
  adc_oneshot_unit_handle_t y_adc_handle = NULL;
  adc_channel_t x_channel = ADC_CHANNEL_0;
  adc_channel_t y_channel = ADC_CHANNEL_0;
  bool x_enabled = resolve_axis(input_pin_settings.js_x_gpio, &x_adc_handle, &x_channel);
  bool y_enabled = resolve_axis(input_pin_settings.js_y_gpio, &y_adc_handle, &y_channel);

  if (x_enabled && adc_oneshot_config_channel(x_adc_handle, x_channel, &adc_channel_config) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to configure X axis channel");
    x_enabled = false;
  }

  if (y_enabled && adc_oneshot_config_channel(y_adc_handle, y_channel, &adc_channel_config) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to configure Y axis channel");
    y_enabled = false;
  }

  // Subscribe to the task watchdog: a hung input task then panics and reboots
  // the remote (recoverable) instead of silently dropping control input
  ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

  while (thumbstick_should_run) {
    esp_task_wdt_reset();
    uint64_t newTime = get_current_time_ms();
    bool trigger_sleep_disrupt = false;
    int16_t deadband = calibration_settings.deadband;
    int16_t y_center = calibration_settings.y_center;
    int16_t y_max = calibration_settings.y_max;
    int16_t y_min = calibration_settings.y_min;
    int16_t x_center = calibration_settings.x_center;
    int16_t x_max = calibration_settings.x_max;
    int16_t x_min = calibration_settings.x_min;
    float expo = calibration_settings.expo;
    bool invert_x = calibration_settings.invert_x;
    bool invert_y = calibration_settings.invert_y;
    esp_err_t read_err;

    if (x_enabled) {
      int x_value;
      read_err = adc_oneshot_read(x_adc_handle, x_channel, &x_value);

      if (read_err == ESP_OK) {
        joystick_data.x = x_value;
        float new_x = convert_adc_to_axis(x_value, x_min, x_center, x_max, deadband, expo, invert_x);
        float curr_x = remote_data.js_x;

        if (new_x != curr_x) {
          remote_data.js_x = new_x;
          trigger_sleep_disrupt = true;
        }
      }
      else {
        ESP_LOGE(TAG, "Error reading X axis: %d", read_err);
      }
    }

    if (y_enabled) {
      int y_value;
      read_err = adc_oneshot_read(y_adc_handle, y_channel, &y_value);

      if (read_err == ESP_OK) {

        joystick_data.y = y_value;
        float new_y = convert_adc_to_axis(y_value, y_min, y_center, y_max, deadband, expo, invert_y);
        float curr_y = remote_data.js_y;

        if (new_y != curr_y) {
          remote_data.js_y = new_y;
          trigger_sleep_disrupt = true;
        }
      }
      else {
        ESP_LOGE(TAG, "Error reading Y axis: %d", read_err);
      }
    }

    if (trigger_sleep_disrupt) {
      reset_sleep_timer();
    }

    int64_t elapsed = get_current_time_ms() - newTime;
    if (elapsed >= 0 && elapsed < INPUT_RATE_MS) {
      vTaskDelay(pdMS_TO_TICKS(INPUT_RATE_MS - elapsed));
    }
    else {
      // Never skip the delay entirely - a sub-tick loop body would otherwise
      // busy-spin at priority 20 and starve lower-priority tasks on core 0
      vTaskDelay(1);
    }
  }

  ESP_LOGI(TAG, "Thumbstick task ended");
  esp_task_wdt_delete(NULL);
  thumbstick_running = false;
  vTaskDelete(NULL);
}

static void thumbstick_start() {
  if (!input_pins_joystick_enabled()) {
    ESP_LOGI(TAG, "No joystick axes configured - input task not started");
    return;
  }

  if (thumbstick_task_handle != NULL) {
    ESP_LOGW(TAG, "Thumbstick task already running");
    return;
  }

  // Set before creating - the new task outranks us and could exit first
  thumbstick_should_run = true;
  thumbstick_running = true;

  TaskHandle_t created = NULL;
  if (xTaskCreatePinnedToCore(thumbstick_task, "thumbstick_task", 3072, NULL, 20, &created, 0) != pdPASS) {
    thumbstick_should_run = false;
    thumbstick_running = false;
    ESP_LOGE(TAG, "Failed to create thumbstick task");
    return;
  }

  thumbstick_task_handle = created;
}

// Waits for the task to exit. Must not be called from the input task.
static void thumbstick_stop() {
  if (thumbstick_task_handle == NULL) {
    thumbstick_should_run = false;
    return;
  }

  thumbstick_should_run = false;
  for (int i = 0; i < 100 && thumbstick_running; i++) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  if (thumbstick_running) {
    // Wedged - unsubscribe for it so the watchdog can't fire on a dead task
    ESP_LOGE(TAG, "Thumbstick task did not exit - forcing deletion");
    esp_task_wdt_delete(thumbstick_task_handle);
    vTaskDelete(thumbstick_task_handle);
    thumbstick_running = false;
  }

  thumbstick_task_handle = NULL;

  remote_data.js_x = 0;
  remote_data.js_y = 0;
}

void thumbstick_init() {
  thumbstick_start();
}

static button_callback_t registered_single_click_cb = NULL;
static void button_single_click_cb(void *arg, void *usr_data) {
  ESP_LOGI(TAG, "BUTTON SINGLE CLICK");
  bool handled = false;

  if (registered_single_click_cb) {
    handled = registered_single_click_cb();
  }

  if (!handled) {
    reset_sleep_timer();
  }
}

static button_callback_t registered_button_down_cb = NULL;
static void button_down_cb(void *arg, void *usr_data) {
  ESP_LOGI(TAG, "BUTTON DOWN");
  bool handled = false;

  if (registered_button_down_cb) {
    handled = registered_button_down_cb();
  }

  haptic_vibrate(HAPTIC_SINGLE_CLICK);

  if (!handled) {
    remote_data.bt_c = 1;
  }
}

static button_callback_t registered_button_up_cb = NULL;
static void button_up_cb(void *arg, void *usr_data) {
  ESP_LOGI(TAG, "BUTTON UP");
  bool handled = false;
  if (registered_button_up_cb) {
    handled = registered_button_up_cb();
  }

  if (!handled) {
    remote_data.bt_c = 0;
  }
}

static button_callback_t registered_double_click_cb = NULL;
static void button_double_click_cb(void *arg, void *usr_data) {
  ESP_LOGI(TAG, "BUTTON DOUBLE CLICK");
  bool handled = false;

  if (registered_double_click_cb) {
    handled = registered_double_click_cb();
  }

  if (!handled) {
    reset_sleep_timer();
  }
}

static button_callback_t registered_long_press_hold_cb = NULL;
static void button_long_press_hold_cb(void *button_handle, void *usr_data) {
  ESP_LOGI(TAG, "BUTTON LONG PRESS HOLD");
  bool handled = false;

  if (registered_long_press_hold_cb) {
    handled = registered_long_press_hold_cb();
  }

  if (!handled) {
    reset_sleep_timer();
  }
}

void buttons_init() {
  if (!input_pins_button_enabled()) {
    ESP_LOGW(TAG, "No primary button configured - power off and wake are unavailable");
    return;
  }

  // create gpio button
  const button_config_t btn_cfg = {
      .long_press_time = CONFIG_BUTTON_LONG_PRESS_TIME_MS,
      .short_press_time = CONFIG_BUTTON_SHORT_PRESS_TIME_MS,
  };

  const button_gpio_config_t btn1_gpio_cfg = {
      .gpio_num = input_pin_settings.btn1_gpio,
      .active_level = input_pin_settings.btn1_active_level,
  };

  button_event_args_t btn_args = {
      .long_press =
          {
              .press_time = CONFIG_BUTTON_LONG_PRESS_TIME_MS,
          },
  };

  if (gpio_btn_handle != NULL) {
    ESP_LOGW(TAG, "Initialize called with existing button config. Please deinit before calling init");
    return;
  }

  iot_button_new_gpio_device(&btn_cfg, &btn1_gpio_cfg, &gpio_btn_handle);
  if (gpio_btn_handle == NULL) {
    ESP_LOGE(TAG, "Button create failed");
  }

  iot_button_register_cb(gpio_btn_handle, BUTTON_PRESS_DOWN, &btn_args, button_down_cb, NULL);
  iot_button_register_cb(gpio_btn_handle, BUTTON_PRESS_UP, &btn_args, button_up_cb, NULL);
  iot_button_register_cb(gpio_btn_handle, BUTTON_SINGLE_CLICK, &btn_args, button_single_click_cb, NULL);
  iot_button_register_cb(gpio_btn_handle, BUTTON_DOUBLE_CLICK, &btn_args, button_double_click_cb, NULL);
  iot_button_register_cb(gpio_btn_handle, BUTTON_LONG_PRESS_HOLD, &btn_args, button_long_press_hold_cb, NULL);
}

void buttons_deinit() {
  if (gpio_btn_handle) {
    iot_button_delete(gpio_btn_handle);
    gpio_btn_handle = NULL;
  }
}
void register_primary_button_cb(ButtonEvent event, button_callback_t cb) {
  switch (event) {
  case BUTTON_EVENT_DOWN:
    registered_button_down_cb = cb;
    break;
  case BUTTON_EVENT_UP:
    registered_button_up_cb = cb;
    break;
  case BUTTON_EVENT_PRESS:
    registered_single_click_cb = cb;
    break;
  case BUTTON_EVENT_DOUBLE_PRESS:
    registered_double_click_cb = cb;
    break;
  case BUTTON_EVENT_LONG_PRESS_HOLD:
    registered_long_press_hold_cb = cb;
    break;

  default:
    ESP_LOGW(TAG, "Unknown button event type");
    break;
  }
}

void unregister_primary_button_cb(ButtonEvent event) {
  switch (event) {
  case BUTTON_EVENT_DOWN:
    registered_button_down_cb = NULL;
    break;
  case BUTTON_EVENT_UP:
    registered_button_up_cb = NULL;
    break;
  case BUTTON_EVENT_PRESS:
    registered_single_click_cb = NULL;
    break;
  case BUTTON_EVENT_DOUBLE_PRESS:
    registered_double_click_cb = NULL;
    break;
  case BUTTON_EVENT_LONG_PRESS_HOLD:
    registered_long_press_hold_cb = NULL;
    break;
  default:
    ESP_LOGW(TAG, "Unknown button event type");
    break;
  }
}

// ─── Runtime input pin mapping ───────────────────────────────────────────────

uint64_t input_pins_adc_capable_mask() {
  static uint64_t cached_mask = 0;

  if (cached_mask == 0) {
    for (int io_num = 0; io_num < GPIO_NUM_MAX; io_num++) {
      if (!GPIO_IS_VALID_GPIO(io_num)) {
        continue;
      }
      adc_unit_t unit;
      adc_channel_t channel;
      if (adc_oneshot_io_to_channel(io_num, &unit, &channel) == ESP_OK) {
        cached_mask |= BIT64(io_num);
      }
    }
  }

  return cached_mask;
}

uint64_t input_pins_reserved_mask() {
  // Flash, PSRAM and driver-claimed pins; a 0 mask just reads the current one
  uint64_t mask = esp_gpio_reserve(0);

#if defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG) || defined(CONFIG_ESP_CONSOLE_USB_CDC)
  #if defined(CONFIG_IDF_TARGET_ESP32S3)
  // USB D-/D+ - the console rides on them
  mask |= BIT64(19) | BIT64(20);
  #endif
#endif

#ifdef I2C_SDA
  mask |= BIT64(I2C_SDA);
#endif
#ifdef I2C_SCL
  mask |= BIT64(I2C_SCL);
#endif
#ifdef DISP_BL
  mask |= BIT64(DISP_BL);
#endif
#ifdef DISP_MOSI
  mask |= BIT64(DISP_MOSI);
#endif
#ifdef DISP_CLK
  mask |= BIT64(DISP_CLK);
#endif
#ifdef DISP_CS
  mask |= BIT64(DISP_CS);
#endif
#ifdef DISP_DC
  mask |= BIT64(DISP_DC);
#endif
#ifdef DISP_RST
  mask |= BIT64(DISP_RST);
#endif
#ifdef DISP_SDIO0
  mask |= BIT64(DISP_SDIO0);
#endif
#ifdef DISP_SDIO1
  mask |= BIT64(DISP_SDIO1);
#endif
#ifdef DISP_SDIO2
  mask |= BIT64(DISP_SDIO2);
#endif
#ifdef DISP_SDIO3
  mask |= BIT64(DISP_SDIO3);
#endif
#ifdef TP_INT
  mask |= BIT64(TP_INT);
#endif
#if defined(TP_RST) && (TP_RST >= 0)
  mask |= BIT64(TP_RST);
#endif
#ifdef IMU_INT
  mask |= BIT64(IMU_INT);
#endif
#ifdef PMU_INT
  mask |= BIT64(PMU_INT);
#endif
#ifdef BUZZER_PWM
  mask |= BIT64(BUZZER_PWM);
#endif
#ifdef HAPTIC_EN
  mask |= BIT64(HAPTIC_EN);
#endif
#ifdef LED_DATA
  mask |= BIT64(LED_DATA);
#endif
#ifdef ACC1_POWER
  mask |= BIT64(ACC1_POWER);
#endif
#ifdef ACC2_POWER
  mask |= BIT64(ACC2_POWER);
#endif

#if defined(BAT_ADC) && (BAT_ADC >= 0)
  // Battery sense is an ADC1 channel - reserve the pin behind it
  int bat_io = INPUT_PIN_DISABLED;
  if (adc_oneshot_channel_to_io(ADC_UNIT_1, (adc_channel_t)BAT_ADC, &bat_io) == ESP_OK && bat_io >= 0) {
    mask |= BIT64(bat_io);
  }
#endif

  return mask;
}

uint64_t input_pins_assignable_mask() {
  uint64_t mask = 0;

  for (int io_num = 0; io_num < GPIO_NUM_MAX; io_num++) {
    if (GPIO_IS_VALID_GPIO(io_num)) {
      mask |= BIT64(io_num);
    }
  }

  return mask & ~input_pins_reserved_mask();
}

uint64_t input_pins_button_capable_mask() {
  uint64_t mask = 0;

  for (int io_num = 0; io_num < GPIO_NUM_MAX; io_num++) {
    if (GPIO_IS_VALID_GPIO(io_num) && rtc_gpio_is_valid_gpio((gpio_num_t)io_num)) {
      mask |= BIT64(io_num);
    }
  }

  return mask & input_pins_assignable_mask();
}

static bool pin_in_mask(int8_t gpio, uint64_t mask) {
  return gpio > INPUT_PIN_DISABLED && (mask & BIT64(gpio)) != 0;
}

esp_err_t input_pins_validate(const struct InputPinSettings *cfg, char *err, size_t err_len) {
  if (cfg == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

#define PIN_FAIL(...)                                                                                                  \
  do {                                                                                                                 \
    if (err != NULL && err_len > 0) {                                                                                   \
      snprintf(err, err_len, __VA_ARGS__);                                                                             \
    }                                                                                                                  \
    return ESP_ERR_INVALID_ARG;                                                                                        \
  } while (0)

  const int8_t pins[] = {cfg->js_x_gpio, cfg->js_y_gpio, cfg->btn1_gpio};
  const char *names[] = {"js_x_gpio", "js_y_gpio", "btn1_gpio"};
  const size_t pin_count = sizeof(pins) / sizeof(pins[0]);

  if (cfg->btn1_active_level > 1) {
    PIN_FAIL("btn1_level must be 0 or 1");
  }

  const uint64_t reserved = input_pins_reserved_mask();
  const uint64_t adc_capable = input_pins_adc_capable_mask();

  for (size_t i = 0; i < pin_count; i++) {
    if (pins[i] <= INPUT_PIN_DISABLED) {
      if (pins[i] < INPUT_PIN_DISABLED) {
        PIN_FAIL("%s: %d is not a valid pin (use -1 to disable)", names[i], pins[i]);
      }
      continue;
    }

    if (pins[i] >= GPIO_NUM_MAX || !GPIO_IS_VALID_GPIO(pins[i])) {
      PIN_FAIL("%s: GPIO %d does not exist on this chip", names[i], pins[i]);
    }

    if (pin_in_mask(pins[i], reserved)) {
      PIN_FAIL("%s: GPIO %d is already used by this board", names[i], pins[i]);
    }

    for (size_t j = i + 1; j < pin_count; j++) {
      if (pins[j] == pins[i]) {
        PIN_FAIL("GPIO %d cannot be both %s and %s", pins[i], names[i], names[j]);
      }
    }
  }

  if (cfg->js_x_gpio > INPUT_PIN_DISABLED && !pin_in_mask(cfg->js_x_gpio, adc_capable)) {
    PIN_FAIL("js_x_gpio: GPIO %d has no ADC channel - pick an analog capable pin", cfg->js_x_gpio);
  }

  if (cfg->js_y_gpio > INPUT_PIN_DISABLED && !pin_in_mask(cfg->js_y_gpio, adc_capable)) {
    PIN_FAIL("js_y_gpio: GPIO %d has no ADC channel - pick an analog capable pin", cfg->js_y_gpio);
  }

  // ext1 wake only works from an RTC pad, so a non-RTC button could turn the
  // remote off but never back on
  if (cfg->btn1_gpio > INPUT_PIN_DISABLED && !rtc_gpio_is_valid_gpio((gpio_num_t)cfg->btn1_gpio)) {
    PIN_FAIL("btn1_gpio: GPIO %d is not an RTC pin, so it could not wake the remote", cfg->btn1_gpio);
  }

#undef PIN_FAIL
  return ESP_OK;
}

static void append_warning(char *buf, size_t buf_len, size_t *offset, const char *fmt, ...) {
  if (buf == NULL || buf_len == 0 || *offset >= buf_len - 1) {
    return;
  }

  if (*offset > 0) {
    buf[(*offset)++] = ' ';
    buf[*offset] = '\0';
    if (*offset >= buf_len - 1) {
      return;
    }
  }

  va_list args;
  va_start(args, fmt);
  int written = vsnprintf(buf + *offset, buf_len - *offset, fmt, args);
  va_end(args);

  if (written > 0) {
    *offset += ((size_t)written < buf_len - *offset) ? (size_t)written : (buf_len - *offset - 1);
  }
}

static bool axis_uses_adc2(int8_t gpio) {
  if (gpio <= INPUT_PIN_DISABLED || !GPIO_IS_VALID_GPIO(gpio)) {
    return false;
  }
  adc_unit_t unit;
  adc_channel_t channel;
  return adc_oneshot_io_to_channel(gpio, &unit, &channel) == ESP_OK && unit == ADC_UNIT_2;
}

size_t input_pins_warnings(const struct InputPinSettings *cfg, char *warn, size_t warn_len) {
  if (cfg == NULL || warn == NULL || warn_len == 0) {
    return 0;
  }

  size_t offset = 0;
  warn[0] = '\0';

  if (cfg->btn1_gpio <= INPUT_PIN_DISABLED) {
    append_warning(warn, warn_len, &offset,
                   "No primary button: the remote cannot be powered off or woken from sleep by button.");
  }

  if (axis_uses_adc2(cfg->js_x_gpio)) {
    append_warning(warn, warn_len, &offset, "js_x GPIO %d uses ADC2, which can fail while Wi-Fi is active.",
                   cfg->js_x_gpio);
  }

  if (axis_uses_adc2(cfg->js_y_gpio)) {
    append_warning(warn, warn_len, &offset, "js_y GPIO %d uses ADC2, which can fail while Wi-Fi is active.",
                   cfg->js_y_gpio);
  }

  return offset;
}

static void release_unused_pin(int8_t gpio, const struct InputPinSettings *cfg) {
  if (gpio <= INPUT_PIN_DISABLED) {
    return;
  }
  if (gpio == cfg->js_x_gpio || gpio == cfg->js_y_gpio || gpio == cfg->btn1_gpio) {
    return;
  }

  ESP_LOGI(TAG, "Releasing GPIO %d", gpio);
  release_pin(gpio);
}

esp_err_t input_pins_apply(const struct InputPinSettings *cfg, char *err, size_t err_len) {
  esp_err_t res = input_pins_validate(cfg, err, err_len);
  if (res != ESP_OK) {
    return res;
  }

  const InputPinSettings previous = input_pin_settings;
  if (memcmp(&previous, cfg, sizeof(InputPinSettings)) == 0) {
    ESP_LOGI(TAG, "Input pins unchanged");
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Applying input pins: js_x=%d js_y=%d btn=%d (level %u)", cfg->js_x_gpio, cfg->js_y_gpio, cfg->btn1_gpio,
           cfg->btn1_active_level);

  thumbstick_stop();
  buttons_deinit();

  release_unused_pin(previous.js_x_gpio, cfg);
  release_unused_pin(previous.js_y_gpio, cfg);
  release_unused_pin(previous.btn1_gpio, cfg);

  input_pin_settings = *cfg;

  // A moved axis has a different resting range, so its calibration is stale
  reset_axis_calibration(previous.js_x_gpio != cfg->js_x_gpio, previous.js_y_gpio != cfg->js_y_gpio);
  save_input_pins();

  buttons_init();
  thumbstick_start();
  display_set_joystick_supported(input_pins_joystick_enabled());

  return ESP_OK;
}