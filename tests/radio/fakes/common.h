#pragma once
#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _MSC_VER
  #include <crtdbg.h>
#endif
static void host_test_init(void) {
#ifdef _MSC_VER
  _set_error_mode(_OUT_TO_STDERR);
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
}

typedef int esp_err_t;
typedef unsigned TickType_t;
typedef struct {
  bool held;
} fake_mutex_t;
typedef fake_mutex_t *SemaphoreHandle_t;
#define pdTRUE 1
#define pdMS_TO_TICKS(ms) (ms)
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE -2
#define ESP_ERR_TIMEOUT -3
#define ESP_ERR_INVALID_ARG -4
#define ESP_ERR_NO_MEM -5
#define ESP_ERR_INVALID_SIZE -6
#define ESP_ERR_WIFI_NOT_INIT -7
#define ESP_ERR_NOT_FOUND -8
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
static unsigned ticks;
static unsigned take_failures;
static bool deny_take;
static int xSemaphoreTake(SemaphoreHandle_t mutex, unsigned timeout) {
  (void)timeout;
  if (deny_take || take_failures) {
    if (take_failures)
      --take_failures;
    return 0;
  }
  if (!mutex || mutex->held)
    return 0;
  mutex->held = true;
  return pdTRUE;
}
static void xSemaphoreGive(SemaphoreHandle_t mutex) {
  assert(mutex->held);
  mutex->held = false;
}
static void vTaskDelay(unsigned delay) {
  ticks += delay;
}
static unsigned xTaskGetTickCount(void) {
  return ticks;
}
static const char *esp_err_to_name(int error) {
  (void)error;
  return "fake error";
}
