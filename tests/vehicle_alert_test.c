#include <assert.h>
#include <setjmp.h>
#include <stddef.h>
#include "vehicle_state.h"
#include "tones.h"
#include "haptic/haptic_patterns.h"
#include "remote/stats.h"

typedef void *TaskHandle_t;
typedef int StaticTask_t;
typedef int StackType_t;
#define ESP_OK 0
#define ESP_FAIL 1
#define ESP_ERROR_CHECK(value) assert((value) == ESP_OK)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define pdMS_TO_TICKS(value) (value)
void vTaskDelay(unsigned ticks);
void vTaskDelete(TaskHandle_t task);
TaskHandle_t create_psram_task(void (*task)(void *), const char *name, unsigned stack_size,
                             void *arg, unsigned priority, StaticTask_t *tcb, StackType_t **stack);
void led_set_alert(uint32_t color);
void led_clear_alert(void);
void haptic_vibrate(HapticFeedbackPattern pattern);
void haptic_stop_vibration(void);
void buzzer_set_tone(BuzzerToneFrequency tone, int duration);
void buzzer_stop(void);
#include "vehicle_state.c"

static jmp_buf monitor_exit;
static unsigned step, led_updates, haptic_updates, tones, clears;
static unsigned haptic_stops, buzzer_stops;
static uint32_t last_color;
static HapticFeedbackPattern last_pattern;

// Current limits must trigger alerts even with duty below every threshold.
static const struct {
  uint8_t duty;
  int8_t phase, battery;
  unsigned leds, haptics, tones, clears;
  uint32_t color;
  HapticFeedbackPattern pattern;
} frames[] = {
    {69, 0, 0, 0, 0, 0, 0, 0, HAPTIC_NONE},
    {20, 70, 0, 1, 1, 0, 0, UTILIZATION_COLOR_CAUTION, HAPTIC_SOFT_BUZZ},
    {20, 80, 0, 2, 2, 0, 0, UTILIZATION_COLOR_WARNING, HAPTIC_ALERT_750MS},
    {20, 0, -90, 3, 3, 1, 0, UTILIZATION_COLOR_CRITICAL, HAPTIC_ALERT_1000MS},
    {20, 0, -90, 3, 3, 1, 0, UTILIZATION_COLOR_CRITICAL, HAPTIC_ALERT_1000MS},
    {20, 0, -80, 4, 3, 1, 0, UTILIZATION_COLOR_WARNING, HAPTIC_ALERT_1000MS},
    {20, -69, 69, 4, 3, 1, 1, UTILIZATION_COLOR_WARNING, HAPTIC_ALERT_1000MS},
    {90, 0, 0, 5, 4, 2, 1, UTILIZATION_COLOR_CRITICAL, HAPTIC_ALERT_1000MS},
    {0, 0, 0, 5, 4, 2, 2, UTILIZATION_COLOR_CRITICAL, HAPTIC_ALERT_1000MS},
};

static void publish_frame(void) {
  BoardTelemetry reading = {.dutyCycle = frames[step].duty,
                            .phaseUtilization = frames[step].phase,
                            .batteryUtilization = frames[step].battery};
  stats_publish_board(&reading, step);
}

void vTaskDelay(unsigned ticks) {
  assert(ticks == VEHICLE_STATE_LOOP_TIME_MS);
  assert(led_updates == frames[step].leds);
  assert(haptic_updates == frames[step].haptics);
  assert(tones == frames[step].tones);
  assert(clears == frames[step].clears);
  assert(haptic_stops == clears && buzzer_stops == clears);
  assert(last_color == frames[step].color);
  assert(last_pattern == frames[step].pattern);
  if (++step == sizeof(frames) / sizeof(frames[0]))
    longjmp(monitor_exit, 1);
  publish_frame();
}
void led_set_alert(uint32_t color) { ++led_updates; last_color = color; }
void led_clear_alert(void) { ++clears; }
void haptic_vibrate(HapticFeedbackPattern pattern) { ++haptic_updates; last_pattern = pattern; }
void haptic_stop_vibration(void) { ++haptic_stops; }
void buzzer_set_tone(BuzzerToneFrequency tone, int duration) {
  assert(tone == NOTE_CRITICAL && duration == 600);
  ++tones;
}
void buzzer_stop(void) { ++buzzer_stops; }
void vTaskDelete(TaskHandle_t task) { (void)task; }
TaskHandle_t create_psram_task(void (*task)(void *), const char *name, unsigned stack_size,
                             void *arg, unsigned priority, StaticTask_t *tcb, StackType_t **stack) {
  (void)task; (void)name; (void)stack_size; (void)arg; (void)priority; (void)tcb; (void)stack;
  return (void *)1;
}

int main(void) {
  publish_frame();
  if (setjmp(monitor_exit) == 0)
    monitor_task(NULL);
  assert(step == sizeof(frames) / sizeof(frames[0]));
  return 0;
}
