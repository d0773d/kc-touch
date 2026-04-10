#include "sdkconfig.h"

#if CONFIG_KC_TOUCH_DISPLAY_ENABLE && CONFIG_KC_TOUCH_DISPLAY_BACKEND_WAVESHARE_P4

#include "kc_touch_display_backend.h"

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lv_adapter.h"
#include "soc/soc_caps.h"
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

#if __has_include("bsp/touch.h")
#include "bsp/touch.h"
#define KC_TOUCH_WAVESHARE_BSP_TOUCH_API_AVAILABLE 1
#else
#define KC_TOUCH_WAVESHARE_BSP_TOUCH_API_AVAILABLE 0
#endif

#if __has_include("esp_lcd_jd9365.h")
#include "esp_lcd_jd9365.h"
#define KC_TOUCH_WAVESHARE_JD9365_COMPONENT_AVAILABLE 1
#elif __has_include("esp_lcd_jd9365_10_1.h")
#include "esp_lcd_jd9365_10_1.h"
#define KC_TOUCH_WAVESHARE_JD9365_COMPONENT_AVAILABLE 1
#else
#define KC_TOUCH_WAVESHARE_JD9365_COMPONENT_AVAILABLE 0
#endif

#if KC_TOUCH_WAVESHARE_JD9365_COMPONENT_AVAILABLE
#include "esp_lcd_mipi_dsi.h"
#include "esp_ldo_regulator.h"
#include "driver/gpio.h"
#endif

static const char *TAG = "kc_ws_p4";
static bool s_ready;
static esp_lcd_panel_handle_t s_panel;
static bool s_bsp_lvgl_active;
#if KC_TOUCH_ESP_LCD_TOUCH_AVAILABLE
static esp_lcd_touch_handle_t s_touch;
#endif
#if KC_TOUCH_WAVESHARE_JD9365_COMPONENT_AVAILABLE
static esp_lcd_dsi_bus_handle_t s_mipi_dsi_bus;
static esp_lcd_panel_io_handle_t s_mipi_dbi_io;
static esp_ldo_channel_handle_t s_mipi_ldo_chan;
#endif
static bool s_backlight_ready;

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
    /* Prefer Waveshare BSP LVGL adapter path (same flow as vendor examples). */
    bsp_display_cfg_t display_cfg = {
        .lv_adapter_cfg = {
            .task_stack_size   = CONFIG_KC_TOUCH_GUI_TASK_STACK_SIZE,
            .task_priority     = ESP_LV_ADAPTER_DEFAULT_TASK_PRIORITY,
            .task_core_id      = ESP_LV_ADAPTER_DEFAULT_TASK_CORE_ID,
            .tick_period_ms    = ESP_LV_ADAPTER_DEFAULT_TICK_PERIOD_MS,
            .task_min_delay_ms = ESP_LV_ADAPTER_DEFAULT_TASK_MIN_DELAY_MS,
            .task_max_delay_ms = ESP_LV_ADAPTER_DEFAULT_TASK_MAX_DELAY_MS,
            .stack_in_psram    = true,
        },
        .rotation = ESP_LV_ADAPTER_ROTATE_0,
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL,
        .touch_flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    lv_display_t *disp = bsp_display_start_with_config(&display_cfg);
    if (!disp) {
        ESP_LOGE(TAG, "bsp_display_start_with_config failed");
        return ESP_FAIL;
    }
    esp_err_t err = bsp_display_backlight_on();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "bsp_display_backlight_on failed: %s", esp_err_to_name(err));
    }
    s_panel = bsp_display_get_panel_handle();
    s_bsp_lvgl_active = true;
    s_touch = NULL;
    ESP_LOGI(TAG, "Waveshare P4 panel initialized via BSP LVGL adapter");
    ESP_LOGI(TAG, "Configured LVGL resolution: %dx%d", CONFIG_KC_TOUCH_DISPLAY_WIDTH, CONFIG_KC_TOUCH_DISPLAY_HEIGHT);
