#pragma once
#include <stddef.h>
#include <stdint.h>

// Caller owns storage and synchronization. Records must end with a newline.
typedef struct {
  char *data;
  size_t capacity;
  size_t head;
  size_t length;
  uint32_t overwritten;
} log_ring_t;

void log_ring_append(log_ring_t *ring, const char *record, size_t length);
void log_ring_copy(const log_ring_t *ring, char *out);
