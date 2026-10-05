#include "common.h"
#include "diagnostic_log.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
static bool fail_heap, fail_mutex, clear_during_output;
static unsigned output_calls;
static fake_mutex_t diagnostic_lock;
static vprintf_like_t installed_sink;
void *heap_caps_malloc(size_t size, unsigned caps) {
  assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  return fail_heap ? NULL : malloc(size);
}
SemaphoreHandle_t xSemaphoreCreateMutex(void) {
  return fail_mutex ? NULL : &diagnostic_lock;
}
static int output(const char *format, va_list args) {
  (void)format;
  (void)args;
  ++output_calls;
  if (clear_during_output) {
    clear_during_output = false;
    assert(diagnostic_log_clear());
  }
  return 123;
}
vprintf_like_t esp_log_set_vprintf(vprintf_like_t sink) {
  installed_sink = sink;
  return output;
}
#include "../../firmware/src/utilities/diagnostic_log.c"
static int emit(const char *format, ...) {
  va_list args;
  va_start(args, format);
  int result = installed_sink(format, args);
  va_end(args);
  return result;
}
int main(void) {
  host_test_init();
  diagnostic_snapshot_t snapshot;
  fail_heap = true;
  assert(!diagnostic_log_init() && !diagnostic_log_snapshot(&snapshot));
  fail_heap = false;
  fail_mutex = true;
  assert(!diagnostic_log_init());
  fail_mutex = false;
  assert(diagnostic_log_init());
  assert(emit("\033[0;32mI (1) PUBMOTE-WIFI: connected\033[0m\n") == 123);
  emit("I (2) PUBREMOTE-CONSOLE: password=secret\n");
  emit("I (3) PUBREMOTE-CONNECTION: recovered\n");
  assert(diagnostic_log_snapshot(&snapshot));
  assert(strstr(snapshot.text, "connected\n") && strstr(snapshot.text, "recovered\n"));
  assert(!strstr(snapshot.text, "secret") && !strchr(snapshot.text, 0x1b) && !snapshot.overwritten);
  diagnostic_log_snapshot_free(&snapshot);
  assert(!snapshot.text);
  deny_take = true;
  emit("W (4) PUBMOTE-WIFI: dropped\n");
  deny_take = false;
  char large[1000];
  memset(large, 'x', sizeof(large) - 1);
  large[sizeof(large) - 1] = 0;
  emit("E (5) PUBMOTE-OTA: %s", large);
  assert(diagnostic_log_snapshot(&snapshot));
  assert(snapshot.dropped == 1 && snapshot.truncated == 1 && strstr(snapshot.text, "[truncated]\n"));
  diagnostic_log_snapshot_free(&snapshot);
  assert(diagnostic_log_clear());
  for (unsigned i = 0; i < 1000; ++i)
    emit("I (%u) PUBMOTE-WIFI: record %u\n", i, i);
  assert(diagnostic_log_snapshot(&snapshot));
  assert(strlen(snapshot.text) <= DIAGNOSTIC_LOG_CAPACITY && snapshot.overwritten > 0);
  assert(!strncmp(snapshot.text, "I (", 3) && strstr(snapshot.text, "record 999\n"));
  assert(!strstr(snapshot.text, "record 0\n"));
  diagnostic_log_snapshot_free(&snapshot);
  clear_during_output = true;
  emit("I (1001) PUBMOTE-WIFI: stale\n");
  assert(diagnostic_log_snapshot(&snapshot));
  assert(!snapshot.text[0] && !snapshot.overwritten);
  diagnostic_log_snapshot_free(&snapshot);
  fail_heap = true;
  assert(!diagnostic_log_snapshot(&snapshot) && !snapshot.text);
  fail_heap = false;
  deny_take = true;
  assert(!diagnostic_log_snapshot(&snapshot) && !snapshot.text);
  deny_take = false;
  assert(output_calls == 1006);
  puts("Diagnostic failure paths passed");
  return 0;
}
