#include "kc_touch_gui.h"

#include <inttypes.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#ifndef CONFIG_KC_TOUCH_GUI_TASK_STACK_SIZE
#define CONFIG_KC_TOUCH_GUI_TASK_STACK_SIZE 8192
#endif

#ifndef CONFIG_KC_TOUCH_GUI_TASK_PRIORITY
#define CONFIG_KC_TOUCH_GUI_TASK_PRIORITY 5
#endif

#ifndef CONFIG_KC_TOUCH_GUI_TASK_PERIOD_MS
#define CONFIG_KC_TOUCH_GUI_TASK_PERIOD_MS 10
#endif

#ifndef CONFIG_KC_TOUCH_GUI_TICK_PERIOD_MS
#define CONFIG_KC_TOUCH_GUI_TICK_PERIOD_MS 5
#endif

#ifndef CONFIG_KC_TOUCH_GUI_WORK_QUEUE_LENGTH
#define CONFIG_KC_TOUCH_GUI_WORK_QUEUE_LENGTH 8
#endif

static const char *TAG = "kc_touch_gui";

kc_touch_gui_config_t kc_touch_gui_default_config(void)
{
    kc_touch_gui_config_t cfg = {
        .task_stack_size = CONFIG_KC_TOUCH_GUI_TASK_STACK_SIZE,
        .task_priority = CONFIG_KC_TOUCH_GUI_TASK_PRIORITY,
        .task_period_ms = CONFIG_KC_TOUCH_GUI_TASK_PERIOD_MS,
        .tick_period_ms = CONFIG_KC_TOUCH_GUI_TICK_PERIOD_MS,
        .work_queue_length = CONFIG_KC_TOUCH_GUI_WORK_QUEUE_LENGTH,
    };
    return cfg;
}

#if CONFIG_KC_TOUCH_GUI_ENABLE

typedef struct {
    kc_touch_gui_work_cb_t cb;
    void *ctx;
} kc_touch_gui_work_item_t;

typedef struct {
    kc_touch_gui_config_t cfg;
    QueueHandle_t queue;
    TaskHandle_t task;
    esp_timer_handle_t tick_timer;
    bool ready;
    volatile bool scanning;
    bool camera_ready;
    kc_touch_gui_prov_cb_t prov_cb;
    void *prov_ctx;
} kc_touch_gui_runtime_t;

static kc_touch_gui_runtime_t s_gui = {0};

void kc_touch_gui_set_provisioning_cb(kc_touch_gui_prov_cb_t cb, void *ctx)
{
    s_gui.prov_cb = cb;
    s_gui.prov_ctx = ctx;
}

void kc_touch_gui_trigger_provisioning(void)
{
    if (s_gui.prov_cb) {
        s_gui.prov_cb(s_gui.prov_ctx);
    } else {
        ESP_LOGW(TAG, "Provisioning triggered but no callback registered");
    }
}

void kc_touch_gui_set_scanning(bool scanning)
{
    s_gui.scanning = scanning;
}

bool kc_touch_gui_is_scanning(void)
{
    return s_gui.scanning;
}

void kc_touch_gui_set_camera_ready(bool ready)
{
    s_gui.camera_ready = ready;
}

bool kc_touch_gui_camera_ready(void)
{
    return s_gui.camera_ready;
}

static bool kc_touch_gui_validate_config(const kc_touch_gui_config_t *cfg)
{
    return cfg && cfg->task_stack_size >= 4096U &&
           cfg->task_period_ms >= 1U &&
           cfg->tick_period_ms >= 1U &&
           cfg->work_queue_length >= 2U;
}

static void kc_touch_gui_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(s_gui.cfg.tick_period_ms);
}

static void kc_touch_gui_task(void *arg)
{
    (void)arg;
    const TickType_t wait = pdMS_TO_TICKS(s_gui.cfg.task_period_ms);
    kc_touch_gui_work_item_t item;

    while (true) {
        if (xQueueReceive(s_gui.queue, &item, wait) == pdTRUE) {
            if (item.cb) {
                item.cb(item.ctx);
            }
            while (xQueueReceive(s_gui.queue, &item, 0) == pdTRUE) {
                if (item.cb) {
                    item.cb(item.ctx);
                }
            }
        }
        lv_timer_handler();
    }
}