#elif KC_TOUCH_WAVESHARE_JD9365_COMPONENT_AVAILABLE && SOC_MIPI_DSI_SUPPORTED
    if (CONFIG_KC_TOUCH_WAVESHARE_BACKLIGHT_GPIO >= 0) {
        gpio_config_t bk_gpio_config = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = (1ULL << CONFIG_KC_TOUCH_WAVESHARE_BACKLIGHT_GPIO),
        };
        esp_err_t err = gpio_config(&bk_gpio_config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "backlight gpio init failed: %s", esp_err_to_name(err));
            return err;
        }
        (void)gpio_set_level(CONFIG_KC_TOUCH_WAVESHARE_BACKLIGHT_GPIO, 1);
        s_backlight_ready = true;
    }

    esp_ldo_channel_config_t ldo_mipi_phy_config = {
        .chan_id = CONFIG_KC_TOUCH_WAVESHARE_MIPI_LDO_CHANNEL,
        .voltage_mv = CONFIG_KC_TOUCH_WAVESHARE_MIPI_LDO_MV,
    };
    esp_err_t err = esp_ldo_acquire_channel(&ldo_mipi_phy_config, &s_mipi_ldo_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MIPI PHY LDO acquire failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_lcd_dsi_bus_config_t bus_config = JD9365_PANEL_BUS_DSI_2CH_CONFIG();
    err = esp_lcd_new_dsi_bus(&bus_config, &s_mipi_dsi_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_dsi_bus failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_lcd_dbi_io_config_t dbi_config = JD9365_PANEL_IO_DBI_CONFIG();
    err = esp_lcd_new_panel_io_dbi(s_mipi_dsi_bus, &dbi_config, &s_mipi_dbi_io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_io_dbi failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_lcd_dpi_panel_config_t dpi_config = JD9365_800_1280_PANEL_60HZ_DPI_CONFIG(LCD_COLOR_PIXEL_FORMAT_RGB888);
    jd9365_vendor_config_t vendor_config = {
        .flags = {
            .use_mipi_interface = 1,
        },
        .mipi_config = {
            .dsi_bus = s_mipi_dsi_bus,
            .dpi_config = &dpi_config,
            .lane_num = 2,
        },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = CONFIG_KC_TOUCH_WAVESHARE_LCD_RST_GPIO,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 24,
        .vendor_config = &vendor_config,
    };
    err = esp_lcd_new_panel_jd9365(s_mipi_dbi_io, &panel_config, &s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_jd9365 failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "panel on failed");
    ESP_LOGI(TAG, "Waveshare P4 panel initialized via jd9365 component (touch TBD)");
    ESP_LOGI(TAG, "Configured LVGL resolution: %dx%d", CONFIG_KC_TOUCH_DISPLAY_WIDTH, CONFIG_KC_TOUCH_DISPLAY_HEIGHT);
#else
    ESP_LOGE(TAG, "No Waveshare display backend available. Add waveshare/esp32_p4_platform or waveshare/esp_lcd_jd9365.");
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
    if (s_bsp_lvgl_active) {
        (void)x1;
        (void)y1;
        (void)x2;
        (void)y2;
        (void)color_data;
        return ESP_OK;
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
    if (s_bsp_lvgl_active) {
        (void)x;
        (void)y;
        return false;
    }
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
#elif KC_TOUCH_WAVESHARE_JD9365_COMPONENT_AVAILABLE
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_backlight_ready || CONFIG_KC_TOUCH_WAVESHARE_BACKLIGHT_GPIO < 0) {
        return ESP_OK;
    }
    return gpio_set_level(CONFIG_KC_TOUCH_WAVESHARE_BACKLIGHT_GPIO, enable ? 1 : 0);
#else
    (void)enable;
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
#endif
}

esp_err_t kc_touch_display_backend_brightness_set(int percent)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    if (percent < 0) {
        percent = 0;
    } else if (percent > 100) {
        percent = 100;
    }

#if KC_TOUCH_WAVESHARE_BSP_AVAILABLE
    return bsp_display_brightness_set(percent);
#elif KC_TOUCH_WAVESHARE_JD9365_COMPONENT_AVAILABLE
    if (!s_backlight_ready || CONFIG_KC_TOUCH_WAVESHARE_BACKLIGHT_GPIO < 0) {
        return ESP_OK;
    }
    return gpio_set_level(CONFIG_KC_TOUCH_WAVESHARE_BACKLIGHT_GPIO, percent > 0 ? 1 : 0);
#else
    return ESP_OK;
#endif
}

#endif // CONFIG_KC_TOUCH_DISPLAY_ENABLE && CONFIG_KC_TOUCH_DISPLAY_BACKEND_WAVESHARE_P4
