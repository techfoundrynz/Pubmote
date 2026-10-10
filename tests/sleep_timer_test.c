#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERROR_CHECK(result) assert((result) == ESP_OK)
typedef int esp_err_t;
uint64_t get_auto_off_ms(void);
#include "remote/sleep_timer.c"

static uint64_t duration = 300000;
static int64_t now;
static bool active;
static uint64_t last_delay;
static int stops, shutdowns;
static void (*queued_callback)(void *);
uint64_t get_auto_off_ms(void) {
  return duration;
}
int64_t esp_timer_get_time(void) {
  return now;
}
bool esp_timer_is_active(esp_timer_handle_t timer) {
  (void)timer;
  return active;
}
int esp_timer_stop(esp_timer_handle_t timer) {
  (void)timer;
  ++stops;
  active = false;
  return ESP_OK;
}
int esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *timer) {
  *timer = (void *)1;
  queued_callback = args->callback;
  return ESP_OK;
}
int esp_timer_start_once(esp_timer_handle_t timer, uint64_t delay) {
  (void)timer;
  active = true;
  last_delay = delay;
  return ESP_OK;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void) {
  return (void *)1;
}
int xSemaphoreTake(SemaphoreHandle_t mutex, uint32_t timeout) {
  (void)mutex;
  (void)timeout;
  return pdTRUE;
}
void xSemaphoreGive(SemaphoreHandle_t mutex) {
  (void)mutex;
}
static void shutdown(void) {
  ++shutdowns;
}

int main(void) {
  sleep_timer_init(shutdown);
  reset_sleep_timer();
  assert(active && last_delay == 300000000);
  duration = 0;
  reset_sleep_timer();
  assert(!active && stops == 1);
  now = 300000000;
  queued_callback(NULL);
  assert(shutdowns == 0); // Already queued expiry cannot shut down a disabled timer.
  duration = 120000;
  reset_sleep_timer();
  assert(active && last_delay == 120000000);
  now += 60000000;
  duration = 600000;
  reset_sleep_timer();
  now += 60000000;
  queued_callback(NULL); // Old two-minute deadline.
  assert(shutdowns == 0);
  now += 540000000;
  queued_callback(NULL);
  assert(shutdowns == 1);
  queued_callback(NULL);
  assert(shutdowns == 1);
  return 0;
}
