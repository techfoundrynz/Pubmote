#include "led.h"
#include "config.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "led_strip.h"
#include "nvs_flash.h"
#include "remote/startup.h"
#include "settings.h"
#include "stats.h"
#include "time.h"
#include <driver/ledc.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <led_strip_types.h>
#include <math.h>
#include <nvs.h>

static const char *TAG = "PUBREMOTE-LED";

static LedEffect current_effect = LED_EFFECT_NONE;
static TaskHandle_t led_task_handle = NULL;

#if LED_ENABLED
  #define LEDC_CHANNEL LEDC_CHANNEL_2
  #define LEDC_TIMER LEDC_TIMER_2
  #define LED_STRIP_RMT_RES_HZ (10 * 1000 * 1000)
static led_strip_handle_t led_strip = NULL;

static uint8_t brightness_level = 255; // Brightness setting for the LED strip
static uint8_t current_brightness = 0; // Effective brightness for the animation
static RGB rgb = {255, 255, 255};

  #define ANIMATION_DELAY_MS 10 // Delay for the LED animation task
  #define BRIGHTNESS_STEP 5     // Step size for brightness adjustment

static void configure_led(void) {
  // LED strip general initialization, according to your led board design
  led_strip_config_t strip_config = {
      .strip_gpio_num = LED_DATA, // The GPIO that connected to the LED strip's data line
      .max_leds = LED_COUNT,      // The number of LEDs in the strip,
      .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB, // Pixel format of your LED strip
      .led_model = LED_MODEL_WS2812,                               // LED strip model
      .flags.invert_out = false,                                   // whether to invert the output signal
  };

  // LED strip backend configuration: RMT
  led_strip_rmt_config_t rmt_config = {
      .clk_src = RMT_CLK_SRC_DEFAULT,        // different clock source can lead to different power consumption
      .resolution_hz = LED_STRIP_RMT_RES_HZ, // RMT counter clock frequency
      .flags.with_dma = false,               // DMA feature is available on ESP target like ESP32-S3
  };

  // LED Strip object handle
  ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
  ESP_LOGI(TAG, "Created LED strip object with RMT backend");
}

static RGB adjustBrightness(RGB originalColor, float brightnessScaling) {
  float gamma = 2.2; // Typical gamma value for WS2812 LEDs

  // Apply gamma correction to the scaling factor
  float correctedScaling = pow(brightnessScaling, gamma);
  RGB return_val;

  return_val.r = round(pow((originalColor.r / 255.0), gamma) * correctedScaling * 255.0);
  return_val.g = round(pow((originalColor.g / 255.0), gamma) * correctedScaling * 255.0);
  return_val.b = round(pow((originalColor.b / 255.0), gamma) * correctedScaling * 255.0);

  return return_val;
}

static void led_off() {
  esp_err_t err = ESP_OK;
  err = led_strip_clear(led_strip);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to clear LED strip: %s", esp_err_to_name(err));
    return;
  }

  err = led_strip_refresh(led_strip);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to refresh LED strip: %s", esp_err_to_name(err));
    return;
  }
}

static void apply_led_effect() {
  if (led_strip == NULL) {
    ESP_LOGE(TAG, "LED strip not initialized");
    return;
  }

  RGB new_col = adjustBrightness(rgb, current_brightness / 255.0);

  esp_err_t err = ESP_OK;

  for (int i = 0; i < LED_COUNT; i++) {
    esp_err_t new_err = led_strip_set_pixel(led_strip, i, new_col.r, new_col.g, new_col.b);
    if (new_err != ESP_OK) {
      err = new_err; // Capture the last error
    }
  }

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set LED color: %s", esp_err_to_name(err));
    return;
  }
  /* Refresh the strip to send data */
  led_strip_refresh(led_strip);
}

static RGB hex_to_rgb(uint32_t hex) {
  RGB color;
  color.r = (hex >> 16) & 0xFF;
  color.g = (hex >> 8) & 0xFF;
  color.b = hex & 0xFF;
  return color;
}

static void pulse_effect() {
  static bool increasing = true;

  if (increasing) {
    current_brightness += BRIGHTNESS_STEP;
  }
  else {
    current_brightness -= BRIGHTNESS_STEP;
  }

  if (current_brightness >= brightness_level) {
    current_brightness = brightness_level;
    increasing = false;
  }
  else if (current_brightness <= 0) {
    current_brightness = 0;
    increasing = true;
  }
  apply_led_effect();
  vTaskDelay(pdMS_TO_TICKS(ANIMATION_DELAY_MS));
}

