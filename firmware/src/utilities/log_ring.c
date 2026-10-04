#include "log_ring.h"
#include <string.h>

void log_ring_append(log_ring_t *ring, const char *record, size_t length) {
  if (!length || length > ring->capacity)
    return;
  while (ring->capacity - ring->length < length) {
    char c;
    do {
      c = ring->data[ring->head];
      ring->head = (ring->head + 1) % ring->capacity;
      --ring->length;
    } while (ring->length && c != '\n');
    ++ring->overwritten;
  }
  size_t tail = (ring->head + ring->length) % ring->capacity;
  size_t first = ring->capacity - tail;
  if (first > length)
    first = length;
  memcpy(ring->data + tail, record, first);
  memcpy(ring->data, record + first, length - first);
  ring->length += length;
}

void log_ring_copy(const log_ring_t *ring, char *out) {
  size_t first = ring->capacity - ring->head;
  if (first > ring->length)
    first = ring->length;
  memcpy(out, ring->data + ring->head, first);
  memcpy(out + first, ring->data, ring->length - first);
  out[ring->length] = '\0';
}
