#include "powermanagement.h"
#include "adc.h"
#include "buzzer.h"
#include "charge/charge_driver.h"
#include "config.h"
#include "connection.h"
#include "display.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "gpio_detection.h"
#include "haptic.h"
#include "imu.h"
#include "led.h"
#include "remote/settings_snapshot.h"
#include "remote/stats.h"
#include "remote/tones.h"
#include "remoteinputs.h"
#include "sleep_timer.h"
#include "utilities/number_utils.h"
#include <driver/ledc.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_err.h>
#include <esp_sleep.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <math.h>
#include <stdatomic.h>
#include <sys/time.h>
static const char *TAG = "PUBREMOTE-POWERMANAGEMENT";

#define INT_SETTLE_TIME_MS 200
#define ERROR_NOTE_DURATION 500

#ifdef PMU_INT
  #define PMU_INT_NOTE_DURATION 100
#endif

RTC_DATA_ATTR bool is_power_connected = false; // Store power state across deep sleep
static atomic_bool power_connected_snapshot = false;
static bool woke_for_charging = false;

bool power_management_is_power_connected(void) {
  return atomic_load(&power_connected_snapshot);
}

bool power_management_woke_for_charging(void) {
  return woke_for_charging;
}

static volatile bool shutdown_initiated = false; // Flag for triggering shutdown sequence

#ifdef PMU_INT
static QueueHandle_t pmu_evt_queue = NULL;

// Interrupt handler function (runs in IRAM)
static void IRAM_ATTR pmu_isr_handler(void *arg) {
  uint32_t gpio_num = (uint32_t)arg;
  xQueueSendFromISR(pmu_evt_queue, &gpio_num, NULL);
}
#endif

static void power_state_update() {
  RemotePowerState powerState = get_power_state();
  uint8_t percentage = battery_mv_to_percent(powerState.voltage);
  stats_publish_power(powerState.voltage, percentage, powerState.chargeState, powerState.current);
  ESP_LOGD(TAG, "Battery volts: %u %d", powerState.voltage, percentage);
  if (is_power_connected != powerState.isPowered) {
    ESP_LOGI(TAG, "Charger %s", powerState.isPowered ? "connected" : "disconnected");
  }
  is_power_connected = powerState.isPowered;
  atomic_store(&power_connected_snapshot, is_power_connected);
  stats_update();
}

static bool get_button_pressed() {
  const InputPinSettings pins = settings_get_pins();
  if (pins.btn1_gpio == INPUT_PIN_DISABLED) {
    return false;
  }
  return gpio_get_level((gpio_num_t)pins.btn1_gpio) == pins.btn1_active_level;
}

static bool check_button_press() {
  uint64_t pressStartTime = esp_timer_get_time();
  while (get_button_pressed()) { // Check if button is still pressed
    esp_task_wdt_reset();        // Boot-window watchdog (no-op if caller unsubscribed)
    if ((esp_timer_get_time() - pressStartTime) >= (CONFIG_BUTTON_LONG_PRESS_TIME_MS * 1000)) {
      ESP_LOGI(TAG, "Button has been pressed for 2 seconds.");
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(10)); // Delay to allow for time checking without busy waiting
  }
  return false;
}

static esp_err_t enable_wake() {
  const InputPinSettings pins = settings_get_pins();
  esp_err_t res = ESP_OK;

  if (pins.btn1_gpio != INPUT_PIN_DISABLED) {
    const gpio_num_t btn_pin = (gpio_num_t)pins.btn1_gpio;
    ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup(BIT64(btn_pin), pins.btn1_active_level ? ESP_EXT1_WAKEUP_ANY_HIGH
                                                                                        : ESP_EXT1_WAKEUP_ANY_LOW));
    if (rtc_gpio_is_valid_gpio(btn_pin)) {
      if (pins.btn1_active_level) {
        rtc_gpio_pulldown_en(btn_pin);
        rtc_gpio_pullup_dis(btn_pin);
      }
      else {
        rtc_gpio_pullup_en(btn_pin);
        rtc_gpio_pulldown_dis(btn_pin);
      }
    }
  }

#ifdef PMU_INT
  // A low interrupt at entry would immediately wake again. Only arm after
  // charger shutdown operations have finished and the interrupt has settled.
  if (gpio_get_level(PMU_INT) && rtc_gpio_is_valid_gpio(PMU_INT)) {
    rtc_gpio_pullup_en(PMU_INT);
    rtc_gpio_pulldown_dis(PMU_INT);
    res = esp_sleep_enable_ext0_wakeup(PMU_INT, 0);
    ESP_ERROR_CHECK(res);
    ESP_LOGI(TAG, "Charger wake armed on GPIO %d", PMU_INT);
  }
  else {
    ESP_LOGW(TAG, "Charger interrupt is active or not RTC capable; charger wake not armed");
  }
