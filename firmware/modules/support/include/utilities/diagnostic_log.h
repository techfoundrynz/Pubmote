#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define DIAGNOSTIC_LOG_CAPACITY (16 * 1024)

  typedef struct {
    char *text;
    uint32_t overwritten;
    uint32_t truncated;
    uint32_t dropped;
  } diagnostic_snapshot_t;

  // Optional: fails gracefully if PSRAM is unavailable. Preserves the console sink.
  bool diagnostic_log_init(void);
  bool diagnostic_log_snapshot(diagnostic_snapshot_t *snapshot);
  void diagnostic_log_snapshot_free(diagnostic_snapshot_t *snapshot);
  bool diagnostic_log_clear(void);

#ifdef __cplusplus
}
#endif
