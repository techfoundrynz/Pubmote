#include "diagnostic_log.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "log_ring.h"
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RECORD_CAPACITY 384
static log_ring_t ring;
static SemaphoreHandle_t lock;
static uint32_t truncated;
static atomic_uint dropped;
static atomic_uint generation;
static _Atomic(vprintf_like_t) previous_sink = vprintf;

static bool capture_tag(const char *record) {
  // Capture known connectivity tags only; settings/console logs may contain secrets.
  static const char *const tags[] = {
      "PUBREMOTE-BLE-DRV:",    "PUBMOTE-WIFI:",          "PUBMOTE-OTA:",        "PUBREMOTE-UPDATE_SCREEN:",
      "PUBREMOTE-CONNECTION:", "PUBREMOTE-TRANSMITTER:", "PUBREMOTE-RECEIVER:", "PUBREMOTE-MEM:",
  };
  const char *tag = strchr(record, ')');
  if (!tag)
    return false;
  ++tag;
  while (*tag == ' ')
    ++tag;
  for (size_t i = 0; i < sizeof(tags) / sizeof(tags[0]); ++i) {
    if (strncmp(tag, tags[i], strlen(tags[i])) == 0)
      return true;
  }
  return false;
}

static int capture_printf(const char *format, va_list args) {
  unsigned started = atomic_load(&generation);
  va_list output_args, capture_args;
  va_copy(output_args, args);
  va_copy(capture_args, args);
  int result = atomic_load(&previous_sink)(format, output_args);
  va_end(output_args);
  char record[RECORD_CAPACITY];
  int written = vsnprintf(record, sizeof(record), format, capture_args);
  va_end(capture_args);
  if (written <= 0 || !capture_tag(record))
    return result;

  bool was_truncated = written >= (int)sizeof(record) - 1;
  size_t source_length = written >= (int)sizeof(record) ? sizeof(record) - 1 : (size_t)written;
  size_t length = 0;
  for (size_t i = 0; i < source_length && length < sizeof(record) - 2; ++i) {
    unsigned char c = record[i];
    // Remove ANSI colours and keep every record on one line.
    if (c == 0x1b && i + 1 < source_length && record[i + 1] == '[') {
      i += 2;
      while (i < source_length && !(record[i] >= '@' && record[i] <= '~'))
        ++i;
      continue;
    }
    if (c == '\r' || c == '\n')
      c = ' ';
    if (c < 0x20 && c != '\t')
      c = '.';
    record[length++] = c;
  }
  while (length && record[length - 1] == ' ')
    --length;
  if (was_truncated) {
    static const char marker[] = " [truncated]";
    if (length > sizeof(record) - sizeof(marker) - 1)
      length = sizeof(record) - sizeof(marker) - 1;
    memcpy(record + length, marker, sizeof(marker) - 1);
    length += sizeof(marker) - 1;
  }
  record[length++] = '\n';
  if (xSemaphoreTake(lock, 0) != pdTRUE) {
    atomic_fetch_add(&dropped, 1);
    return result;
  }
  if (started == atomic_load(&generation)) {
    if (was_truncated)
      ++truncated;
    log_ring_append(&ring, record, length);
  }
  xSemaphoreGive(lock);
  return result;
}

bool diagnostic_log_init(void) {
  if (lock)
    return true;
  char *storage = heap_caps_malloc(DIAGNOSTIC_LOG_CAPACITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!storage)
    return false;
  lock = xSemaphoreCreateMutex();
  if (!lock) {
    free(storage);
    return false;
  }
  ring = (log_ring_t){.data = storage, .capacity = DIAGNOSTIC_LOG_CAPACITY};
  atomic_store(&previous_sink, esp_log_set_vprintf(capture_printf));
  return true;
}

bool diagnostic_log_snapshot(diagnostic_snapshot_t *snapshot) {
  if (!snapshot)
    return false;
  memset(snapshot, 0, sizeof(*snapshot));
  if (!lock)
    return false;
  char *text = heap_caps_malloc(DIAGNOSTIC_LOG_CAPACITY + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!text)
    return false;
  if (xSemaphoreTake(lock, pdMS_TO_TICKS(100)) != pdTRUE) {
    free(text);
    return false;
  }
  log_ring_copy(&ring, text);
  snapshot->text = text;
  snapshot->overwritten = ring.overwritten;
  snapshot->truncated = truncated;
  snapshot->dropped = atomic_load(&dropped);
  xSemaphoreGive(lock);
  return true;
}

void diagnostic_log_snapshot_free(diagnostic_snapshot_t *snapshot) {
  if (!snapshot)
    return;
  free(snapshot->text);
  memset(snapshot, 0, sizeof(*snapshot));
}

bool diagnostic_log_clear(void) {
  if (!lock || xSemaphoreTake(lock, pdMS_TO_TICKS(100)) != pdTRUE)
    return false;
  atomic_fetch_add(&generation, 1);
  ring.head = ring.length = ring.overwritten = 0;
  truncated = 0;
  atomic_store(&dropped, 0);
  xSemaphoreGive(lock);
  return true;
}
