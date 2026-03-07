#include "sdkconfig.h"

#if CONFIG_KC_TOUCH_DISPLAY_ENABLE && CONFIG_KC_TOUCH_DISPLAY_BACKEND_WAVESHARE_P4

#include "kc_touch_display_backend.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"

#if __has_include("bsp/esp32_p4_platform.h")
#include "bsp/esp32_p4_platform.h"
#define KC_TOUCH_WAVESHARE_BSP_AVAILABLE 1
#else
#define KC_TOUCH_WAVESHARE_BSP_AVAILABLE 0
#endif

static const char *TAG = "kc_ws_p4";
static bool s_ready;
static esp_lcd_panel_handle_t s_panel;

esp_err_t kc_touch_display_backend_init_hw(void)
{
    if (s_ready) {
        return ESP_OK;
    }

#if KC_TOUCH_WAVESHARE_BSP_AVAILABLE
    /* Use Waveshare BSP helper for ESP32-P4 10.1 DSI panel init. */
    esp_err_t err = bsp_display_new_with_handles(NULL, &s_panel, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bsp_display_new_with_handles failed: %s", esp_err_to_name(err));
        return err;
    }
    if (!s_panel) {
        ESP_LOGE(TAG, "Waveshare BSP did not return a panel handle");
        return ESP_ERR_INVALID_STATE;
    }
    (void)esp_lcd_panel_disp_on_off(s_panel, true);
    ESP_LOGI(TAG, "Waveshare P4 panel initialized via BSP");
#else
    ESP_LOGE(TAG, "Waveshare BSP header missing. Add dependency waveshare/esp32_p4_platform.");
    return ESP_ERR_NOT_SUPPORTED;
#endif

    s_ready = true;
    return ESP_OK;
}

esp_err_t kc_touch_display_backend_flush(int32_t x1, int32_t y1, int32_t x2, int32_t y2, const void *color_data)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_panel || !color_data) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, x1, y1, x2 + 1, y2 + 1, color_data);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "panel draw failed: %s", esp_err_to_name(err));
    }
    return err;
}

bool kc_touch_display_backend_touch_sample(uint16_t *x, uint16_t *y)
{
    (void)x;
    (void)y;
    /* Touch controller wiring/driver is handled in the next step. */
    return false;
}

esp_err_t kc_touch_display_backend_backlight_set(bool enable)
{
#if KC_TOUCH_WAVESHARE_BSP_AVAILABLE
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    /* Keep brightness simple for now; 0=off, 100=on. */
    int percent = enable ? 100 : 0;
    return bsp_display_brightness_set(percent);
#else
    (void)enable;
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
#endif
}

#endif // CONFIG_KC_TOUCH_DISPLAY_ENABLE && CONFIG_KC_TOUCH_DISPLAY_BACKEND_WAVESHARE_P4
