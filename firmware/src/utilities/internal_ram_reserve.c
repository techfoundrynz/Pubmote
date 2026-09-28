/*
 * SPDX-FileCopyrightText: 2015-2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 *
 * Adapted from ESP-IDF 5.5 esp_psram_extram_reserve_dma_pool().
 */
#include "esp_heap_caps.h"
#include "esp_heap_caps_init.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include <stdint.h>

#if CONFIG_SPIRAM_USE_MALLOC
// Called by IDF startup before app_main. Match the IDF 5.5 reserve policy:
// ordinary malloc/realloc must spill to PSRAM instead of exhausting the pool
// required by FreeRTOS stacks, libc file locks, and DMA. IDF 6.1 adds
// MALLOC_CAP_DEFAULT to this pool, making the configured reserve only a preference.
esp_err_t __wrap_esp_psram_extram_reserve_dma_pool(size_t size) {
  ESP_EARLY_LOGI("internal_reserve", "Protecting %u bytes for DMA/internal allocations", (unsigned)size);
  while (size > 0) {
    size_t chunk = heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (chunk > size)
      chunk = size;
    if (!chunk)
      return ESP_ERR_NO_MEM;
    uint8_t *pool = heap_caps_malloc(chunk, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!pool)
      return ESP_ERR_NO_MEM;
    const uint32_t caps[] = {0, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL, MALLOC_CAP_8BIT | MALLOC_CAP_32BIT};
    esp_err_t result = heap_caps_add_region_with_caps(caps, (intptr_t)pool, (intptr_t)pool + chunk - 1);
    if (result != ESP_OK) {
      heap_caps_free(pool);
      return result;
    }
    size -= chunk;
  }
  return ESP_OK;
}
#endif
