#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** GUI task configuration */
typedef struct {
    uint32_t task_stack_size;   /**< GUI FreeRTOS task stack size in bytes */
    UBaseType_t task_priority;  /**< GUI FreeRTOS task priority */
    uint32_t task_period_ms;    /**< Period in milliseconds for lv_timer_handler */
    uint32_t tick_period_ms;    /**< Period in milliseconds for lv_tick_inc */
    uint32_t work_queue_length; /**< Pending work items that fit in the GUI queue */
} kc_touch_gui_config_t;

typedef void (*kc_touch_gui_work_cb_t)(void *ctx);

kc_touch_gui_config_t kc_touch_gui_default_config(void);

esp_err_t kc_touch_gui_init(const kc_touch_gui_config_t *config);

esp_err_t kc_touch_gui_dispatch(kc_touch_gui_work_cb_t cb, void *ctx, TickType_t ticks_to_wait);

bool kc_touch_gui_is_ready(void);

#ifdef __cplusplus
}
#endif
