#include "display_driver_st7789.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "hal/ledc_types.h"
#include <esp_err.h>
#include <esp_lcd_types.h>
#include <esp_log.h>

static const char *TAG = "PUBREMOTE-ST7789";

esp_err_t st7789_test_display_communication(esp_lcd_panel_io_handle_t io_handle) {
  ESP_LOGI(TAG, "Testing ST7789 communication");
  return ESP_OK;
}

esp_err_t st7789_display_driver_preinit() {
  ESP_LOGI(TAG, "Preinit ST7789 display driver");

  // Configure PWM channel for backlight
  ledc_timer_config_t timer_config = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .timer_num = LEDC_TIMER_0,
      .duty_resolution = LEDC_TIMER_8_BIT, // Resolution up to 256
      .freq_hz = 1000,                     // 1 kHz frequency
  };
  ledc_timer_config(&timer_config);

  ledc_channel_config_t channel_config = {
      .gpio_num = DISP_BL,
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .channel = LEDC_CHANNEL_0,
      .timer_sel = LEDC_TIMER_0,
  };
  ledc_channel_config(&channel_config);

  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);

  return ESP_OK;
}

esp_err_t st7789_set_display_brightness(esp_lcd_panel_io_handle_t io_handle, uint8_t brightness) {
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, brightness);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
  return ESP_OK;
}