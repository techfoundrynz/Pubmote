#include "buzzer.h"
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
#include "tones.h"
#include <driver/ledc.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <led_strip_types.h>
#include <nvs.h>

static const char *TAG = "PUBREMOTE-BUZZER";

static TaskHandle_t buzzer_task_handle = NULL;

#if BUZZER_ENABLED
  #define BUZZER_DELAY_MS 50 // Delay for the buzzer task
  #define BUZZER_CHANNEL LEDC_CHANNEL_1
  #define BUZZER_TIMER LEDC_TIMER_1
  #define BUZZER_RESOLUTION LEDC_TIMER_10_BIT
  #define BUZZER_MAX_DUTY ((1 << 10) - 1)
#endif

#if BUZZER_ENABLED
static BuzzerPatttern current_pattern = BUZZER_PATTERN_NONE;
static int current_frequency = 0;
static int current_time_left = 0;
static int current_duty = 0;

// Note (hz), duration (ms)
static const int melody[] = {NOTE_C4, 100, NOTE_D4, 100, NOTE_E4, 100, NOTE_F4, 100,
                             NOTE_G4, 100, NOTE_A4, 100, NOTE_B4, 100, NOTE_C5, 200};

static const int melody_duration = 900;
#endif

void buzzer_stop() {
#if BUZZER_ENABLED
  int duty = BUZZER_LEVEL ? 0 : BUZZER_MAX_DUTY; // Invert duty if needed
  current_pattern = BUZZER_PATTERN_NONE;         // Reset pattern

  if (duty == current_duty) {
    return;
  }
  ledc_set_duty(LEDC_LOW_SPEED_MODE, BUZZER_CHANNEL, duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, BUZZER_CHANNEL);
  current_duty = duty;
#endif
}

static void process_current_note() {
#if BUZZER_ENABLED
  static int last_frequency = 0;
  uint32_t duty = BUZZER_MAX_DUTY / 2;

  if (current_frequency != last_frequency) {
    // Configure the timer with the new frequency
    ledc_timer_config_t timer_conf = {.speed_mode = LEDC_LOW_SPEED_MODE,
                                      .timer_num = BUZZER_TIMER,
                                      .duty_resolution = BUZZER_RESOLUTION,
                                      .freq_hz = current_frequency,
                                      .clk_cfg = LEDC_AUTO_CLK};
    ledc_timer_config(&timer_conf);
    last_frequency = current_frequency;
  }

  // Start the buzzer
  if (current_duty != duty) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BUZZER_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BUZZER_CHANNEL);
    current_duty = duty;
  }
#endif
}

#if BUZZER_ENABLED
static void process_melody() {
  #if BUZZER_ENABLED
  const int melody_length = sizeof(melody) / sizeof(melody[0]);
  int current_time = 0;
  int elapsed_time_ms = melody_duration - current_time_left;
  int frequency = melody[0]; // Start with the first note

  // Iterate through melody pairs (note, duration)
  for (int i = 0; i < melody_length; i += 2) {
    int note_frequency = melody[i];
    int note_duration = melody[i + 1];

    // Check if elapsed time falls within this note's time window
    if (elapsed_time_ms >= current_time && elapsed_time_ms < current_time + note_duration) {
      frequency = note_frequency;
      break;
    }

    current_time += note_duration;
  }

  current_frequency = frequency;
  #endif
}
#endif

void buzzer_set_pattern(BuzzerPatttern pattern) {
#if BUZZER_ENABLED
  ESP_LOGI(TAG, "Setting buzzer pattern to %d", pattern);

  switch (pattern) {
  case BUZZER_PATTERN_MELODY:
    current_pattern = pattern;
    current_time_left = melody_duration;
    ESP_LOGI(TAG, "Buzzer pattern set to MELODY");
    break;
  case BUZZER_PATTERN_SOLID:
    current_pattern = pattern;
    current_frequency = NOTE_C5; // Default beep frequency
    if (current_time_left <= 0) {
      current_time_left = 200; // Default beep duration
    }
    ESP_LOGI(TAG, "Buzzer pattern set to SOLID");
    break;
  default:
    buzzer_stop();                         // Stop any current sound
    current_pattern = BUZZER_PATTERN_NONE; // Reset pattern
    current_frequency = 0;                 // Reset frequency
    current_time_left = 0;                 // Reset time left
    break;
  }
#endif
}

void buzzer_set_tone(BuzzerToneFrequency frequency, int duration) {
#if BUZZER_ENABLED
  if (!frequency || !duration) {
    ESP_LOGW(TAG, "Invalid frequency or duration for buzzer tone");
    buzzer_set_pattern(BUZZER_PATTERN_NONE); // Reset to no pattern
    return;
  }
  buzzer_stop();                          // Stop any current sound
  current_pattern = BUZZER_PATTERN_SOLID; // Set to solid tone
  current_frequency = frequency;
  current_time_left = duration;
#endif
}

#if BUZZER_ENABLED
static void process_buzzer_pattern() {
  if (current_pattern == BUZZER_PATTERN_MELODY) {
    process_melody();
    process_current_note();
    return;
  }

  if (current_pattern == BUZZER_PATTERN_SOLID) {
    process_current_note();
    return;
  }
}

#endif

static void play_startup_effect() {
#if BUZZER_ENABLED
  // Handle startup
  if (device_settings.startup_sound == STARTUP_SOUND_MELODY) {
    buzzer_set_pattern(BUZZER_PATTERN_MELODY);
  }
  else if (device_settings.startup_sound == STARTUP_SOUND_BEEP) {
    buzzer_set_tone(NOTE_C5, 500); // Default beep tone
  }
#endif
}

#if BUZZER_ENABLED
// task to play melody
static void buzzer_task(void *pvParameters) {

  while (true) {
    if (current_time_left == 0 || current_pattern == BUZZER_PATTERN_NONE) {
      current_pattern = BUZZER_PATTERN_NONE; // Reset pattern
      current_time_left = 0;                 // Reset time left
      buzzer_stop();
      vTaskDelay(pdMS_TO_TICKS(BUZZER_DELAY_MS));
      continue;
    }

    process_buzzer_pattern();
    current_time_left -= BUZZER_DELAY_MS; // Decrease time left
    if (current_time_left < 0) {
      current_time_left = 0; // Prevent negative time left
    }
    vTaskDelay(pdMS_TO_TICKS(BUZZER_DELAY_MS));
  }
  buzzer_stop();
  vTaskDelete(NULL);
  buzzer_task_handle = NULL;
}
#endif

void buzzer_init() {
#if BUZZER_ENABLED
  ledc_channel_config_t channel_conf = {
      .gpio_num = BUZZER_PWM,
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .channel = BUZZER_CHANNEL,
      .intr_type = LEDC_INTR_DISABLE,
      .timer_sel = BUZZER_TIMER,
  #if BUZZER_LEVEL
      .duty = 0,
  #else
      .duty = BUZZER_MAX_DUTY,
  #endif
  };
  ledc_channel_config(&channel_conf);
  ESP_ERROR_CHECK(xTaskCreate(buzzer_task, "buzzer_task", 1024, NULL, 2, &buzzer_task_handle) == pdPASS ? ESP_OK
                                                                                                        : ESP_FAIL);
  register_startup_cb(play_startup_effect);
#endif
}

void buzzer_deinit() {
#if BUZZER_ENABLED
  buzzer_stop();
  if (buzzer_task_handle != NULL) {
    vTaskDelete(buzzer_task_handle);
    buzzer_task_handle = NULL;
  }
  buzzer_stop();
#endif
}