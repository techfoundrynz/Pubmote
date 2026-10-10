#include "vehicle_state.h"
#include "esp_log.h"
#include "esp_task.h"
#include "haptic/haptic_patterns.h"
#include "remote/buzzer.h"
#include "remote/haptic.h"
#include "remote/led.h"
#include "remote/stats.h"
#include "utilities/psram_task.h"

static const char *TAG = "PUBREMOTE-VEHICLE_STATE";
#define VEHICLE_STATE_LOOP_TIME_MS 100
#define VEHICLE_STATE_DEBUG 0

static TaskHandle_t monitor_task_handle = NULL;

UtilizationStatus get_utilization_status(uint8_t utilization) {
  if (utilization >= UTILIZATION_THRESHOLD_CRITICAL) {
    return UTILIZATION_STATUS_CRITICAL;
  }
  else if (utilization >= UTILIZATION_THRESHOLD_WARNING) {
    return UTILIZATION_STATUS_WARNING;
  }
  else if (utilization >= UTILIZATION_THRESHOLD_CAUTION) {
    return UTILIZATION_STATUS_CAUTION;
  }
  else {
    return UTILIZATION_STATUS_NONE;
  }
}

BuzzerToneFrequency get_buzzer_tone(UtilizationStatus status) {
  switch (status) {
  case UTILIZATION_STATUS_CAUTION:
    // return NOTE_CAUTION;
    return 0;
  case UTILIZATION_STATUS_WARNING:
    // return NOTE_WARNING;
    return 0;
  case UTILIZATION_STATUS_CRITICAL:
    return NOTE_CRITICAL;
  default:
    return 0; // No alert tone
  }
}

UtilizationStatusColor get_utilization_color(UtilizationStatus status) {
  switch (status) {
  case UTILIZATION_STATUS_CAUTION:
    return UTILIZATION_COLOR_CAUTION;
  case UTILIZATION_STATUS_WARNING:
    return UTILIZATION_COLOR_WARNING;
  case UTILIZATION_STATUS_CRITICAL:
    return UTILIZATION_COLOR_CRITICAL;
  default:
    return UTILIZATION_COLOR_NONE; // Don't use this
  }
}

HapticFeedbackPattern get_haptic_pattern(UtilizationStatus status) {
  switch (status) {
  case UTILIZATION_STATUS_CAUTION:
    return HAPTIC_SOFT_BUZZ;
  case UTILIZATION_STATUS_WARNING:
    return HAPTIC_ALERT_750MS;
  case UTILIZATION_STATUS_CRITICAL:
    return HAPTIC_ALERT_1000MS;
  default:
    return HAPTIC_NONE; // No alert pattern
  }
}

static void monitor_task(void *pvParameters) {
  UtilizationStatus last_utilization_status = UTILIZATION_STATUS_NONE;
#if VEHICLE_STATE_DEBUG
  int count = 0;
#endif
  while (1) {

#if VEHICLE_STATE_DEBUG
    stats_set_duty_cycle(count);
    stats_update();
    count++;
    if (count >= 100) {
      count = 0;
    }
#endif

    const RemoteStats telemetry = stats_snapshot();
    const uint8_t utilization = stats_utilization(&telemetry);
    UtilizationStatus current_utilization_status = get_utilization_status(utilization);
    bool is_utilization_alert = current_utilization_status != UTILIZATION_STATUS_NONE;
    if (current_utilization_status != last_utilization_status) {
      if (is_utilization_alert) {
        ESP_LOGW(TAG, "Utilization alert: %d%%", utilization);
        led_set_alert(get_utilization_color(current_utilization_status));
        if (current_utilization_status > last_utilization_status) {
          // Utilization increased, alert with haptic and buzzer
          haptic_vibrate(get_haptic_pattern(current_utilization_status));
          // Caution and warning have no tone assigned, which is not an error - only call the
          // buzzer when there is something to play.
          BuzzerToneFrequency tone = get_buzzer_tone(current_utilization_status);
          if (tone) {
            buzzer_set_tone(tone, 200 * current_utilization_status);
          }
        }
      }
      else {
        ESP_LOGD(TAG, "Utilization normal: %d%%", utilization);
        led_clear_alert();
        haptic_stop_vibration();
        buzzer_stop();
      }
      last_utilization_status = current_utilization_status;
      continue;
    }

    vTaskDelay(pdMS_TO_TICKS(VEHICLE_STATE_LOOP_TIME_MS));
  }
  vTaskDelete(NULL);
  monitor_task_handle = NULL;
}

static StaticTask_t monitor_task_tcb;
static StackType_t *monitor_task_stack;

void vehicle_monitor_init() {
  monitor_task_handle =
      create_psram_task(monitor_task, "monitor_task", 4096, NULL, 5, &monitor_task_tcb, &monitor_task_stack);
  ESP_ERROR_CHECK(monitor_task_handle ? ESP_OK : ESP_FAIL);
  ESP_LOGI("VEHICLE_STATE", "Vehicle state monitor initialized");
}

void vehicle_monitor_deinit() {
  if (monitor_task_handle != NULL) {
    vTaskDelete(monitor_task_handle);
    monitor_task_handle = NULL;
  }
}
