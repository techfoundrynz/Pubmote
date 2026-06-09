/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/idf_additions.h"
#include "esp_lvgl_port.h"
#include "esp_lvgl_port_priv.h"
#include "lvgl.h"

static const char *TAG = "LVGL";

/*******************************************************************************
* Types definitions
*******************************************************************************/

typedef struct lvgl_port_ctx_s {
    TaskHandle_t        lvgl_task;
    SemaphoreHandle_t   lvgl_mux;
    esp_timer_handle_t  tick_timer;
    bool                running;
    int                 task_max_sleep_ms;
    int                 timer_period_ms;
} lvgl_port_ctx_t;

/*******************************************************************************
* Local variables
*******************************************************************************/
static lvgl_port_ctx_t lvgl_port_ctx;

uint64_t lvgl_flush_time_us = 0;
bool lvgl_frame_rendered = false;

/*******************************************************************************
* Function definitions
*******************************************************************************/
static void lvgl_port_task(void *arg);
static esp_err_t lvgl_port_tick_init(void);
static void lvgl_port_task_deinit(void);

/*******************************************************************************
* Public API functions
*******************************************************************************/

esp_err_t lvgl_port_init(const lvgl_port_cfg_t *cfg)
{
    esp_err_t ret = ESP_OK;
    ESP_GOTO_ON_FALSE(cfg, ESP_ERR_INVALID_ARG, err, TAG, "invalid argument");
    ESP_GOTO_ON_FALSE(cfg->task_affinity < (configNUM_CORES), ESP_ERR_INVALID_ARG, err, TAG, "Bad core number for task! Maximum core number is %d", (configNUM_CORES - 1));

    memset(&lvgl_port_ctx, 0, sizeof(lvgl_port_ctx));

    /* LVGL init */
    lv_init();
    /* Tick init */
    lvgl_port_ctx.timer_period_ms = cfg->timer_period_ms;
    ESP_RETURN_ON_ERROR(lvgl_port_tick_init(), TAG, "");
    /* Create task */
    lvgl_port_ctx.task_max_sleep_ms = cfg->task_max_sleep_ms;
    if (lvgl_port_ctx.task_max_sleep_ms == 0) {
        lvgl_port_ctx.task_max_sleep_ms = 500;
    }
    /* LVGL semaphore */
    lvgl_port_ctx.lvgl_mux = xSemaphoreCreateRecursiveMutex();
    ESP_GOTO_ON_FALSE(lvgl_port_ctx.lvgl_mux, ESP_ERR_NO_MEM, err, TAG, "Create LVGL mutex fail!");

    BaseType_t res;
    const uint32_t caps = cfg->task_stack_caps ? cfg->task_stack_caps : MALLOC_CAP_INTERNAL | MALLOC_CAP_DEFAULT; // caps cannot be zero
    if (cfg->task_affinity < 0) {
        res = xTaskCreateWithCaps(lvgl_port_task, "taskLVGL", cfg->task_stack, NULL, cfg->task_priority, &lvgl_port_ctx.lvgl_task, caps);
    } else {
        res = xTaskCreatePinnedToCoreWithCaps(lvgl_port_task, "taskLVGL", cfg->task_stack, NULL, cfg->task_priority, &lvgl_port_ctx.lvgl_task, cfg->task_affinity, caps);
    }
    ESP_GOTO_ON_FALSE(res == pdPASS, ESP_FAIL, err, TAG, "Create LVGL task fail!");

err:
    if (ret != ESP_OK) {
        lvgl_port_deinit();
    }

    return ret;
}

esp_err_t lvgl_port_resume(void)
{
    esp_err_t ret = ESP_ERR_INVALID_STATE;

    if (lvgl_port_ctx.tick_timer != NULL) {
        lv_timer_enable(true);
        ret = esp_timer_start_periodic(lvgl_port_ctx.tick_timer, lvgl_port_ctx.timer_period_ms * 1000);
    }

    return ret;
}

esp_err_t lvgl_port_stop(void)
{
    esp_err_t ret = ESP_ERR_INVALID_STATE;

    if (lvgl_port_ctx.tick_timer != NULL) {
        lv_timer_enable(false);
        ret = esp_timer_stop(lvgl_port_ctx.tick_timer);
    }

    return ret;
}

esp_err_t lvgl_port_deinit(void)
{
    /* Stop running task */
    if (lvgl_port_ctx.running) {
        lvgl_port_ctx.running = false;
    }

    return ESP_OK;
}

