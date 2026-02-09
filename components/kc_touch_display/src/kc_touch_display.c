#include "kc_touch_display.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "kc_touch_display_tab5.h"
#include "kc_touch_gui.h"
#include "lvgl.h"
#include "extra/libs/qrcode/lv_qrcode.h"
#include "sdkconfig.h"

#if CONFIG_KC_TOUCH_DISPLAY_ENABLE

#define DISPLAY_WIDTH  CONFIG_KC_TOUCH_DISPLAY_WIDTH
#define DISPLAY_HEIGHT CONFIG_KC_TOUCH_DISPLAY_HEIGHT
#define BUFFER_LINES   CONFIG_KC_TOUCH_DISPLAY_BUFFER_LINES
#define BUFFER_PIXELS  (DISPLAY_WIDTH * BUFFER_LINES)

_Static_assert(BUFFER_LINES <= DISPLAY_HEIGHT, "LVGL buffer must not exceed panel height");

static const char *TAG = "kc_touch_display";

static lv_disp_t *s_lv_display;
static lv_disp_draw_buf_t s_lv_draw_buf;
static lv_disp_drv_t s_lv_disp_drv;
static bool s_display_ready;
static lv_color_t s_lv_buf_a[BUFFER_PIXELS];
static lv_color_t s_lv_buf_b[BUFFER_PIXELS];
static kc_touch_display_prov_cb_t s_prov_cb;
static void *s_prov_ctx;
static kc_touch_display_cancel_cb_t s_cancel_cb = NULL;
static void *s_cancel_ctx = NULL;
static lv_obj_t *s_status_label;
static lv_obj_t *s_prov_back_btn = NULL;

#if CONFIG_KC_TOUCH_TOUCH_ENABLE
static lv_indev_t *s_touch_indev;
static lv_indev_drv_t s_touch_drv;
static bool s_touch_ready;
#endif

static lv_disp_rot_t kc_touch_display_rotation(void)
{
#if defined(CONFIG_KC_TOUCH_DISPLAY_ROTATION_0) && CONFIG_KC_TOUCH_DISPLAY_ROTATION_0
    return LV_DISP_ROT_NONE;
#elif defined(CONFIG_KC_TOUCH_DISPLAY_ROTATION_90) && CONFIG_KC_TOUCH_DISPLAY_ROTATION_90
    return LV_DISP_ROT_90;
#elif defined(CONFIG_KC_TOUCH_DISPLAY_ROTATION_180) && CONFIG_KC_TOUCH_DISPLAY_ROTATION_180
    return LV_DISP_ROT_180;
#elif defined(CONFIG_KC_TOUCH_DISPLAY_ROTATION_270) && CONFIG_KC_TOUCH_DISPLAY_ROTATION_270
    return LV_DISP_ROT_270;
#else
    return LV_DISP_ROT_NONE;
#endif
}



static void kc_touch_display_flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    if (!area || !color_p) {
        lv_disp_flush_ready(disp_drv);
        return;
    }
    if (area->x2 < area->x1 || area->y2 < area->y1) {
        lv_disp_flush_ready(disp_drv);
        return;
    }
    esp_err_t err = kc_touch_tab5_flush(area->x1, area->y1, area->x2, area->y2, color_p);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Panel flush failed (%s)", esp_err_to_name(err));
    }
    lv_disp_flush_ready(disp_drv);
}

static void kc_touch_display_register_lvgl(void *ctx)
{
    (void)ctx;
    lv_disp_draw_buf_init(&s_lv_draw_buf, s_lv_buf_a, s_lv_buf_b, BUFFER_PIXELS);
    lv_disp_drv_init(&s_lv_disp_drv);
    s_lv_disp_drv.flush_cb = kc_touch_display_flush_cb;
    s_lv_disp_drv.draw_buf = &s_lv_draw_buf;

    // Handle rotation by swapping resolutions and disabling LVGL software rotation
    // This assumes the underlying hardware/driver (M5GFX) is already rotated.
    lv_disp_rot_t rot = kc_touch_display_rotation();
    if (rot == LV_DISP_ROT_90 || rot == LV_DISP_ROT_270) {
        s_lv_disp_drv.hor_res = DISPLAY_HEIGHT;
        s_lv_disp_drv.ver_res = DISPLAY_WIDTH;
    } else {
        s_lv_disp_drv.hor_res = DISPLAY_WIDTH;
        s_lv_disp_drv.ver_res = DISPLAY_HEIGHT;
    }

    s_lv_display = lv_disp_drv_register(&s_lv_disp_drv);
    
    // Force NONE because we handled rotation via resolution swap and M5GFX
    lv_disp_set_rotation(s_lv_display, LV_DISP_ROT_NONE);
    
    // We do NOT build the default scene here anymore, because kc_touch_gui
    // is responsible for launching the main application UI (ui_root).
    // kc_touch_display_build_scene(NULL); 
}

