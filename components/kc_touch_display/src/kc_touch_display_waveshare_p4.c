#include "sdkconfig.h"

#if CONFIG_KC_TOUCH_DISPLAY_ENABLE && CONFIG_KC_TOUCH_DISPLAY_BACKEND_WAVESHARE_P4

#include "kc_touch_display_backend.h"

#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "kc_ws_p4";
static bool s_ready;

esp_err_t kc_touch_display_backend_init_hw(void)
{
    /* Waveshare ESP32-P4 board backend scaffold.
     * TODO: Wire esp_lcd panel + touch init for your exact display variant.
     */
    s_ready = true;
    ESP_LOGW(TAG, "Waveshare P4 display backend scaffold active (panel init TODO)");
    return ESP_OK;
}

esp_err_t kc_touch_display_backend_flush(int32_t x1, int32_t y1, int32_t x2, int32_t y2, const void *color_data)
{
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    (void)color_data;
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    /* No-op until esp_lcd flush is wired. */
    return ESP_OK;
}

bool kc_touch_display_backend_touch_sample(uint16_t *x, uint16_t *y)
{
    (void)x;
    (void)y;
    return false;
}

esp_err_t kc_touch_display_backend_backlight_set(bool enable)
{
    (void)enable;
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

#endif // CONFIG_KC_TOUCH_DISPLAY_ENABLE && CONFIG_KC_TOUCH_DISPLAY_BACKEND_WAVESHARE_P4

