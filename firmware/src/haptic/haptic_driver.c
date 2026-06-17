#include "haptic_driver.h"
#include "esp_log.h"
#if HAPTIC_DRV2605
  #include "haptic/drv2605/haptic_driver_drv2605.hpp"
#endif

static const char *TAG = "PUBREMOTE-HAPTIC";

esp_err_t haptic_driver_init() {
#if HAPTIC_DRV2605
  // Initialize the DRV2605 haptic driver
  ESP_LOGI(TAG, "Initializing DRV2605 haptic driver");
  return drv2605_haptic_driver_init();
#elif HAPTIC_PWM
  // Initialize the PWM haptic driver
  ESP_LOGI(TAG, "Initializing PWM haptic driver");
  return pwm_haptic_driver_init();
#else
  ESP_LOGE(TAG, "No haptic driver defined");
  return ESP_ERR_NOT_SUPPORTED;
#endif
}

void haptic_driver_play_vibration(HapticFeedbackPattern pattern) {
#if HAPTIC_DRV2605
  // Play vibration using the DRV2605 driver
  ESP_LOGI(TAG, "Playing vibration pattern: %d", pattern);
  drv2605_haptic_play_vibration(pattern);
#endif
}

void haptic_driver_stop_vibration() {
#if HAPTIC_DRV2605
  // Stop vibration using the DRV2605 driver
  ESP_LOGI(TAG, "Stopping vibration");
  drv2605_haptic_stop();
#endif
}

void haptic_driver_deinit() {
#if HAPTIC_DRV2605
  // Deinitialize the DRV2605 haptic driver
  ESP_LOGI(TAG, "Deinitializing DRV2605 haptic driver");
  drv2605_haptic_driver_deinit();
#endif
}