static void kc_touch_gui_cleanup_partial(void)
{
    if (s_gui.tick_timer) {
        esp_timer_stop(s_gui.tick_timer);
        esp_timer_delete(s_gui.tick_timer);
        s_gui.tick_timer = NULL;
    }
    if (s_gui.queue) {
        vQueueDelete(s_gui.queue);
        s_gui.queue = NULL;
    }
    s_gui.task = NULL;
    s_gui.ready = false;
}

#include "lvgl_yaml_gui.h"

esp_err_t kc_touch_gui_init(const kc_touch_gui_config_t *config)
{
    if (s_gui.ready) {
        return ESP_OK;
    }

    kc_touch_gui_config_t cfg = config ? *config : kc_touch_gui_default_config();
    if (!kc_touch_gui_validate_config(&cfg)) {
        return ESP_ERR_INVALID_ARG;
    }
    s_gui.cfg = cfg;

    lv_init();

    s_gui.queue = xQueueCreate((UBaseType_t)s_gui.cfg.work_queue_length, sizeof(kc_touch_gui_work_item_t));
    if (s_gui.queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_timer_create_args_t timer_args = {
        .callback = kc_touch_gui_tick_cb,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "kc_gui_tick",
    };

    esp_err_t err = esp_timer_create(&timer_args, &s_gui.tick_timer);
    if (err != ESP_OK) {
        kc_touch_gui_cleanup_partial();
        return err;
    }

    err = esp_timer_start_periodic(s_gui.tick_timer, s_gui.cfg.tick_period_ms * 1000ULL);
    if (err != ESP_OK) {
        kc_touch_gui_cleanup_partial();
        return err;
    }

    BaseType_t created = xTaskCreatePinnedToCore(kc_touch_gui_task,
                                                 "kc_gui",
                                                 s_gui.cfg.task_stack_size,
                                                 NULL,
                                                 s_gui.cfg.task_priority,
                                                 &s_gui.task,
                                                 tskNO_AFFINITY);
    if (created != pdPASS) {
        kc_touch_gui_cleanup_partial();
        return ESP_FAIL;
    }

    s_gui.ready = true;
    ESP_LOGI(TAG, "LVGL core initialized (stack=%" PRIu32 ", period=%" PRIu32 " ms)",
             s_gui.cfg.task_stack_size, s_gui.cfg.task_period_ms);

    return ESP_OK;
}

esp_err_t kc_touch_gui_dispatch(kc_touch_gui_work_cb_t cb, void *ctx, TickType_t ticks_to_wait)
{
    if (!s_gui.ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (cb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    kc_touch_gui_work_item_t item = {
        .cb = cb,
        .ctx = ctx,
    };

    BaseType_t queued = xQueueSend(s_gui.queue, &item, ticks_to_wait);
    if (queued != pdPASS) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static void kc_touch_gui_build_ui(void *ctx)
{
    (void)ctx;
    esp_err_t err = lvgl_yaml_gui_load_default();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load YamUI bundle (%s)", esp_err_to_name(err));
    }
}

void kc_touch_gui_show_root(void)
{
    kc_touch_gui_dispatch(kc_touch_gui_build_ui, NULL, 0);
}

bool kc_touch_gui_is_ready(void)
{
    return s_gui.ready;
}

#else  /* CONFIG_KC_TOUCH_GUI_ENABLE */

esp_err_t kc_touch_gui_init(const kc_touch_gui_config_t *config)
{
    (void)config;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t kc_touch_gui_dispatch(kc_touch_gui_work_cb_t cb, void *ctx, TickType_t ticks_to_wait)
{
    (void)cb;
    (void)ctx;
    (void)ticks_to_wait;
    return ESP_ERR_NOT_SUPPORTED;
}

bool kc_touch_gui_is_ready(void)
{
    return false;
}

void kc_touch_gui_set_camera_ready(bool ready)
{
    (void)ready;
}

bool kc_touch_gui_camera_ready(void)
{
    return false;
}

#endif /* CONFIG_KC_TOUCH_GUI_ENABLE */