#if CONFIG_KC_TOUCH_TOUCH_ENABLE
static bool kc_touch_touch_sample(uint16_t *x, uint16_t *y)
{
    if (!x || !y) {
        return false;
    }
    return kc_touch_tab5_touch_sample(x, y);
}

static void kc_touch_touch_read_cb(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    (void)indev_drv;
    uint16_t x = 0;
    uint16_t y = 0;
    if (kc_touch_touch_sample(&x, &y)) {
        // ESP_LOGI(TAG, "Touch: x=%d, y=%d", x, y);
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void kc_touch_touch_register_lvgl(void *ctx)
{
    (void)ctx;
    if (!s_lv_display) {
        return;
    }
    lv_indev_drv_init(&s_touch_drv);
    s_touch_drv.type = LV_INDEV_TYPE_POINTER;
    s_touch_drv.read_cb = kc_touch_touch_read_cb;
    s_touch_indev = lv_indev_drv_register(&s_touch_drv);
}

static esp_err_t kc_touch_touch_init(void)
{
    esp_err_t err = kc_touch_gui_dispatch(kc_touch_touch_register_lvgl, NULL, pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        return err;
    }
    s_touch_ready = true;
    ESP_LOGI(TAG, "M5 Tab5 touch input initialized");
    return ESP_OK;
}
#endif // CONFIG_KC_TOUCH_TOUCH_ENABLE

esp_err_t kc_touch_display_init(void)
{
    if (s_display_ready) {
        return ESP_OK;
    }

    if (!kc_touch_gui_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(kc_touch_tab5_init_hw(), TAG, "tab5 init");
    ESP_RETURN_ON_ERROR(kc_touch_gui_dispatch(kc_touch_display_register_lvgl, NULL, pdMS_TO_TICKS(200)), TAG, "lvgl disp");

#if CONFIG_KC_TOUCH_TOUCH_ENABLE
    esp_err_t touch_err = kc_touch_touch_init();
    if (touch_err != ESP_OK) {
        ESP_LOGW(TAG, "Touch init skipped (%s)", esp_err_to_name(touch_err));
    }
#endif

    s_display_ready = true;
    ESP_LOGI(TAG, "LVGL display registered (%dx%d)", DISPLAY_WIDTH, DISPLAY_HEIGHT);
    return ESP_OK;
}

esp_err_t kc_touch_display_backlight_set(bool enable)
{
    return kc_touch_tab5_backlight_set(enable);
}

esp_err_t kc_touch_display_set_provisioning_cb(kc_touch_display_prov_cb_t cb, void *ctx)
{
    s_prov_cb = cb;
    s_prov_ctx = ctx;
    return ESP_OK;
}

esp_err_t kc_touch_display_set_cancel_cb(kc_touch_display_cancel_cb_t cb, void *ctx)
{
    s_cancel_cb = cb;
    s_cancel_ctx = ctx;
    return ESP_OK;
}

static void kc_touch_display_enable_back_task(void *ctx) {
    bool enable = (bool)ctx;
    if (s_prov_back_btn && lv_obj_is_valid(s_prov_back_btn)) {
        if (enable) {
            lv_obj_clear_state(s_prov_back_btn, LV_STATE_DISABLED);
            lv_obj_set_style_bg_color(s_prov_back_btn, lv_color_hex(0x888888), 0); // Restore color
        } else {
            lv_obj_add_state(s_prov_back_btn, LV_STATE_DISABLED);
            lv_obj_set_style_bg_color(s_prov_back_btn, lv_color_hex(0x444444), 0); // Dim color
        }
    }
}

esp_err_t kc_touch_display_prov_enable_back(bool enable) {
    return kc_touch_gui_dispatch(kc_touch_display_enable_back_task, (void*)enable, 0);
}

static void on_prov_back_click(lv_event_t *e) {
    if (s_cancel_cb) {
        s_cancel_cb(s_cancel_ctx);
    } else {
        // Fallback if no callback registered
        kc_touch_gui_show_root();
    }
}

static void kc_touch_display_show_qr_task(void *ctx)
{
    char *payload = (char *)ctx;
    lv_obj_t * lcd_scr = lv_scr_act();
    lv_obj_clean(lcd_scr);
    
    // Reset layout properties that might persist from Previous UI (e.g. Flex Row)
    // In LVGL 8.3+, there isn't a direct "NONE" enum for flex flow, 
    // but we can reset style flex flow or just not adding layout styles.
    // However, if the object style had flex flow, we need to remove it.
    // The safest way is to reset the style or manually remove the flex flag/style.
    // Assuming we added flex flow via style in ui_root.c, cleaning the object (children) 
    // doesn't remove the style from 'lcd_scr' itself if it was applied to the screen object.
    
    // We can force empty style or remove specific style properties.
    // Or we can just create a confusing layout if we don't fix it.
    // The simplest way to "disable" flex layout is to set it to a dummy value or clear the layout flag.
    // But lv_obj_set_flex_flow wraps adding a style.
    
    // Let's reset the style of the screen completely to be safe.
    lv_obj_remove_style_all(lcd_scr);
    
    // Re-apply basic screen style if needed (bg color etc), but for now default is white/black which is fine.
    // Or just set the background explicitly.
    lv_obj_set_style_bg_color(lcd_scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(lcd_scr, LV_OPA_COVER, 0);

    // Scale QR code for 720px wide screen
    int qr_size = 400;
    if (DISPLAY_WIDTH < 480 || DISPLAY_HEIGHT < 480) qr_size = 200; // Constrain for smaller screens

    lv_obj_t * qr = lv_qrcode_create(lcd_scr, qr_size, lv_color_black(), lv_color_white());
    lv_qrcode_update(qr, payload, strlen(payload));
    lv_obj_center(qr);
    lv_obj_align(qr, LV_ALIGN_CENTER, 0, -30); // Shift up slightly to leave room for text

    lv_obj_t * label = lv_label_create(lcd_scr);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
    lv_label_set_text(label, "Scan QR Code with App");
    lv_obj_set_width(label, LV_PCT(90));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_align_to(label, qr, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);

    // Re-create the status label so set_status calls don't crash or fail
    s_status_label = lv_label_create(lcd_scr);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_status_label, LV_PCT(90));
    lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_status_label, "Provisioning Mode");
    lv_obj_align(s_status_label, LV_ALIGN_TOP_MID, 0, 20);

    // Back Button
    s_prov_back_btn = lv_btn_create(lcd_scr);
    lv_obj_set_size(s_prov_back_btn, 140, 60);
    // Align to bottom-center of the SCREEN, not relative to QR
    lv_obj_align(s_prov_back_btn, LV_ALIGN_BOTTOM_MID, 0, -30);
    
    lv_obj_set_style_bg_color(s_prov_back_btn, lv_color_hex(0x888888), 0);
    lv_obj_t *lbl_back = lv_label_create(s_prov_back_btn);
    lv_label_set_text(lbl_back, "Exit"); // "Exit" might be clearer than "Back" for mode switch
    lv_obj_center(lbl_back);
    lv_obj_add_event_cb(s_prov_back_btn, on_prov_back_click, LV_EVENT_CLICKED, NULL);

    free(payload);
}

esp_err_t kc_touch_display_show_qr(const char *payload)
{
    if (!payload) return ESP_ERR_INVALID_ARG;
    char *p = strdup(payload);
    if (!p) return ESP_ERR_NO_MEM;
    return kc_touch_gui_dispatch(kc_touch_display_show_qr_task, p, 0);
}

static void kc_touch_display_update_label_task(void *ctx)
{
    char *msg = (char *)ctx;
    if (s_status_label && lv_obj_is_valid(s_status_label)) {
        lv_label_set_text(s_status_label, msg);
    }
    free(msg);
}

esp_err_t kc_touch_display_set_status(const char *fmt, ...)
{
    if (!s_display_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    
    char *msg = NULL;
    va_list args;
    va_start(args, fmt);
    int len = vasprintf(&msg, fmt, args);
    va_end(args);

    if (len < 0 || !msg) {
        return ESP_FAIL;
    }

    return kc_touch_gui_dispatch(kc_touch_display_update_label_task, msg, 0);
}

void kc_touch_display_reset_ui_state(void)
{
    // These objects are destroyed by lv_obj_clean() in the UI layer.
    // We must forget them to avoid use-after-free in async tasks.
    s_status_label = NULL;
    s_prov_back_btn = NULL;
}

bool kc_touch_display_is_ready(void)
{
    return s_display_ready;
}

bool kc_touch_touch_is_ready(void)
{
#if CONFIG_KC_TOUCH_TOUCH_ENABLE
    return s_touch_ready;
#else
    return false;
#endif
}

#else // CONFIG_KC_TOUCH_DISPLAY_ENABLE

esp_err_t kc_touch_display_init(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t kc_touch_display_backlight_set(bool enable)
{
    (void)enable;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t kc_touch_display_set_provisioning_cb(kc_touch_display_prov_cb_t cb, void *ctx)
{
    (void)cb;
    (void)ctx;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t kc_touch_display_set_cancel_cb(kc_touch_display_cancel_cb_t cb, void *ctx)
{
    (void)cb;
    (void)ctx;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t kc_touch_display_prov_enable_back(bool enable)
{
    (void)enable;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t kc_touch_display_set_status(const char *fmt, ...)
{
    (void)fmt;
    return ESP_ERR_NOT_SUPPORTED;
}

void kc_touch_display_reset_ui_state(void)
{
}

bool kc_touch_display_is_ready(void)
{
    return false;
}

bool kc_touch_touch_is_ready(void)
{
    return false;
}

#endif // CONFIG_KC_TOUCH_DISPLAY_ENABLE