#endif

  return res;
}

static bool empty_long_press_hold() {
  return true;
}

static bool power_button_long_press_hold() {
  BoardState state = stats_snapshot().state;
  if (state == BOARD_STATE_RUNNING || state == BOARD_STATE_RUNNING_FLYWHEEL || state == BOARD_STATE_RUNNING_TILTBACK ||
      state == BOARD_STATE_RUNNING_UPSIDEDOWN || state == BOARD_STATE_RUNNING_WHEELSLIP) {
    ESP_LOGI(TAG, "Power button long press hold detected. Ignoring.");
    return false;
  }

  shutdown_initiated = true;
  return true;
}

static void bind_power_button() {
  register_primary_button_cb(BUTTON_EVENT_LONG_PRESS_HOLD, power_button_long_press_hold);
}

// Specifically bind with empty handler to mark event as handled
static void unbind_power_button() {
  register_primary_button_cb(BUTTON_EVENT_LONG_PRESS_HOLD, empty_long_press_hold);
}

static bool power_button_initial_release() {
  unregister_primary_button_cb(BUTTON_EVENT_UP);
  bind_power_button();
  return true;
}

#ifdef PMU_INT
static void await_pmu_int_reset() {
  // Wait for PMU_INT to go high
  int timeout = INT_SETTLE_TIME_MS;
  while (gpio_get_level(PMU_INT) == 0 && timeout > 0) {
    vTaskDelay(pdMS_TO_TICKS(10));
    timeout -= 10;
    ESP_LOGD(TAG, "Waiting for PMU_INT to go high...");
  }

  if (timeout <= 0) {
    ESP_LOGE(TAG, "PMU_INT did not go high within the expected time.");
  }
}
#endif

void acc1_power_set_level(bool enable) {
#ifdef ACC1_POWER
  static bool is_initialized = false;

  if (!is_initialized) {
    gpio_config_t io_conf = {.pin_bit_mask = (1ULL << ACC1_POWER),
                             .mode = GPIO_MODE_OUTPUT,
                             .pull_up_en = GPIO_PULLUP_DISABLE,
                             .pull_down_en = GPIO_PULLDOWN_DISABLE,
                             .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&io_conf);
    is_initialized = true;
  }

  gpio_set_level(ACC1_POWER, ACC1_POWER_ON_LEVEL ? enable : !enable);
#endif
}

// Sets the power level of ACC2 using PWM for brightness control
// Level should be between 0 (off) and 100 (full power)
void acc2_power_set_level(uint8_t level) {
#ifdef ACC2_POWER
  #define POWER_LEDC_CHANNEL LEDC_CHANNEL_3
  #define POWER_LEDC_TIMER LEDC_TIMER_3
  #define POWER_TIMER LEDC_TIMER_3
  #define POWER_RESOLUTION LEDC_TIMER_8_BIT
  #define POWER_MAX_DUTY ((1 << 8) - 1)

  static bool is_initialized = false;

  if (!is_initialized) {
    ledc_timer_config_t timer_conf = {.speed_mode = LEDC_LOW_SPEED_MODE,
                                      .timer_num = POWER_LEDC_TIMER,
                                      .duty_resolution = POWER_RESOLUTION,
                                      .freq_hz = 1000,
                                      .clk_cfg = LEDC_AUTO_CLK};
    ledc_timer_config(&timer_conf);

    ledc_channel_config_t channel_conf = {
        .gpio_num = ACC2_POWER,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = POWER_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = POWER_TIMER,
        .duty = 0, // Initially off
    };
    ledc_channel_config(&channel_conf);
    is_initialized = true;
  }

  uint8_t final_duty = level > 100 ? 100 : level;
  final_duty = (final_duty * POWER_MAX_DUTY) / 100;

  #if !ACC2_POWER_ON_LEVEL
  final_duty = POWER_MAX_DUTY - final_duty;
  #endif

  ledc_set_duty(LEDC_LOW_SPEED_MODE, POWER_LEDC_CHANNEL, final_duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, POWER_LEDC_CHANNEL);
#endif
}

void enter_sleep() {
  shutdown_initiated = true;
}

