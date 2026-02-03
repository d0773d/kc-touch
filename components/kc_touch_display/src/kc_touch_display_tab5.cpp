
#include <inttypes.h>

#include "sdkconfig.h"

#if CONFIG_KC_TOUCH_DISPLAY_ENABLE

#include "kc_touch_display_tab5.h"

#include "esp_log.h"
#include "M5Unified.hpp"

static const char *TAG = "kc_tab5";
static bool s_tab5_ready;
static uint8_t s_tab5_prev_brightness = 255;

static inline uint16_t clamp_coord(int32_t value, int32_t max_value)
{
    if (value < 0) {
        return 0;
    }
    if (value > max_value) {
        return (uint16_t)max_value;
    }
    return (uint16_t)value;
}

esp_err_t kc_touch_tab5_init_hw(void)
{
    if (s_tab5_ready) {
        return ESP_OK;
    }

    auto cfg = M5.config();
    cfg.clear_display = true;
    cfg.output_power = true;
    cfg.internal_mic = false;
    cfg.internal_spk = false;
    cfg.internal_imu = false;
    cfg.internal_rtc = false;
    cfg.disable_rtc_irq = true;
    cfg.external_speaker_value = 0;
    cfg.external_display_value = 0;
    cfg.fallback_board = m5::board_t::board_M5Tab5;

    M5.begin(cfg);
    M5.Display.setRotation(0);
    auto brightness = M5.Display.getBrightness();
    if (brightness == 0) {
        brightness = s_tab5_prev_brightness;
    }
    M5.Display.setBrightness(brightness);
    s_tab5_prev_brightness = brightness;
    M5.Display.clearDisplay();

    s_tab5_ready = true;
    ESP_LOGI(TAG, "M5 Tab5 display online (%" PRId32 " x %" PRId32 ")",
             static_cast<int32_t>(M5.Display.width()),
             static_cast<int32_t>(M5.Display.height()));
    return ESP_OK;
}

esp_err_t kc_touch_tab5_flush(int32_t x1, int32_t y1, int32_t x2, int32_t y2, const void *color_data)
{
    if (!s_tab5_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!color_data) {
        return ESP_ERR_INVALID_ARG;
    }

    const int32_t w = x2 - x1 + 1;
    const int32_t h = y2 - y1 + 1;
    if (w <= 0 || h <= 0) {
        return ESP_OK;
    }

    const uint16_t *pixels = static_cast<const uint16_t *>(color_data);
    M5.Display.startWrite();
    M5.Display.pushImage(x1, y1, w, h, pixels);
    M5.Display.endWrite();
    return ESP_OK;
}

bool kc_touch_tab5_touch_sample(uint16_t *x, uint16_t *y)
{
    if (!s_tab5_ready || !x || !y) {
        return false;
    }

    M5.update();
    if (M5.Touch.getCount() == 0) {
        return false;
    }

    auto detail = M5.Touch.getDetail();
    if (!detail.isPressed()) {
        return false;
    }

    *x = clamp_coord(detail.x, M5.Display.width() - 1);
    *y = clamp_coord(detail.y, M5.Display.height() - 1);
    return true;
}

esp_err_t kc_touch_tab5_backlight_set(bool enable)
{
    if (!s_tab5_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    if (enable) {
        if (s_tab5_prev_brightness == 0) {
            s_tab5_prev_brightness = 255;
        }
        M5.Display.setBrightness(s_tab5_prev_brightness);
    } else {
        s_tab5_prev_brightness = M5.Display.getBrightness();
        M5.Display.setBrightness(0);
    }
    return ESP_OK;
}

#endif // CONFIG_KC_TOUCH_DISPLAY_ENABLE