bool lvgl_port_lock(uint32_t timeout_ms)
{
    assert(lvgl_port_ctx.lvgl_mux && "lvgl_port_init must be called first");

    const TickType_t timeout_ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(lvgl_port_ctx.lvgl_mux, timeout_ticks) == pdTRUE;
}

void lvgl_port_unlock(void)
{
    assert(lvgl_port_ctx.lvgl_mux && "lvgl_port_init must be called first");
    xSemaphoreGiveRecursive(lvgl_port_ctx.lvgl_mux);
}

esp_err_t lvgl_port_task_wake(lvgl_port_event_type_t event, void *param)
{
    ESP_LOGE(TAG, "Task wake is not supported, when used LVGL8!");
    return ESP_ERR_NOT_SUPPORTED;
}

IRAM_ATTR bool lvgl_port_task_notify(uint32_t value)
{
    BaseType_t need_yield = pdFALSE;

    // Notify LVGL task
    if (xPortInIsrContext() == pdTRUE) {
        xTaskNotifyFromISR(lvgl_port_ctx.lvgl_task, value, eNoAction, &need_yield);
    } else {
        xTaskNotify(lvgl_port_ctx.lvgl_task, value, eNoAction);
    }

    return (need_yield == pdTRUE);
}

/*******************************************************************************
* Private functions
*******************************************************************************/

static void lvgl_port_task(void *arg)
{
    uint32_t task_delay_ms = lvgl_port_ctx.task_max_sleep_ms;

    ESP_LOGI(TAG, "Starting LVGL task");
    lvgl_port_ctx.running = true;
    while (lvgl_port_ctx.running) {
        if (lvgl_port_lock(0)) {
            lvgl_flush_time_us = 0;
            lvgl_frame_rendered = false;
            uint64_t start_time = esp_timer_get_time();
            task_delay_ms = lv_timer_handler();
            uint64_t end_time = esp_timer_get_time();
            lvgl_port_unlock();

            if (lvgl_frame_rendered) {
                uint64_t total_draw = end_time - start_time;
                uint64_t wait_transmit = lvgl_flush_time_us;
                uint64_t render = (total_draw > wait_transmit) ? (total_draw - wait_transmit) : 0;

                static uint64_t render_sum = 0;
                static uint64_t wait_transmit_sum = 0;
                static uint64_t total_draw_sum = 0;
                static uint32_t frame_count = 0;

                render_sum += render;
                wait_transmit_sum += wait_transmit;
                total_draw_sum += total_draw;
                frame_count++;

                if (frame_count >= 60) {
                    ESP_LOGI("lvgl_platform", "LVGL Timing [60f avg]: render=%llu us, copy=0 us, wait_transmit=%llu us, total_draw=%llu us",
                             render_sum / 60, wait_transmit_sum / 60, total_draw_sum / 60);
                    render_sum = 0;
                    wait_transmit_sum = 0;
                    total_draw_sum = 0;
                    frame_count = 0;
                }
            }
        }
        if (task_delay_ms > lvgl_port_ctx.task_max_sleep_ms) {
            task_delay_ms = lvgl_port_ctx.task_max_sleep_ms;
        } else if (task_delay_ms < 5) {
            task_delay_ms = 5;
        }
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }

    ESP_LOGI(TAG, "Stopped LVGL task");

    lvgl_port_task_deinit();

    /* Close task */
    vTaskDelete( NULL );
}

static void lvgl_port_task_deinit(void)
{
    /* Stop and delete timer */
    if (lvgl_port_ctx.tick_timer != NULL) {
        esp_timer_stop(lvgl_port_ctx.tick_timer);
        esp_timer_delete(lvgl_port_ctx.tick_timer);
        lvgl_port_ctx.tick_timer = NULL;
    }

    if (lvgl_port_ctx.lvgl_mux) {
        vSemaphoreDelete(lvgl_port_ctx.lvgl_mux);
    }
    memset(&lvgl_port_ctx, 0, sizeof(lvgl_port_ctx));
#if LV_ENABLE_GC || !LV_MEM_CUSTOM
    /* Deinitialize LVGL */
    lv_deinit();
#endif
}

static void lvgl_port_tick_increment(void *arg)
{
    /* Tell LVGL how many milliseconds have elapsed */
    lv_tick_inc(lvgl_port_ctx.timer_period_ms);
}

static esp_err_t lvgl_port_tick_init(void)
{
    // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &lvgl_port_tick_increment,
        .name = "LVGL tick",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&lvgl_tick_timer_args, &lvgl_port_ctx.tick_timer), TAG, "Creating LVGL timer filed!");
    return esp_timer_start_periodic(lvgl_port_ctx.tick_timer, lvgl_port_ctx.timer_period_ms * 1000);
}
