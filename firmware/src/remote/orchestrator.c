#include "remote/orchestrator.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "remote/console.h"
#include "utilities/mem_debug.h"
#include "utilities/psram_task.h"

#define ORCHESTRATOR_PERIOD_MS 250
#define MEM_REPORT_EVERY 8

static StaticTask_t orchestrator_tcb;
static StackType_t *orchestrator_stack;

static void orchestrator_task(void *pvParameters)
{
#if DEBUG_MEMORY
    int since_report = 0;
#endif
    while (1)
    {
        console_poll_usb();
#if DEBUG_MEMORY
        if (++since_report >= MEM_REPORT_EVERY)
        {
            since_report = 0;
            mem_debug_report();
        }
#endif
        vTaskDelay(pdMS_TO_TICKS(ORCHESTRATOR_PERIOD_MS));
    }
}

void orchestrator_init(void)
{
    create_psram_task(orchestrator_task, "orchestrator", 4096, NULL, 1, &orchestrator_tcb, &orchestrator_stack);
}
