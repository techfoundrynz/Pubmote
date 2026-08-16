#ifndef __MEM_DEBUG_H
#define __MEM_DEBUG_H

#include "config.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

// DEBUG_MEMORY comes from config.h, alongside TEST_MODE
#if DEBUG_MEMORY

// Internal DRAM is the scarce pool - PSRAM is 8MB and largest-free-block is what BLE and
// the panel buffers actually run out of, so all three are worth seeing together.
#define MEM_MARK(label)                                                                                                \
    ESP_LOGW("PUBREMOTE-MEM", "mem %-18s internal=%6u largest=%6u psram=%8u", (label),                                 \
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),                                                   \
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),                                          \
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM))

// Free/min-ever internal heap plus per-task stack headroom.
void mem_debug_report(void);

#else

#define MEM_MARK(label) ((void)0)
#define mem_debug_report() ((void)0)

#endif

#endif
