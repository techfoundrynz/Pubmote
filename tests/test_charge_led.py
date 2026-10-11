"""Test the real charging LED output with strip/task/timer I/O stubbed."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from test_charge_diagnostics import ROOT, without_includes


STUBS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <stdatomic.h>
#define LED_ENABLED 1
#define LED_DATA 1
#define LED_COUNT 1
#define LED_STRIP_COLOR_COMPONENT_FMT_GRB 0
#define LED_MODEL_WS2812 0
#define RMT_CLK_SRC_DEFAULT 0
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_ERROR_CHECK(x) assert((x) == ESP_OK)
#define pdMS_TO_TICKS(x) (x)
typedef int esp_err_t, StaticTask_t, StackType_t;
typedef void *TaskHandle_t, *led_strip_handle_t, *esp_timer_handle_t;
typedef enum {LED_MODE_OFF, LED_MODE_SOLID, LED_MODE_ALERTS, LED_MODE_COUNT} LedModeOptions;
typedef struct { LedModeOptions led_mode; uint32_t theme_color; } DeviceSettings;
static DeviceSettings preferences = {LED_MODE_SOLID, 0x0000FF};
DeviceSettings settings_get_device(void) { return preferences; }
static int64_t clock_us = 1500000;
int64_t esp_timer_get_time(void) { return clock_us; }
void vTaskDelay(int ticks) {} void vTaskDelete(TaskHandle_t task) {}
TaskHandle_t create_psram_task(void (*f)(void*), const char* name, int size, void* arg,
                              int priority, StaticTask_t* tcb, StackType_t** stack) { return (void*)1; }
void register_startup_cb(void (*cb)(void)) {}
typedef struct { int strip_gpio_num, max_leds, color_component_format, led_model;
                 struct { bool invert_out; } flags; } led_strip_config_t;
typedef struct { int clk_src, resolution_hz; struct { bool with_dma; } flags; } led_strip_rmt_config_t;
int led_strip_new_rmt_device(void* config, void* rmt, led_strip_handle_t* handle) { *handle=(void*)1; return 0; }
static unsigned pixel[3];
int led_strip_set_pixel(led_strip_handle_t s, int i, unsigned r, unsigned g, unsigned b) {
  pixel[0]=r; pixel[1]=g; pixel[2]=b; return 0;
}
int led_strip_clear(led_strip_handle_t s) { pixel[0]=pixel[1]=pixel[2]=0; return 0; }
int led_strip_refresh(led_strip_handle_t s) { return 0; }
int led_strip_del(led_strip_handle_t s) { return 0; }
const char* esp_err_to_name(int err) { return "test"; }
#define ESP_TIMER_TASK 0
typedef struct { void (*callback)(void*); void* arg; int dispatch_method;
                 const char* name; bool skip_unhandled_events; } esp_timer_create_args_t;
int esp_timer_create(void* args, esp_timer_handle_t* handle) { *handle=(void*)1; return 0; }
int esp_timer_start_once(esp_timer_handle_t timer, int us) { return 0; }
int esp_timer_delete(esp_timer_handle_t timer) { return 0; }
'''

SCENARIOS = r'''
int main(void) {
  led_strip = (void*)1;
  led_set_effect_solid(0x0000FF);
  led_set_effect_charging(LED_CHARGING_ACTIVE);
  clock_us = 1000000;
  apply_led_effect();
  assert(pixel[0] > pixel[1] && pixel[1] > 0 && pixel[2] == 0);
  unsigned peak = pixel[0];
  clock_us = 2000000;
  apply_led_effect();
  assert(pixel[0] < peak && pixel[0] > 0);

  led_set_effect_charging(LED_CHARGING_COMPLETE);
  apply_led_effect();
  assert(pixel[1] > pixel[0] && pixel[1] > pixel[2]);
  // A late startup callback or alert updates the underlying effect only.
  led_apply_mode();
  led_set_alert(0xFF0000);
  apply_led_effect();
  assert(pixel[1] > pixel[0]);
  led_set_effect_charging(LED_CHARGING_NONE);
  clock_us += 300000;
  apply_led_effect();
  assert(pixel[0] == 255 && pixel[1] == 0 && pixel[2] == 0);

  led_set_effect_charging(LED_CHARGING_POWERED);
  apply_led_effect();
  assert(pixel[0] > pixel[1] && pixel[1] > 0 && pixel[2] == 0);
  preferences.led_mode = LED_MODE_OFF;
  for (int state = LED_CHARGING_ACTIVE; state <= LED_CHARGING_POWERED; ++state) {
    led_set_effect_charging(state);
    apply_led_effect();
    assert(pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0);
  }
}
'''


@unittest.skipUnless(shutil.which("gcc"), "requires a host C compiler")
class ChargeLedTest(unittest.TestCase):
    def test_charging_override_and_restoration(self):
        with tempfile.TemporaryDirectory(prefix="pubremote-led-") as directory:
            source, executable = Path(directory) / "test.c", Path(directory) / "test"
            source.write_text(STUBS + without_includes(ROOT / "firmware/src/remote/led.h")
                              + "\n" + without_includes(ROOT / "firmware/src/remote/led.c")
                              + SCENARIOS, encoding="utf-8")
            subprocess.run(["gcc", "-std=c11", "-UNDEBUG", str(source), "-lm", "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
