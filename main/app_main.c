#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "kc_touch_gui.h"
#include "kc_touch_display.h"
#include "sensor_manager.h"
#include "yui_camera.h"
#include "yamui_logging.h"

static const char *TAG = "yamui_main";

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

    kc_touch_gui_show_root();
    ESP_LOGI(TAG, "Waveshare YamUI runtime started");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
