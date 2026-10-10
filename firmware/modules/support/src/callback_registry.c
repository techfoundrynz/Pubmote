#include "utilities/callback_registry.h"
#include "esp_log.h"

static const char *TAG = "PUBREMOTE-CALLBACK_REGISTRY";

static void cleanup_registry(callback_registry_t *registry) {
  free(registry->callbacks);
  registry->callbacks = NULL;
  registry->count = registry->capacity = 0;
}

void register_cb(callback_registry_t *registry, callback_t callback) {
  if (registry->count >= registry->capacity) {
    size_t new_capacity = registry->capacity + 1; // Just add 1
    callback_t *new_callbacks = realloc(registry->callbacks, new_capacity * sizeof(callback_t));
    if (!new_callbacks) {
      ESP_LOGE(TAG, "Failed to allocate memory for callback registry");
      return;
    }
    registry->callbacks = new_callbacks;
    registry->capacity = new_capacity;
  }

  registry->callbacks[registry->count++] = callback;
}

void remove_cb(callback_registry_t *registry, callback_t callback) {
  for (size_t i = 0; i < registry->count; i++) {
    if (registry->callbacks[i] == callback) {
      // Shift remaining callbacks down
      for (size_t j = i; j < registry->count - 1; j++) {
        registry->callbacks[j] = registry->callbacks[j + 1];
      }
      registry->count--;

      // shrink capacity
      if (registry->capacity > registry->count && registry->count > 0) {
        size_t new_capacity = registry->count;
        callback_t *new_callbacks = realloc(registry->callbacks, new_capacity * sizeof(callback_t));
        if (new_callbacks) {
          registry->callbacks = new_callbacks;
          registry->capacity = new_capacity;
        }
      }

      // Handle empty registry
      if (registry->count == 0) {
        free(registry->callbacks);
        registry->callbacks = NULL;
        registry->capacity = 0;
      }

      return;
    }
  }
  ESP_LOGW(TAG, "Callback not found in registry");
}

void registry_cb(callback_registry_t *registry, bool cleanup) {
  for (size_t i = 0; i < registry->count; i++) {
    registry->callbacks[i]();
  }

  if (cleanup) {
    cleanup_registry(registry);
  }
}
