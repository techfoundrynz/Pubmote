#ifndef __PSRAM_TASK_H
#define __PSRAM_TASK_H

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Task stacks default to internal DRAM, which is the scarce pool on this hardware - the
// panel staging buffers and the BLE controller both want large contiguous blocks of it.
// A stack may live in PSRAM as long as the task never runs with the cache disabled, so
// this is only for tasks that never write flash/NVS and never enter deep sleep.
//
// Static allocation rather than xTaskCreateWithCaps: these tasks self-delete with
// vTaskDelete(NULL), and the WithCaps path spawns a temporary internal task to free the
// stack and aborts if it cannot. Here the stack is allocated once and kept, so a task can
// stop and restart without leaking or failing. The TCB stays internal - the scheduler
// touches it with the cache off.
//
// Falls back to an ordinary internal-stack task if PSRAM is unavailable.
static inline TaskHandle_t create_psram_task(TaskFunction_t fn, const char *name, uint32_t stack_bytes, void *arg,
                                             UBaseType_t prio, StaticTask_t *tcb, StackType_t **stack)
{
    if (*stack == NULL)
    {
        *stack = (StackType_t *)heap_caps_malloc(stack_bytes, MALLOC_CAP_SPIRAM);
    }
    if (*stack == NULL)
    {
        TaskHandle_t handle = NULL;
        xTaskCreate(fn, name, stack_bytes, arg, prio, &handle);
        return handle;
    }
    return xTaskCreateStatic(fn, name, stack_bytes, arg, prio, *stack, tcb);
}

#endif