// Drive a pin to a defined level and latch it so it can't float once the
// digital power domain shuts down in deep sleep. gpio_config reclaims pins
// still routed to a peripheral (LEDC/RMT) and disables pulls so no resistor
// fights the driven level through sleep.
static void hold_pin_during_sleep(gpio_num_t pin, uint32_t level) {
  gpio_set_level(pin, level);
  gpio_config_t cfg = {
      .pin_bit_mask = BIT64(pin),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&cfg);
  gpio_set_level(pin, level);
  gpio_hold_en(pin);
}

// Latch every external-facing control pin at its off level for deep sleep.
// Without this the pads go high-impedance and enable/data lines float, which
// can leave downstream parts (panel rail, haptic driver, LED strip, buzzer
// transistor) partially energized.
static void configure_sleep_pins() {
#ifdef DISP_BL
  // Panel enable / backlight (LCD_EN on AMOLED boards): only ever driven high
  // by the display driver, so force it off for sleep
  #if defined(DISP_BL_HIGH_LEVEL) && !DISP_BL_HIGH_LEVEL
  hold_pin_during_sleep(DISP_BL, 1);
  #else
  hold_pin_during_sleep(DISP_BL, 0);
  #endif
#endif
#ifdef HAPTIC_EN
  hold_pin_during_sleep(HAPTIC_EN, 0);
#endif
#ifdef LED_DATA
  hold_pin_during_sleep(LED_DATA, 0);
#endif
#ifdef BUZZER_PWM
  hold_pin_during_sleep(BUZZER_PWM, BUZZER_LEVEL ? 0 : 1);
#endif
#ifdef ACC1_POWER
  hold_pin_during_sleep(ACC1_POWER, ACC1_POWER_ON_LEVEL ? 0 : 1);
#endif
#ifdef ACC2_POWER
  hold_pin_during_sleep(ACC2_POWER, ACC2_POWER_ON_LEVEL ? 0 : 1);
#endif
  // Required for holds on non-RTC pads to survive deep sleep
  gpio_deep_sleep_hold_en();
}

// Release the pin holds from the previous deep sleep so drivers can
// reconfigure their pins. Must run before any peripheral init.
void power_management_preinit() {
#ifdef PMU_INT
  // EXT0 leaves its pad in RTC mode after wake. Restore it before reading
  // the interrupt, including early boot paths that return straight to sleep.
  rtc_gpio_deinit(PMU_INT);
  gpio_config_t pmu_input = {.pin_bit_mask = BIT64(PMU_INT),
                             .mode = GPIO_MODE_INPUT,
                             .pull_up_en = GPIO_PULLUP_ENABLE,
                             .pull_down_en = GPIO_PULLDOWN_DISABLE,
                             .intr_type = GPIO_INTR_DISABLE};
  ESP_ERROR_CHECK(gpio_config(&pmu_input));
#endif
  gpio_deep_sleep_hold_dis();
#ifdef DISP_BL
  gpio_hold_dis(DISP_BL);
#endif
#ifdef HAPTIC_EN
  gpio_hold_dis(HAPTIC_EN);
#endif
#ifdef LED_DATA
  gpio_hold_dis(LED_DATA);
#endif
#ifdef BUZZER_PWM
  gpio_hold_dis(BUZZER_PWM);
#endif
#ifdef ACC1_POWER
  gpio_hold_dis(ACC1_POWER);
#endif
#ifdef ACC2_POWER
  gpio_hold_dis(ACC2_POWER);
#endif
}

