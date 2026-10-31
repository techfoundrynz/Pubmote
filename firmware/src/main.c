#include "config.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "games/game_store.h"
#include "remote/adc.h"
#include "remote/buzzer.h"
#include "remote/comms.h"
#include "remote/connection.h"
#include "remote/console.h"
#include "remote/display.h"
#include "remote/haptic.h"
#include "remote/i2c.h"
#include "remote/imu.h"
#include "remote/led.h"
#include "remote/orchestrator.h"
#include "remote/powermanagement.h"
#include "remote/radio_session.h"
#include "remote/receiver.h"
#include "remote/remoteinputs.h"
#include "remote/settings.h"
#include "remote/startup.h"
#include "remote/test_mode.h"
#include "remote/transmitter.h"
#include "remote/vehicle_state.h"
#include "remote/wake_button_sampler.h"
#include "ui/navigation.h"
#include "utilities/diagnostic_log.h"
#include "utilities/mem_debug.h"

static const char *TAG = "PUBREMOTE-MAIN";

// ─── Log level config ────────────────────────────────────────────────────────
// Edit here to silence or verbose any module. Call this before any init so the
// levels are applied from the first log line of each module.
// Levels: ESP_LOG_NONE / ERROR / WARN / INFO / DEBUG / VERBOSE
static void configure_log_levels(void) {
  // Third-party drivers — always silenced (noisy on I2C NACKs during sleep)
  esp_log_level_set("FT5x06", ESP_LOG_NONE);
  esp_log_level_set("CST816S", ESP_LOG_NONE);
  esp_log_level_set("i2c.master", ESP_LOG_NONE);
  esp_log_level_set("lcd_panel.io.i2c", ESP_LOG_NONE);

  // Module overrides — comment a line out to restore the global default
  esp_log_level_set("PUBREMOTE-IMU", ESP_LOG_ERROR);
  esp_log_level_set("PUBREMOTE-IMU_DRIVER_QMI8658", ESP_LOG_ERROR);
  esp_log_level_set("PUBREMOTE-PANEL", ESP_LOG_ERROR);
  esp_log_level_set("PUBREMOTE-SLINT-DISPLAY", ESP_LOG_ERROR);
  esp_log_level_set("PUBREMOTE-DISPLAY-DRIVER", ESP_LOG_ERROR);
  esp_log_level_set("NimBLE", ESP_LOG_WARN);
}
// ─────────────────────────────────────────────────────────────────────────────

#if IMU_ENABLED
static void init_imu_task(void *finished) {
  imu_init();
  xSemaphoreGive((SemaphoreHandle_t)finished);
  vTaskDelete(NULL);
}
#endif

void app_main(void) {
  configure_log_levels();
  if (!diagnostic_log_init())
    ESP_LOGW(TAG, "Diagnostic capture unavailable (PSRAM or mutex allocation failed)");

  // Cover the boot window with the task watchdog: until power_management_task
  // exists nothing else could recover from an init step hanging forever
  ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

  // Release deep sleep pin holds before any peripheral init - held pads
  // ignore reconfiguration until the hold is lifted
  MEM_MARK("app_main entry");
  power_management_preinit();

  // The early charger probe needs powered, settled peripheral/I2C rails.
  acc1_power_set_level(1);
  TickType_t peripheral_power_started = xTaskGetTickCount();
  gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
  init_i2c();
  settings_init();
  wake_button_sampler_finish();
  buttons_init();
  xTaskDelayUntil(&peripheral_power_started, pdMS_TO_TICKS(50));
  power_management_check_early_wake();
  init_adcs();
  buzzer_init();
  // Rejected button wakes must not power/calibrate the vibration motor.
  if (!(esp_sleep_get_wakeup_causes() & BIT(ESP_SLEEP_WAKEUP_EXT1)) ||
      wake_button_sampler_result() == WAKE_SAMPLE_CONFIRMED)
    haptic_init();
  led_init();
  MEM_MARK("core peripherals");
  power_management_init();
  esp_task_wdt_reset();

  // Fire startup callbacks once boot is confirmed
  startup_cb();
  // Enable accessories after callbacks.
#ifdef ACC2_POWER_DEFAULT_LEVEL
  acc2_power_set_level(ACC2_POWER_DEFAULT_LEVEL);
#endif

  // Peripherals
  MEM_MARK("startup callbacks");
  thumbstick_init();
  MEM_MARK("thumbstick");
  game_store_mount(); // Never formats an existing filesystem on mount failure.
#if IMU_ENABLED
  // Overlap IMU setup with panel reset waits; join before starting the radio.
  StaticSemaphore_t imu_finished_storage;
  SemaphoreHandle_t imu_finished = xSemaphoreCreateBinaryStatic(&imu_finished_storage);
  bool imu_started = xTaskCreate(init_imu_task, "imu_init", 4096, imu_finished, 1, NULL) == pdPASS;
#endif
  display_init();
  MEM_MARK("display_init");
#if IMU_ENABLED
  if (imu_started) {
    ESP_ERROR_CHECK(xSemaphoreTake(imu_finished, pdMS_TO_TICKS(5000)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT);
  }
  else {
    imu_init();
  }
  vSemaphoreDelete(imu_finished);
#else
  imu_init();
#endif
  MEM_MARK("imu_init");
  vehicle_monitor_init();
  esp_task_wdt_reset();

  // Comms
  CommsType boot_comms_mode = settings_get_active_comms_mode();
  comms_select_driver(boot_comms_mode);
  MEM_MARK("comms driver select");
  ESP_ERROR_CHECK(radio_session_init());
  ESP_ERROR_CHECK(comms_init());
  MEM_MARK("comms_init");
  connection_init();
  receiver_init();
  transmitter_init();
  MEM_MARK("tx/rx init");
  console_init();

  MEM_MARK("console_init");

#if TEST_MODE
  test_mode_init();
#endif

  orchestrator_init();

  // Runtime tasks take over the watchdog before app_main returns.
  ESP_ERROR_CHECK(esp_task_wdt_delete(NULL));
  ESP_LOGI(TAG, "Boot complete");
  ui_navigation_finish_boot();
}
