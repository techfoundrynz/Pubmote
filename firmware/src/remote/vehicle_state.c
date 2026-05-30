#include "vehicle_state.h"
#include "esp_log.h"
#include "esp_task.h"
#include "haptic/haptic_patterns.h"
#include "remote/buzzer.h"
#include "remote/haptic.h"
#include "remote/led.h"
#include "remote/stats.h"

static const char *TAG = "PUBREMOTE-VEHICLE_STATE";
#define VEHICLE_STATE_LOOP_TIME_MS 100
#define VEHICLE_STATE_DEBUG 0

static TaskHandle_t monitor_task_handle = NULL;

DutyStatus get_duty_status(uint8_t duty) {
  if (duty >= DUTY_THRESHOLD_CRITICAL) {
    return DUTY_STATUS_CRITICAL;
  }
  else if (duty >= DUTY_THRESHOLD_WARNING) {
    return DUTY_STATUS_WARNING;
  }
  else if (duty >= DUTY_THRESHOLD_CAUTION) {
    return DUTY_STATUS_CAUTION;
  }
  else {
    return DUTY_STATUS_NONE;
  }
}

BuzzerToneFrequency get_buzzer_tone(DutyStatus status) {
  switch (status) {
  case DUTY_STATUS_CAUTION:
    // return NOTE_CAUTION;
    return 0;
  case DUTY_STATUS_WARNING:
    // return NOTE_WARNING;
    return 0;
  case DUTY_STATUS_CRITICAL:
    return NOTE_CRITICAL;
  default:
    return 0; // No alert tone
  }
}

DutyStatusColor get_duty_color(DutyStatus status) {
  switch (status) {
  case DUTY_STATUS_CAUTION:
    return DUTY_COLOR_CAUTION;
  case DUTY_STATUS_WARNING:
    return DUTY_COLOR_WARNING;
  case DUTY_STATUS_CRITICAL:
    return DUTY_COLOR_CRITICAL;
  default:
    return DUTY_COLOR_NONE; // Don't use this
  }
}

HapticFeedbackPattern get_haptic_pattern(DutyStatus status) {
  switch (status) {
  case DUTY_STATUS_CAUTION:
    return HAPTIC_SOFT_BUZZ;
  case DUTY_STATUS_WARNING:
    return HAPTIC_ALERT_750MS;
  case DUTY_STATUS_CRITICAL:
    return HAPTIC_ALERT_1000MS;
  default:
    return HAPTIC_NONE; // No alert pattern
  }
}

static void monitor_task(void *pvParameters) {
  DutyStatus last_duty_status = DUTY_STATUS_NONE;
#if VEHICLE_STATE_DEBUG
  int count = 0;
#endif
  while (1) {

#if VEHICLE_STATE_DEBUG
    remoteStats.dutyCycle = count;
    stats_update();
    count++;
    if (count >= 100) {
      count = 0;
    }
#endif

    DutyStatus current_duty_status = get_duty_status(remoteStats.dutyCycle);
    bool is_duty_alert = get_duty_status(remoteStats.dutyCycle) != DUTY_STATUS_NONE;
    if (current_duty_status != last_duty_status) {
      if (is_duty_alert) {
        ESP_LOGW(TAG, "Duty cycle alert: %d%%", remoteStats.dutyCycle);
        led_set_effect_solid(get_duty_color(current_duty_status));
        if (current_duty_status > last_duty_status) {
          // Duty cycle increased, alert with haptic and buzzer
          haptic_vibrate(get_haptic_pattern(current_duty_status));
          buzzer_set_tone(get_buzzer_tone(current_duty_status), 200 * current_duty_status);
        }
      }
      else {
        ESP_LOGD(TAG, "Duty cycle normal: %d%%", remoteStats.dutyCycle);
        led_set_effect_default();
        haptic_stop_vibration();
        buzzer_stop();
      }
      last_duty_status = current_duty_status;
      continue;
    }

    vTaskDelay(pdMS_TO_TICKS(VEHICLE_STATE_LOOP_TIME_MS));
  }
  vTaskDelete(NULL);
  monitor_task_handle = NULL;
}

void vehicle_monitor_init() {
  ESP_ERROR_CHECK(xTaskCreate(monitor_task, "monitor_task", 4096, NULL, 5, &monitor_task_handle) == pdPASS ? ESP_OK
                                                                                                           : ESP_FAIL);
  ESP_LOGI("VEHICLE_STATE", "Vehicle state monitor initialized");
}

void vehicle_monitor_deinit() {
  if (monitor_task_handle != NULL) {
    vTaskDelete(monitor_task_handle);
    monitor_task_handle = NULL;
  }
}