static void enter_sleep_internal() {
  // The whole sleep-entry sequence takes a few seconds - restart the watchdog
  // window so it can't fire mid-shutdown (no-op if caller unsubscribed)
  esp_task_wdt_reset();

  // Disable some things so they don't run during wake check
  connection_update_state(CONNECTION_STATE_DISCONNECTED);
  unbind_power_button();
  haptic_vibrate(HAPTIC_ALERT_750MS);

  // Turn off things
  display_off();
  led_set_effect_none();

  // wait for button release. Feed the watchdog while waiting - the user can
  // legitimately hold the button longer than the WDT timeout
  while (get_button_pressed()) {
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI(TAG, "Waiting for button release before sleep...");
  }
  vTaskDelay(pdMS_TO_TICKS(1000)); // Delay to allow effects to finish
  esp_task_wdt_reset();

  // Each of these waits on a task to exit or talks to an I2C device, and an unresponsive bus
  // costs the full transfer timeout per call - feed between them or shutdown trips the watchdog.
  imu_deinit();
  esp_task_wdt_reset();
  haptic_deinit(); // Stops the DRV2605 and drives HAPTIC_EN low
  esp_task_wdt_reset();
  led_deinit(); // Releases the RMT channel so LED_DATA can be latched low
  buzzer_deinit();
  esp_task_wdt_reset();

  acc1_power_set_level(0);
  acc2_power_set_level(0);

  esp_task_wdt_reset();

#ifdef PMU_INT
  power_state_update();
  esp_task_wdt_reset();
#endif

  // Must come after the last power_state_update - it turns off the PMU ADC
  charge_driver_deinit();
#ifdef PMU_INT
  await_pmu_int_reset();
#endif
  ESP_ERROR_CHECK(enable_wake());

  configure_sleep_pins();

  ESP_LOGI(TAG, "Entering deep sleep mode");
  // RTC peripherals stay on so the wake button's internal pull survives sleep
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  esp_deep_sleep_start(); // No code executes after esp_deep_sleep_start()
}

static void sleep_timer_expired(void) {
  shutdown_initiated = true;
}

void power_management_task(void *pvParameters) {
#define POWER_MANAGEMENT_MAX_DELAY 1000 * 1000 // 1 second in microseconds
// Force-shutdown escape hatch: raw GPIO poll, independent of the button
// driver / esp_timer / UI, so a wedged firmware can always be powered off
#define FORCE_SLEEP_HOLD_US (10 * 1000 * 1000)
  static int64_t last_time = 0;
  int64_t force_sleep_hold_start = 0;

  // Subscribe to the task watchdog: if this task ever hangs, nothing could
  // act on the shutdown flag and the remote could not be turned off - panic
  // and reboot instead
  ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

  while (1) {
    esp_task_wdt_reset();

    if (shutdown_initiated) {
      ESP_LOGI(TAG, "Shutdown initiated. Entering sleep.");
      enter_sleep_internal();
    }

    // If the button driver or UI is wedged, the normal long-press path never
    // sets the shutdown flag - a sustained raw hold forces shutdown anyway
    if (get_button_pressed()) {
      if (force_sleep_hold_start == 0) {
        force_sleep_hold_start = esp_timer_get_time();
      }
      else if (esp_timer_get_time() - force_sleep_hold_start > FORCE_SLEEP_HOLD_US) {
        ESP_LOGW(TAG, "Button held for %d s. Forcing shutdown.", (int)(FORCE_SLEEP_HOLD_US / 1000000));
        enter_sleep_internal();
      }
    }
    else {
      force_sleep_hold_start = 0;
    }

#ifdef PMU_INT
    // Handle interrupts so we can react to power state changes almost immediately
    uint32_t io_num;
    while (xQueueReceive(pmu_evt_queue, &io_num, 0) == pdTRUE) {
      if (io_num == PMU_INT) {
        ESP_LOGD(TAG, "PMU interrupt received on GPIO %lu", io_num);
        bool last_power_connected = is_power_connected;
        power_state_update();

        last_time = esp_timer_get_time(); // Update last time in milliseconds

        if (is_power_connected != last_power_connected) {
          buzzer_set_tone(is_power_connected ? NOTE_SUCCESS : NOTE_ERROR, 300);
        }
      }
    }
#endif

    int64_t current_time = esp_timer_get_time();
    if (current_time - last_time > POWER_MANAGEMENT_MAX_DELAY) {
      power_state_update();
      last_time = current_time;
    }

    if (stats_snapshot().remoteBatteryVoltage < MIN_BATTERY_VOLTAGE && !is_power_connected) {
      ESP_LOGW(TAG, "Battery voltage too low: %d mV", stats_snapshot().remoteBatteryVoltage);
      buzzer_set_tone(NOTE_ERROR, ERROR_NOTE_DURATION);
      vTaskDelay(pdMS_TO_TICKS(ERROR_NOTE_DURATION)); // Allow time for the note to play

      // If battery is too low, enter sleep immediately
      enter_protection_mode();
    }

    // Todo - Check battery voltage and enter sleep if too low
    // if (stats_snapshot().remoteBatteryVoltage <= MIN_BATTERY_VOLTAGE && !is_power_connected) {
    //   ESP_LOGW(TAG, "Battery voltage too low: %d mV", stats_snapshot().remoteBatteryVoltage);
    //   play_note(NOTE_ERROR, 1000);
    //   // If battery is too low, enter sleep immediately
    //   enter_sleep_internal();
    // }

    vTaskDelay(pdMS_TO_TICKS(100));
  }

  ESP_LOGI(TAG, "Power management task ended");
  // terminate self
  vTaskDelete(NULL);
}

