#include "sdkconfig.h"

#if CONFIG_KC_TOUCH_DISPLAY_ENABLE && CONFIG_KC_TOUCH_DISPLAY_BACKEND_WAVESHARE_P4

#include "kc_touch_display_backend.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#if __has_include("esp_lcd_touch.h")
#include "esp_lcd_touch.h"
#define KC_TOUCH_ESP_LCD_TOUCH_AVAILABLE 1
#else
#define KC_TOUCH_ESP_LCD_TOUCH_AVAILABLE 0
#endif

#if __has_include("bsp/esp32_p4_platform.h")
#include "bsp/esp32_p4_platform.h"
#define KC_TOUCH_WAVESHARE_BSP_AVAILABLE 1
#else
#define KC_TOUCH_WAVESHARE_BSP_AVAILABLE 0
#endif

static const char *TAG = "kc_ws_p4";
static bool s_ready;
static esp_lcd_panel_handle_t s_panel;
#if KC_TOUCH_ESP_LCD_TOUCH_AVAILABLE
static esp_lcd_touch_handle_t s_touch;
#endif

#ifndef CONFIG_KC_TOUCH_DISPLAY_WIDTH
#define CONFIG_KC_TOUCH_DISPLAY_WIDTH 1280
#endif

#ifndef CONFIG_KC_TOUCH_DISPLAY_HEIGHT
#define CONFIG_KC_TOUCH_DISPLAY_HEIGHT 800
#endif

esp_err_t kc_touch_display_backend_init_hw(void)
{
    if (s_ready) {
        return ESP_OK;
    }

#if KC_TOUCH_WAVESHARE_BSP_AVAILABLE
    /* Use Waveshare BSP helper for ESP32-P4 10.1 DSI panel init. */
#if KC_TOUCH_ESP_LCD_TOUCH_AVAILABLE
    esp_err_t err = bsp_display_new_with_handles(NULL, &s_panel, &s_touch);
#else
    esp_err_t err = bsp_display_new_with_handles(NULL, &s_panel, NULL);
#endif
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bsp_display_new_with_handles failed: %s", esp_err_to_name(err));
        return err;
    }
    if (!s_panel) {
        ESP_LOGE(TAG, "Waveshare BSP did not return a panel handle");
        return ESP_ERR_INVALID_STATE;
    }
    (void)esp_lcd_panel_disp_on_off(s_panel, true);
#if KC_TOUCH_ESP_LCD_TOUCH_AVAILABLE
    ESP_LOGI(TAG, "Waveshare P4 panel initialized via BSP (touch %s)", s_touch ? "ready" : "not detected");
#else
    ESP_LOGI(TAG, "Waveshare P4 panel initialized via BSP (touch API unavailable)");
#endif
    ESP_LOGI(TAG, "Configured LVGL resolution: %dx%d", CONFIG_KC_TOUCH_DISPLAY_WIDTH, CONFIG_KC_TOUCH_DISPLAY_HEIGHT);
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
#if KC_TOUCH_ESP_LCD_TOUCH_AVAILABLE
    if (!s_ready || !s_touch || !x || !y) {
        return false;
    }
    esp_lcd_touch_read_data(s_touch);
    uint16_t x_pos[1] = {0};
    uint16_t y_pos[1] = {0};
    uint16_t strength[1] = {0};
    uint8_t point_count = 0;
    bool pressed = esp_lcd_touch_get_coordinates(
        s_touch,
        x_pos,
        y_pos,
        strength,
        &point_count,
        1
    );
    if (!pressed || point_count == 0) {
        return false;
    }
    *x = x_pos[0];
    *y = y_pos[0];
    return true;
#else
    (void)x;
    (void)y;
    return false;
#endif
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
