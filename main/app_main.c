#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "kc_touch_gui.h"
#include "kc_touch_display.h"
#include "sensor_manager.h"
#include "yui_camera.h"
#include "yamui_async.h"
#include "yamui_logging.h"
#include "yamui_runtime.h"
#include "yamui_state.h"

static const char *TAG = "yamui_main";

typedef struct {
    char operation[32];
    int32_t progress;
    bool increment_sync_count;
    bool mark_complete;
    bool mark_failed;
    char message[64];
} yui_demo_sync_ctx_t;

static void yui_demo_sync_apply(void *arg)
{
    yui_demo_sync_ctx_t *ctx = (yui_demo_sync_ctx_t *)arg;
    if (!ctx) {
        return;
    }
    const char *operation = ctx->operation[0] != '\0' ? ctx->operation : "sync_demo";

    if (ctx->mark_complete) {
        if (ctx->increment_sync_count) {
            (void)yui_state_set_int("ui.sync_count", yui_state_get_int("ui.sync_count", 0) + 1);
        }
        (void)yamui_async_complete(operation, ctx->message);
        free(ctx);
        return;
    }

    if (ctx->mark_failed) {
        (void)yamui_async_fail(operation, ctx->message);
        free(ctx);
        return;
    }

    if (ctx->progress <= 0) {
        (void)yamui_async_begin(operation, ctx->message);
    } else {
        (void)yamui_async_progress(operation, ctx->progress, ctx->message);
    }
    free(ctx);
}

static bool yui_demo_sync_dispatch_step(const char *operation,
                                        int32_t progress,
                                        const char *message,
                                        bool mark_complete,
                                        bool increment_sync_count,
                                        bool mark_failed)
{
    yui_demo_sync_ctx_t *ctx = (yui_demo_sync_ctx_t *)calloc(1, sizeof(yui_demo_sync_ctx_t));
    if (!ctx) {
        return false;
    }
    snprintf(ctx->operation, sizeof(ctx->operation), "%s", operation ? operation : "sync_demo");
    ctx->progress = progress;
    ctx->mark_complete = mark_complete;
    ctx->increment_sync_count = increment_sync_count;
    ctx->mark_failed = mark_failed;
    snprintf(ctx->message, sizeof(ctx->message), "%s", message ? message : "");

    esp_err_t err = kc_touch_gui_dispatch(yui_demo_sync_apply, ctx, pdMS_TO_TICKS(250));
    if (err != ESP_OK) {
        free(ctx);
        return false;
    }
    return true;
}

static void yui_demo_sync_task(void *arg)
{
    yui_demo_sync_ctx_t *ctx = (yui_demo_sync_ctx_t *)arg;
    const char *operation = (ctx && ctx->operation[0] != '\0') ? ctx->operation : "sync_demo";

    if (!yui_demo_sync_dispatch_step(operation, 0, "status.sync_starting", false, false, false)) {
        goto fail;
    }
    vTaskDelay(pdMS_TO_TICKS(350));

    if (!yui_demo_sync_dispatch_step(operation, 50, "status.sync_preparing", false, false, false)) {
        goto fail;
    }
    vTaskDelay(pdMS_TO_TICKS(450));

    if (!yui_demo_sync_dispatch_step(operation, 90, "status.sync_finalizing", false, false, false)) {
        goto fail;
    }
    vTaskDelay(pdMS_TO_TICKS(350));

    (void)yui_demo_sync_dispatch_step(operation, 100, "status.sync_done", true, true, false);

    free(ctx);
    vTaskDelete(NULL);
    return;

fail:
    (void)yui_demo_sync_dispatch_step(operation, 0, "status.sync_failed", false, false, true);
    free(ctx);
    vTaskDelete(NULL);
}

static void yui_native_fn_demo_async_sync(int argc, const char **argv)
{
    const char *operation = (argc > 0 && argv && argv[0] && argv[0][0] != '\0') ? argv[0] : "sync_demo";
    if (yui_state_get_bool("async.sync_demo.running", false) && strcmp(operation, "sync_demo") == 0) {
        return;
    }

    yui_demo_sync_ctx_t *ctx = (yui_demo_sync_ctx_t *)calloc(1, sizeof(yui_demo_sync_ctx_t));
    if (!ctx) {
        (void)yamui_async_fail(operation, "status.sync_failed");
        return;
    }
    snprintf(ctx->operation, sizeof(ctx->operation), "%s", operation);

    BaseType_t created = xTaskCreate(yui_demo_sync_task,
                                     "yui_async_sync",
                                     4096,
                                     ctx,
                                     tskIDLE_PRIORITY + 1,
                                     NULL);
    if (created != pdPASS) {
        free(ctx);
        (void)yamui_async_fail(operation, "status.sync_failed");
    }
}

static void app_register_yamui_demo_functions(void)
{
    (void)yamui_runtime_register_function("demo_async_sync", yui_native_fn_demo_async_sync);
}

void app_main(void)
{
    yamui_set_log_level(YAMUI_LOG_LEVEL_DEBUG);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(ret);
    }

    kc_touch_gui_config_t gui_cfg = kc_touch_gui_default_config();
    ESP_ERROR_CHECK(kc_touch_gui_init(&gui_cfg));
    ESP_ERROR_CHECK(kc_touch_display_init());
    ESP_ERROR_CHECK(kc_touch_display_backlight_set(true));

    esp_err_t sensor_err = sensor_manager_init();
    if (sensor_err != ESP_OK) {
        ESP_LOGW(TAG, "sensor_manager_init: %s", esp_err_to_name(sensor_err));
    }

    esp_err_t camera_err = yui_camera_init();
    if (camera_err == ESP_OK) {
        kc_touch_gui_set_camera_ready(true);
        ESP_LOGI(TAG, "yui_camera_init: ready");
    } else if (camera_err != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "yui_camera_init: %s", esp_err_to_name(camera_err));
    } else {
        ESP_LOGI(TAG, "yui_camera_init: not enabled/supported");
    }

    app_register_yamui_demo_functions();
    kc_touch_gui_show_root();
    ESP_LOGI(TAG, "Waveshare YamUI runtime started");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