#ifdef PMU_INT
static bool check_pmu_should_wake(bool last_powered) {
  await_pmu_int_reset();
  power_state_update();
  if (is_power_connected && !last_powered) {
    woke_for_charging = true;
    buzzer_set_tone(NOTE_SUCCESS, PMU_INT_NOTE_DURATION);
    // Power was connected after last sleep - continue to normal operation
  }
  else {
    if (!is_power_connected && last_powered) {
      buzzer_set_tone(NOTE_ERROR, PMU_INT_NOTE_DURATION);
      vTaskDelay(pdMS_TO_TICKS(PMU_INT_NOTE_DURATION)); // Allow time for the note to play
    }
    enter_sleep_internal();
    return false;
  }
  return true;
}
#endif

void power_management_init() {
  // Workaround for ESP-IDF crash when entering deep sleep.
  // The timekeeping synchronization function (esp_sync_timekeeping_timers) called
  // by esp_deep_sleep_start() accesses gettimeofday(), which requires s_time_lock
  // and s_boot_time_lock. If these locks are not yet initialized, they will be
  // lazily initialized during deep sleep entry. However, because the RTOS scheduler
  // is suspended during deep sleep entry, xQueueCreateMutex (called by lock_init_generic)
  // aborts. Initializing the locks now (when scheduler is running) avoids the crash.
  struct timeval tv_init;
  gettimeofday(&tv_init, NULL);

  bool power_was_connected = is_power_connected;
  ESP_ERROR_CHECK(charge_driver_init());
  sleep_timer_init(sleep_timer_expired);
  vTaskDelay(pdMS_TO_TICKS(50)); // Allow time for peripherals to initialize
  const uint32_t wakeup_causes = esp_sleep_get_wakeup_causes();
  uint64_t wakeup_pin_mask = esp_sleep_get_ext1_wakeup_status();
  power_state_update();

  ESP_LOGI(TAG, "Wake-up sources: 0x%lx", (unsigned long)wakeup_causes);
  const bool ext1_wake = (wakeup_causes & BIT(ESP_SLEEP_WAKEUP_EXT1)) != 0;
  // A deliberate button wake takes priority over a simultaneous PMU interrupt.
  if (ext1_wake && input_pins_button_enabled() &&
      (wakeup_pin_mask & BIT64(settings_get_pins().btn1_gpio))) {
    ESP_LOGI(TAG, "Woken up by power button.");
    if (!check_button_press()) {
      enter_sleep_internal();
      return;
    }
  }
#ifdef PMU_INT
  else if ((wakeup_causes & BIT(ESP_SLEEP_WAKEUP_EXT0)) || (ext1_wake && (wakeup_pin_mask & BIT64(PMU_INT)))) {
    ESP_LOGI(TAG, "Woken up by PMU interrupt.");
    if (!check_pmu_should_wake(power_was_connected)) {
      return;
    }
  }
#endif
  else if (ext1_wake) {
    enter_sleep_internal();
    return;
  }
  else {
    ESP_LOGI(TAG, "Not a deep sleep wakeup or other wake-up sources.");
  }
  // Bind empty long press hold so we can mark event as handled
  unbind_power_button();

  reset_sleep_timer();
  if (get_button_pressed()) {
    // Enable the power button once released if it wasn't already
    register_primary_button_cb(BUTTON_EVENT_UP, power_button_initial_release);
  }
  else {
    power_button_initial_release();
  }

#ifdef PMU_INT
  gpio_config_t pmu_io_conf = {};
  pmu_io_conf.intr_type = GPIO_INTR_NEGEDGE;
  pmu_io_conf.mode = GPIO_MODE_INPUT;
  pmu_io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
  pmu_io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  pmu_io_conf.pin_bit_mask = BIT64(PMU_INT);
  gpio_config(&pmu_io_conf);
  pmu_evt_queue = xQueueCreate(1, sizeof(uint32_t));

  gpio_isr_handler_add(PMU_INT, pmu_isr_handler, (void *)PMU_INT);
#endif

  ESP_ERROR_CHECK(xTaskCreate(power_management_task, "power_management_task", 3 * 1024, NULL, 2, NULL) == pdPASS
                      ? ESP_OK
                      : ESP_FAIL);
}
