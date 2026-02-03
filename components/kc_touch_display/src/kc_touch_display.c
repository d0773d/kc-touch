#include "kc_touch_display.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "kc_touch_display_tab5.h"
#include "kc_touch_gui.h"
#include "lvgl.h"
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

static void kc_touch_display_force_prov_event(lv_event_t *event)
{
    (void)event;
    if (s_prov_cb) {
        s_prov_cb(s_prov_ctx);
    } else {
        ESP_LOGW(TAG, "Provision button pressed but no callback registered");
    }
}

static void kc_touch_display_build_scene(void *ctx)
{
    (void)ctx;
    lv_obj_t *screen = lv_scr_act();
    lv_obj_clean(screen);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "KC Touch Console");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    lv_obj_t *subtitle = lv_label_create(screen);
    lv_label_set_text(subtitle, "Wi-Fi + GUI online");
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 40);

    lv_obj_t *btn = lv_btn_create(screen);
    lv_obj_set_size(btn, 200, 56);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(btn, kc_touch_display_force_prov_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Start Wi-Fi provisioning");
    lv_obj_center(btn_label);

    lv_obj_t *note = lv_label_create(screen);
    lv_label_set_text(note, "Tap the button to expose the SoftAP provisioning portal.");
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(note, DISPLAY_WIDTH - 20);
    lv_obj_align(note, LV_ALIGN_BOTTOM_MID, 0, -10);
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
    s_lv_disp_drv.hor_res = DISPLAY_WIDTH;
    s_lv_disp_drv.ver_res = DISPLAY_HEIGHT;
    s_lv_disp_drv.flush_cb = kc_touch_display_flush_cb;
    s_lv_disp_drv.draw_buf = &s_lv_draw_buf;
    s_lv_display = lv_disp_drv_register(&s_lv_disp_drv);
    lv_disp_set_rotation(s_lv_display, kc_touch_display_rotation());
    kc_touch_display_build_scene(NULL);
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

bool kc_touch_display_is_ready(void)
{
    return false;
}

bool kc_touch_touch_is_ready(void)
{
    return false;
}

#endif // CONFIG_KC_TOUCH_DISPLAY_ENABLE