static void solid_effect() {
  current_brightness = brightness_level;
  apply_led_effect();
  vTaskDelay(pdMS_TO_TICKS(ANIMATION_DELAY_MS));
}

static void rainbow_effect() {
  static int hue = 0;
  hue = (hue + 1) % 360; // Increment hue for rainbow effect

  // Convert hue to RGB
  float r, g, b;
  int i = hue / 60;
  float f = (hue / 60.0) - i;

  switch (i % 6) {
  case 0:
    r = 1, g = f, b = 0;
    break;
  case 1:
    r = f, g = 1, b = 0;
    break;
  case 2:
    r = 0, g = 1, b = f;
    break;
  case 3:
    r = 0, g = f, b = 1;
    break;
  case 4:
    r = f, g = 0, b = 1;
    break;
  case 5:
    r = 1, g = 0, b = f;
    break;
  default:
    r = g = b = 0;
    break;
  }

  rgb.r = (uint8_t)(r * brightness_level);
  rgb.g = (uint8_t)(g * brightness_level);
  rgb.b = (uint8_t)(b * brightness_level);
  apply_led_effect();
  vTaskDelay(pdMS_TO_TICKS(ANIMATION_DELAY_MS));
}

static void no_effect() {
  current_brightness = 0;
  apply_led_effect();
  vTaskDelay(pdMS_TO_TICKS(ANIMATION_DELAY_MS));
}

static esp_timer_handle_t led_startup_off_timer = NULL;

static void startup_effect_stop() {
  led_set_effect_none();

  if (led_startup_off_timer != NULL) {
    esp_timer_delete(led_startup_off_timer);
    led_startup_off_timer = NULL;
  }
}

static void play_startup_effect() {
  if (led_startup_off_timer != NULL) {
    esp_timer_delete(led_startup_off_timer);
    led_startup_off_timer = NULL;
  }

  // Timer configuration
  esp_timer_create_args_t timer_args = {.callback = startup_effect_stop,
                                        .arg = NULL,
                                        .dispatch_method = ESP_TIMER_TASK,
                                        .name = "led_startup_off",
                                        .skip_unhandled_events = false};

  // Create the timer
  esp_timer_create(&timer_args, &led_startup_off_timer);
  esp_timer_start_once(led_startup_off_timer, 3000 * 1000);

  led_set_effect_pulse(device_settings.theme_color);
}

static void led_task(void *pvParameters) {
  while (true) {
    if (current_effect == LED_EFFECT_PULSE) {
      pulse_effect();
      continue;
    }

    if (current_effect == LED_EFFECT_SOLID) {
      solid_effect();
      continue;
    }

    if (current_effect == LED_EFFECT_RAINBOW) {
      rainbow_effect();
      continue;
    }

    if (current_effect == LED_EFFECT_NONE) {
      no_effect();
      continue;
    }

    // Default off in case of an unknown effect
    ESP_LOGW(TAG, "Unknown LED effect: %d", current_effect);
    current_brightness = 0;
    apply_led_effect();
    vTaskDelay(pdMS_TO_TICKS(ANIMATION_DELAY_MS));
  }
  vTaskDelete(NULL);
}
#endif

void led_set_effect_solid(uint32_t color) {
#if LED_ENABLED
  rgb = hex_to_rgb(color);
  current_effect = LED_EFFECT_SOLID;
  current_brightness = brightness_level;
#endif
}

void led_set_effect_pulse(uint32_t color) {
#if LED_ENABLED
  rgb = hex_to_rgb(color);
  current_effect = LED_EFFECT_PULSE;
  current_brightness = 0;
#endif
}

void led_set_effect_rainbow() {
#if LED_ENABLED
  current_effect = LED_EFFECT_RAINBOW;
  current_brightness = brightness_level;
#endif
}

void led_set_effect_none() {
#if LED_ENABLED
  current_effect = LED_EFFECT_NONE;
  current_brightness = 0;
#endif
}

void led_init() {
#if LED_ENABLED
  ESP_LOGI(TAG, "Initializing LED strip");
  configure_led();
  xTaskCreate(led_task, "led_task", 2048, NULL, 2, NULL);
  register_startup_cb(play_startup_effect);
#endif
}

void led_deinit() {
#if LED_ENABLED
  if (led_task_handle != NULL) {
    vTaskDelete(led_task_handle);
    led_task_handle = NULL;
  }

  if (led_strip != NULL) {
    led_off();
    led_strip_del(led_strip);
    led_strip = NULL;
  }
  ESP_LOGI(TAG, "LED strip deinitialized");
#endif
}

void led_set_brightness(uint8_t brightness) {
#if LED_ENABLED
  brightness_level = brightness;
  apply_led_effect();
#endif
}