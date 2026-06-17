#include "config.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "remote/adc.h"
#include "remote/buzzer.h"
#include "remote/connection.h"
#include "remote/console.h"
#include "remote/display.h"
#include "remote/espnow.h"
#include "remote/haptic.h"
#include "remote/i2c.h"
#include "remote/imu.h"
#include "remote/led.h"
#include "remote/peers.h"
#include "remote/powermanagement.h"
#include "remote/receiver.h"
#include "remote/remoteinputs.h"
#include "remote/settings.h"
#include "remote/startup.h"
#include "remote/stats.h"
#include "remote/test_mode.h"
#include "remote/time.h"
#include "remote/transmitter.h"
#include "remote/vehicle_state.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "PUBREMOTE-MAIN";

#define DEBUG_MEMORY 0

void app_main(void) {
  // Enable power for core peripherals
  acc1_power_set_level(1);
  gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
  // Core setup
  init_i2c();
  settings_init();
  init_adcs();
  buttons_init();
  buzzer_init();
  haptic_init();
  led_init();
  power_management_init();

  // Fire startup callbacks once boot is confirmed
  startup_cb();
// Enable accessories after callbacks
#ifdef ACC2_POWER_DEFAULT
  acc2_power_set_level(ACC2_POWER_DEFAULT);
#endif

  // Peripherals
  thumbstick_init();
  imu_init();
  display_init();
  vehicle_monitor_init();

  // Comms
  espnow_init();
  connection_init();
  receiver_init();
  transmitter_init();
  console_init();

  ESP_LOGI(TAG, "Boot complete");

#if TEST_MODE
  test_mode_init();
#endif

#if DEBUG_MEMORY
  while (1) {
    size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t min_heap = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "Free heap before update: %d bytes (min ever: %d bytes)", free_heap, min_heap);
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
#endif
}
