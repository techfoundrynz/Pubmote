#include "haptic.h"
#include "config.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "haptic/haptic_driver.h"
#include "remote/startup.h"
#include <stdatomic.h>

static const char *TAG = "PUBREMOTE-HAPTIC";
#if HAPTIC_ENABLED
static atomic_bool initialized = false;
#endif

void haptic_vibrate(HapticFeedbackPattern pattern) {
#if HAPTIC_ENABLED
  if (!atomic_load(&initialized))
    return;
  ESP_LOGI(TAG, "Vibrating with pattern: %d", pattern);
  haptic_driver_play_vibration(pattern);
#endif
}

void haptic_stop_vibration() {
#if HAPTIC_ENABLED
  if (!atomic_load(&initialized))
    return;
  ESP_LOGI(TAG, "Stopping vibration");
  haptic_driver_stop_vibration();
#endif
}

static void haptic_play_startup_effect() {
#if HAPTIC_ENABLED
  haptic_vibrate(HAPTIC_ALERT_750MS);
  // Additional startup effects can be added here
#endif
}

void haptic_init() {
#if HAPTIC_ENABLED
  if (atomic_load(&initialized) || haptic_driver_init() != ESP_OK)
    return;
  atomic_store(&initialized, true);
  register_startup_cb(haptic_play_startup_effect);
#endif
}

void haptic_deinit() {
#if HAPTIC_ENABLED
  if (!atomic_exchange(&initialized, false))
    return;
  haptic_driver_stop_vibration();
  vTaskDelay(pdMS_TO_TICKS(10)); // Small delay to ensure stop command is processed
  haptic_driver_deinit();
#endif
}
