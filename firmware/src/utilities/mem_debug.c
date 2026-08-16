#include "utilities/mem_debug.h"

#if DEBUG_MEMORY

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "PUBREMOTE-MEM";

// FreeRTOS keeps at most configMAX_TASK_NAME_LEN-1 (15) characters and xTaskGetHandle
// asserts on anything longer, so these are the truncated names, not the created ones.
static const char *const watched_tasks[] = {
    "slint_event_loo", "slint_input_tas", "imu_task",        "monitor_task", "receiver_task",
    "transmitter_tas", "connection_task", "power_managemen", "led_task",     "buzzer_task",
    "ble_rssi_poll",   "thumbstick_task", "orchestrator",
};

void mem_debug_report(void)
{
    ESP_LOGI(TAG, "internal free=%u min-ever=%u largest=%u psram=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    for (size_t i = 0; i < sizeof(watched_tasks) / sizeof(watched_tasks[0]); i++)
    {
        TaskHandle_t handle = xTaskGetHandle(watched_tasks[i]);
        if (handle)
        {
            ESP_LOGW(TAG, "stack %-18s unused=%5u", watched_tasks[i], (unsigned)uxTaskGetStackHighWaterMark(handle));
        }
    }
}

#endif
