#include "kc_touch_camera.h"

#include <esp_log.h>

static const char *TAG = "kc_camera";
static bool s_camera_ready;

extern "C" esp_err_t kc_touch_camera_init(void)
{
    if (s_camera_ready) {
        return ESP_OK;
    }
    ESP_LOGW(TAG, "Camera init is not implemented for the active hardware backend");
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t kc_touch_camera_deinit(void)
{
    if (!s_camera_ready) {
        return ESP_OK;
    }
    s_camera_ready = false;
    return ESP_OK;
}

extern "C" bool kc_touch_camera_ready(void)
{
    return s_camera_ready;
}
