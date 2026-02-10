#include "lvgl.h"
#include "ui_theme.h"
#include "kc_touch_gui.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_video_device.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "linux/videodev2.h"
#include "quirc.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "esp_cache.h"
#include "../app_video.h"

static const char *TAG = "PageWiFi";
static lv_obj_t *s_wifi_list = NULL;
static lv_obj_t *s_scan_btn_label = NULL;
static bool s_is_scanning = false;
static lv_obj_t *s_page_root = NULL;

#define QR_FRAME_WIDTH           640
#define QR_FRAME_HEIGHT          480
#define QR_PREVIEW_WIDTH         320
#define QR_PREVIEW_HEIGHT        240
#define QR_PREVIEW_BPP           2
#define QR_PREVIEW_BUF_SIZE      (QR_PREVIEW_WIDTH * QR_PREVIEW_HEIGHT * QR_PREVIEW_BPP)
#define QR_DECODE_TARGET_WIDTH   320
#define QR_DECODE_TARGET_HEIGHT  240
#define QR_DECODE_MIN_WIDTH      160
#define QR_DECODE_MIN_HEIGHT     120
#define QR_CONVERT_YIELD_ROWS    1
#define QR_CONVERT_CHUNK_PIXELS  64
#define QR_V4L2_BUFFER_COUNT     3
#define QR_DEVICE_PATH           ESP_VIDEO_MIPI_CSI_DEVICE_NAME
#define ALIGN_UP(num, align)     (((num) + ((align) - 1)) & ~((align) - 1))

typedef struct {
    void *addr;
    size_t length;
} qr_buffer_t;

typedef struct {
    char ssid[33];
    char password[65];
    wifi_auth_mode_t authmode;
    bool hidden;
} qr_wifi_creds_t;

typedef struct {
    size_t length;
} qr_preview_msg_t;

typedef struct {
    char *text;
    bool is_ssid;
} qr_label_msg_t;

static lv_obj_t *s_qr_overlay = NULL;
static lv_obj_t *s_qr_status_label = NULL;
static lv_obj_t *s_qr_ssid_label = NULL;
static lv_obj_t *s_qr_preview_img = NULL;
static lv_obj_t *s_qr_cancel_btn = NULL;
static lv_obj_t *s_qr_spinner = NULL;
static lv_img_dsc_t s_qr_preview_dsc = {0};
static uint8_t *s_qr_preview_disp_buf = NULL;
static uint8_t *s_qr_preview_work_buf = NULL;
static uint8_t *s_qr_camera_frame_buf = NULL;  // Cache-aligned intermediate buffer for camera frames
static size_t s_qr_camera_frame_size = 0;
static SemaphoreHandle_t s_qr_preview_sem = NULL;
static TaskHandle_t s_qr_task_handle = NULL;
static volatile bool s_qr_stop_flag = false;
static bool s_qr_overlay_visible = false;
static bool s_qr_has_preview = false;

// app_video context
static int s_video_fd = -1;
static struct quirc *s_decoder = NULL;
static struct quirc_code *s_code = NULL;
static struct quirc_data *s_data = NULL;
static uint32_t s_frame_width = 0;
static uint32_t s_frame_height = 0;
static uint32_t s_decode_width = 0;
static uint32_t s_decode_height = 0;
static int64_t s_last_preview = 0;
static uint32_t s_pixel_format = 0;
static uint32_t s_stride = 0;

// Forward decl
static void start_scan(void);

// -------------------------------------------------------------------------
// GUI Thread Task: Update List with Scan Results
// -------------------------------------------------------------------------
typedef struct {
    uint16_t count;
    wifi_ap_record_t *records;
} scan_result_t;

static void update_list_ui_task(void *ctx) {
    scan_result_t *res = (scan_result_t*)ctx;
    
    s_is_scanning = false;
    if(s_scan_btn_label && lv_obj_is_valid(s_scan_btn_label)) {
        lv_label_set_text(s_scan_btn_label, "Scan Networks");
    }

    if (!s_wifi_list || !lv_obj_is_valid(s_wifi_list)) {
        // Page was probably closed, free memory and exit
        if(res) {
            if(res->records) free(res->records);
            free(res);
        }
        return;
    }

    lv_obj_clean(s_wifi_list);

    if (res && res->count > 0) {
        // Iterate results
        for (int i = 0; i < res->count; i++) {
            wifi_ap_record_t *r = &res->records[i];
            
            // Create Item Container
            lv_obj_t *item = lv_obj_create(s_wifi_list);
            lv_obj_set_width(item, LV_PCT(100));
            lv_obj_set_height(item, LV_SIZE_CONTENT);
            lv_obj_add_style(item, &style_card, 0); // Reuse card style
            lv_obj_set_style_pad_all(item, 15, 0);  // Slight padding
            lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);            

            // Icon (RSSI)
            lv_obj_t *icon = lv_label_create(item);
            lv_obj_set_style_text_font(icon, &lv_font_montserrat_14, 0);
            if (r->rssi > -60) lv_label_set_text(icon, LV_SYMBOL_WIFI);
            else if (r->rssi > -70) lv_label_set_text(icon, LV_SYMBOL_WIFI); 
            else lv_label_set_text(icon, LV_SYMBOL_WIFI); 
            
            // Text (SSID)
            lv_obj_t *lbl = lv_label_create(item);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
            if (strlen((const char*)r->ssid) > 0) {
                lv_label_set_text(lbl, (const char*)r->ssid);
            } else {
                lv_label_set_text(lbl, "(Hidden SSID)");
            }
            lv_obj_set_style_text_color(lbl, COLOR_TEXT, 0);
            lv_obj_set_flex_grow(lbl, 1);
            lv_obj_set_style_pad_left(lbl, 10, 0);
            
            // Lock icon if secured
            if (r->authmode != WIFI_AUTH_OPEN) {
                lv_obj_t *lock = lv_label_create(item);
                lv_obj_set_style_text_font(lock, &lv_font_montserrat_14, 0);
                lv_label_set_text(lock, LV_SYMBOL_WARNING);
                lv_obj_set_style_text_color(lock, COLOR_ALERT, 0);
            }
        }
    } else {
        lv_obj_t *lbl = lv_label_create(s_wifi_list);
        lv_label_set_text(lbl, "No networks found.");
        lv_obj_set_style_text_color(lbl, COLOR_TEXT_DIM, 0);
        lv_obj_center(lbl);
    }
    
    // Cleanup
    if(res) {
        if(res->records) free(res->records);
        free(res);
    }
}

// -------------------------------------------------------------------------
// System Event Handler (Runs in Event Task)
// -------------------------------------------------------------------------
static void wifi_scan_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        ESP_LOGI(TAG, "Scan done event received");
        
        uint16_t ap_count = 0;
        esp_wifi_scan_get_ap_num(&ap_count);
        ESP_LOGI(TAG, "Scan Found: %d APs", ap_count);
        
        wifi_ap_record_t *all_aps = calloc(ap_count, sizeof(wifi_ap_record_t));
        if (all_aps) {
            esp_wifi_scan_get_ap_records(&ap_count, all_aps);

            scan_result_t *res = calloc(1, sizeof(scan_result_t));
            if (res) {
                // Reserve space for worst case (all unique)
                res->records = calloc(ap_count, sizeof(wifi_ap_record_t));
                res->count = 0;
                
                if (res->records) {
                    for (int i = 0; i < ap_count; i++) {
                        // Skip empty SSIDs
                        if (strlen((const char*)all_aps[i].ssid) == 0) continue;

                        bool seen = false;
                        for (int j = 0; j < res->count; j++) {
                            if (strcmp((const char*)res->records[j].ssid, (const char*)all_aps[i].ssid) == 0) {
                                // Already in list, keep the strongest signal
                                if (all_aps[i].rssi > res->records[j].rssi) {
                                    memcpy(&res->records[j], &all_aps[i], sizeof(wifi_ap_record_t));
                                }
                                seen = true;
                                break;
                            }
                        }
                        if (!seen) {
                            memcpy(&res->records[res->count], &all_aps[i], sizeof(wifi_ap_record_t));
                            res->count++;
                        }
                    }
                }
                // Dispatch to GUI thread
                kc_touch_gui_dispatch(update_list_ui_task, res, 0);
            }
            free(all_aps);
        }

        // Scan done, reset flag so main app can reconnect
        kc_touch_gui_set_scanning(false);
        
        // Attempt to restore connection to stored network
        esp_wifi_connect();
    }
}

// -------------------------------------------------------------------------
// Button Event
// -------------------------------------------------------------------------
static void start_scan(void) {
    ESP_LOGI(TAG, "Starting scan...");
    s_is_scanning = true;
    
    // Lock connection Manager (app_main)
    kc_touch_gui_set_scanning(true);
    // Force disconnect (async) to free up radio
    esp_wifi_disconnect();
    
    // Give the disconnect event a moment to propagate and state to update
    vTaskDelay(pdMS_TO_TICKS(200));

    if(s_scan_btn_label) lv_label_set_text(s_scan_btn_label, "Scanning...");
    
    // Clear list
    if(s_wifi_list) lv_obj_clean(s_wifi_list);
    
    // Add spinner
    lv_obj_t *spinner = lv_spinner_create(s_wifi_list, 1000, 60);
    lv_obj_set_size(spinner, 40, 40);
    lv_obj_center(spinner);

    // Wait a brief moment for disconnect event (hacky but often sufficient if not blocking extensively)
    // Or just try starting scan. If ESP_ERR_WIFI_STATE, we might rely on a disconnect callback to retry.
    // For now, let's try starting it. If it fails due to STATE, we might need a dedicated retry.
    // However, calling esp_wifi_disconnect() usually puts it in a state where next tick it handles it.
    
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = {
            .active = {
                .min = 120,
                .max = 240
            },
            .passive = 360
        }
    };
    
    // We attempt; if it fails due to connecting, we might need a delay. 
    // Ideally we should wait for event. But let's try this simple logic first:
    // Disconnect -> Scan. (If scan fails, user clicks again).
    esp_err_t err = esp_wifi_scan_start(&scan_config, false); // Block = false
    
    // If we failed because we are busy connecting/disconnecting, we just log it.
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Scan failed: %s", esp_err_to_name(err));
        
        // If it was state error, maybe we are still disconnecting.
        if (err == ESP_ERR_WIFI_STATE && s_is_scanning) {
             ESP_LOGW(TAG, "Scan blocked by state. User might need to press again or we should retry.");
             // We leave s_is_scanning true? No, fail visually.
             s_is_scanning = false;
             kc_touch_gui_set_scanning(false); // Release lock
             if(s_scan_btn_label) lv_label_set_text(s_scan_btn_label, "Busy/Retry");
             lv_obj_clean(s_wifi_list);
        } else {
            s_is_scanning = false;
            kc_touch_gui_set_scanning(false);
            if(s_scan_btn_label) lv_label_set_text(s_scan_btn_label, "Scan Failed");
            lv_obj_clean(s_wifi_list);
        }
    }
}

/*
static void on_scan_click(lv_event_t *e) {
    if(s_is_scanning) return;
    start_scan();
}
*/

// -------------------------------------------------------------------------
// QR Scanner Implementation
// -------------------------------------------------------------------------
static void qr_scan_task(void *arg);
static void qr_reset_overlay_refs(void)
{
    s_qr_overlay = NULL;
    s_qr_status_label = NULL;
    s_qr_ssid_label = NULL;
    s_qr_preview_img = NULL;
    s_qr_cancel_btn = NULL;
    s_qr_spinner = NULL;
    s_qr_overlay_visible = false;
    s_qr_has_preview = false;
}

static void qr_free_preview_buffers(void)
{
    if (s_qr_preview_disp_buf) {
        heap_caps_free(s_qr_preview_disp_buf);
        s_qr_preview_disp_buf = NULL;
    }
    if (s_qr_preview_work_buf) {
        heap_caps_free(s_qr_preview_work_buf);
        s_qr_preview_work_buf = NULL;
    }
    if (s_qr_camera_frame_buf) {
        heap_caps_free(s_qr_camera_frame_buf);
        s_qr_camera_frame_buf = NULL;
    }
    s_qr_camera_frame_size = 0;
    if (s_qr_preview_sem) {
        vSemaphoreDelete(s_qr_preview_sem);
        s_qr_preview_sem = NULL;
    }
}

static void qr_label_update_cb(void *ctx)
{
    qr_label_msg_t *msg = (qr_label_msg_t *)ctx;
    if (!msg || !msg->text) {
        free(msg);
        return;
    }

    lv_obj_t *target = msg->is_ssid ? s_qr_ssid_label : s_qr_status_label;
    if (target && lv_obj_is_valid(target)) {
        if (msg->is_ssid) {
            lv_label_set_text_fmt(target, "SSID: %s", msg->text);
            lv_obj_clear_flag(target, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(target, msg->text);
        }
    }
    free(msg->text);
    free(msg);
}

static void qr_post_label(const char *text, bool is_ssid)
{
    if (!text || !s_qr_overlay_visible) {
        return;
    }
    qr_label_msg_t *msg = calloc(1, sizeof(qr_label_msg_t));
    if (!msg) {
        return;
    }
    msg->text = strdup(text);
    msg->is_ssid = is_ssid;
    if (!msg->text) {
        free(msg);
        return;
    }
    if (kc_touch_gui_dispatch(qr_label_update_cb, msg, 0) != ESP_OK) {
        free(msg->text);
        free(msg);
    }
}

static void qr_preview_copy_cb(void *ctx)
{
    qr_preview_msg_t *msg = (qr_preview_msg_t *)ctx;
    if (msg && s_qr_preview_disp_buf && s_qr_preview_work_buf &&
        s_qr_preview_img && lv_obj_is_valid(s_qr_preview_img) &&
        msg->length <= QR_PREVIEW_BUF_SIZE) {
        memcpy(s_qr_preview_disp_buf, s_qr_preview_work_buf, msg->length);
        
        s_qr_preview_dsc.data = s_qr_preview_disp_buf;
        s_qr_preview_dsc.data_size = msg->length;
        lv_img_set_src(s_qr_preview_img, &s_qr_preview_dsc);
        if (s_qr_spinner && lv_obj_is_valid(s_qr_spinner)) {
            lv_obj_add_flag(s_qr_spinner, LV_OBJ_FLAG_HIDDEN);
        }
        s_qr_has_preview = true;
    }
    free(msg);
    if (s_qr_preview_sem) {
        xSemaphoreGive(s_qr_preview_sem);
    }
}

static bool qr_schedule_preview_copy(size_t length)
{
    if (!s_qr_overlay_visible || length > QR_PREVIEW_BUF_SIZE) {
        return false;
    }
    qr_preview_msg_t *msg = malloc(sizeof(qr_preview_msg_t));
    if (!msg) {
        return false;
    }
    msg->length = length;
    if (kc_touch_gui_dispatch(qr_preview_copy_cb, msg, 0) != ESP_OK) {
        free(msg);
        return false;
    }
    return true;
}

static uint32_t qr_bytes_per_pixel(uint32_t pixel_format)
{
    switch (pixel_format) {
        case V4L2_PIX_FMT_YUYV:
        case V4L2_PIX_FMT_YVYU:
        case V4L2_PIX_FMT_UYVY:
        case V4L2_PIX_FMT_VYUY:
        case V4L2_PIX_FMT_RGB565:
            return 2;
        case V4L2_PIX_FMT_GREY:
        case V4L2_PIX_FMT_SRGGB8:
        case V4L2_PIX_FMT_SGRBG8:
        case V4L2_PIX_FMT_SGBRG8:
        case V4L2_PIX_FMT_SBGGR8:
            return 1;
        default:
            return 1;
    }
}

static inline void qr_maybe_yield_rows(uint32_t row)
{
    // Yield every 32 rows to let the IDLE task run (prevent wdt trigger)
    // 180 / 32 = ~5 yields. At 10ms/tick, adds ~50ms latency per frame.
    // Using simple mask 0x1F (31) checks for every 32nd row.
    if ((row & 0x1FU) == 0U) {
        vTaskDelay(1);
    }
}

static inline void qr_maybe_yield_chunk(void)
{
    // No-op: we rely on row-level delays to feed WDT.
    // Keeping function to minimize code churn.
}

static inline uint8_t qr_rgb565_to_gray(uint16_t pixel)
{
    uint8_t r5 = (pixel >> 11) & 0x1F;
    uint8_t g6 = (pixel >> 5) & 0x3F;
    uint8_t b5 = pixel & 0x1F;
    uint8_t r8 = (r5 << 3) | (r5 >> 2);
    uint8_t g8 = (g6 << 2) | (g6 >> 4);
    uint8_t b8 = (b5 << 3) | (b5 >> 2);
    uint32_t y = r8 * 77u + g8 * 150u + b8 * 29u;
    return (uint8_t)(y >> 8);
}

static inline uint8_t qr_sample_gray_from_frame(const uint8_t *src_base,
                                                const uint8_t *row,
                                                uint32_t x,
                                                uint32_t y,
                                                uint32_t src_width,
                                                uint32_t src_height,
                                                uint32_t pixel_format)
{
    (void)src_base;
    (void)y;
    (void)src_height;
    
    switch (pixel_format) {
        case V4L2_PIX_FMT_YUYV:
        case V4L2_PIX_FMT_YVYU: {
            const uint8_t *px = row + (x & ~1u) * 2u;
            return px[(x & 1u) ? 2 : 0];
        }
        case V4L2_PIX_FMT_UYVY:
        case V4L2_PIX_FMT_VYUY: {
            const uint8_t *px = row + (x & ~1u) * 2u;
            return px[(x & 1u) ? 3 : 1];
        }
        case V4L2_PIX_FMT_RGB565:
        default: {
            // RGBP format on ESP32-P4 is actually packed RGB565, not planar
            // Treat any unknown format as RGB565
            const uint16_t *pix16 = (const uint16_t *)row;
            return qr_rgb565_to_gray(pix16[x]);
        }
    }
}

static void qr_convert_frame_to_gray(const uint8_t *src,
                                     uint32_t src_width,
                                     uint32_t src_height,
                                     uint32_t pixel_format,
                                     uint32_t stride,
                                     uint32_t dst_width,
                                     uint32_t dst_height,
                                     uint8_t *dst)
{
    if (!src || !dst || src_width == 0 || src_height == 0 || dst_width == 0 || dst_height == 0) {
        return;
    }

    if (stride == 0) {
        stride = qr_bytes_per_pixel(pixel_format) * src_width;
    }

    for (uint32_t dy = 0; dy < dst_height; ++dy) {
        if (s_qr_stop_flag) {
            return;
        }
        uint32_t sy = (uint64_t)dy * src_height / dst_height;
        if (sy >= src_height) {
            sy = src_height - 1;
        }
        const uint8_t *row = src + sy * stride;
        uint8_t *dst_row = dst + dy * dst_width;
        for (uint32_t dx = 0; dx < dst_width; ) {
            uint32_t chunk_end = dx + QR_CONVERT_CHUNK_PIXELS;
            if (chunk_end > dst_width) {
                chunk_end = dst_width;
            }
            for (; dx < chunk_end; ++dx) {
                uint32_t sx = (uint64_t)dx * src_width / dst_width;
                if (sx >= src_width) {
                    sx = src_width - 1;
                }
                dst_row[dx] = qr_sample_gray_from_frame(src, row, sx, sy, src_width, src_height, pixel_format);
            }
            qr_maybe_yield_chunk();
            if (s_qr_stop_flag) {
                return;
            }
        }
        qr_maybe_yield_rows(dy);
    }
}

static void qr_render_preview(const uint8_t *gray, uint32_t width, uint32_t height)
{
    if (!gray || !s_qr_preview_work_buf || width == 0 || height == 0) {
        return;
    }

    uint32_t step_x = width / QR_PREVIEW_WIDTH;
    uint32_t step_y = height / QR_PREVIEW_HEIGHT;
    if (step_x == 0) step_x = 1;
    if (step_y == 0) step_y = 1;

    uint16_t *dest = (uint16_t *)s_qr_preview_work_buf;
    for (uint32_t py = 0; py < QR_PREVIEW_HEIGHT; ++py) {
        if (s_qr_stop_flag) {
            return;
        }
        uint32_t sy = py * step_y;
        if (sy >= height) {
            sy = height - 1;
        }
        const uint8_t *row = gray + sy * width;
        for (uint32_t px = 0; px < QR_PREVIEW_WIDTH; ) {
            uint32_t chunk_end = px + QR_CONVERT_CHUNK_PIXELS;
            if (chunk_end > QR_PREVIEW_WIDTH) {
                chunk_end = QR_PREVIEW_WIDTH;
            }
            for (; px < chunk_end; ++px) {
                uint32_t sx = px * step_x;
                if (sx >= width) {
                    sx = width - 1;
                }
                uint8_t y_val = row[sx];
                uint16_t color = (uint16_t)(((y_val >> 3) << 11) |
                                            ((y_val >> 2) << 5) |
                                            (y_val >> 3));
                dest[py * QR_PREVIEW_WIDTH + px] = color;
            }
            qr_maybe_yield_chunk();
            if (s_qr_stop_flag) {
                return;
            }
        }
        qr_maybe_yield_rows(py);
    }
}

static const char *qr_extract_field(const char *src, char *dst, size_t dst_len)
{
    bool escape = false;
    size_t idx = 0;
    while (*src && !(*src == ';' && !escape)) {
        if (!escape && *src == '\\') {
            escape = true;
        } else {
            if (idx + 1 < dst_len) {
                dst[idx++] = *src;
            }
            escape = false;
        }
        src++;
    }
    dst[idx] = '\0';
    if (*src == ';') {
        src++;
    }
    return src;
}

static wifi_auth_mode_t qr_auth_from_token(const char *token)
{
    if (!token) {
        return WIFI_AUTH_WPA2_PSK;
    }
    if (strcasecmp(token, "WPA") == 0) return WIFI_AUTH_WPA_PSK;
    if (strcasecmp(token, "WPA2") == 0) return WIFI_AUTH_WPA2_PSK;
    if (strcasecmp(token, "WPA/WPA2") == 0) return WIFI_AUTH_WPA_WPA2_PSK;
    if (strcasecmp(token, "WPA3") == 0) return WIFI_AUTH_WPA3_PSK;
    if (strcasecmp(token, "WPA2/WPA3") == 0) return WIFI_AUTH_WPA2_WPA3_PSK;
    if (strcasecmp(token, "WEP") == 0) return WIFI_AUTH_WEP;
    if (strcasecmp(token, "nopass") == 0) return WIFI_AUTH_OPEN;
    return WIFI_AUTH_WPA2_PSK;
}

static bool qr_parse_simple_payload(const char *text, qr_wifi_creds_t *out)
{
    const char *sep = strpbrk(text, ",\n");
    if (!sep) {
        return false;
    }
    size_t ssid_len = sep - text;
    if (ssid_len >= sizeof(out->ssid)) {
        ssid_len = sizeof(out->ssid) - 1;
    }
    memcpy(out->ssid, text, ssid_len);
    out->ssid[ssid_len] = '\0';
    strncpy(out->password, sep + 1, sizeof(out->password) - 1);
    out->password[sizeof(out->password) - 1] = '\0';
    out->authmode = out->password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    return true;
}

static bool qr_parse_wifi_payload(const char *payload, size_t len, qr_wifi_creds_t *out)
{
    if (!payload || !out) {
        return false;
    }

    char *text = malloc(len + 1);
    if (!text) {
        return false;
    }
    memcpy(text, payload, len);
    text[len] = '\0';

    bool ok = false;
    memset(out, 0, sizeof(*out));
    out->authmode = WIFI_AUTH_WPA2_PSK;

    if (strncmp(text, "WIFI:", 5) == 0) {
        const char *p = text + 5;
        while (*p) {
            if (*p == ';') {
                p++;
                continue;
            }
            char key = *p++;
            if (*p != ':') {
                break;
            }
            p++;
            char value[128];
            p = qr_extract_field(p, value, sizeof(value));
            switch (key) {
                case 'S':
                    strncpy(out->ssid, value, sizeof(out->ssid) - 1);
                    break;
                case 'P':
                    strncpy(out->password, value, sizeof(out->password) - 1);
                    break;
                case 'T':
                    out->authmode = qr_auth_from_token(value);
                    break;
                case 'H':
                    out->hidden = (strcasecmp(value, "true") == 0 || strcmp(value, "1") == 0);
                    break;
                default:
                    break;
            }
        }
        if (out->authmode == WIFI_AUTH_OPEN) {
            out->password[0] = '\0';
        }
        ok = (out->ssid[0] != '\0');
    } else {
        ok = qr_parse_simple_payload(text, out);
    }

    free(text);
    return ok;
}

static void qr_apply_wifi_cb(void *ctx)
{
    qr_wifi_creds_t *creds = (qr_wifi_creds_t *)ctx;
    if (!creds) {
        return;
    }
    if (s_qr_cancel_btn && lv_obj_is_valid(s_qr_cancel_btn)) {
        lv_obj_add_state(s_qr_cancel_btn, LV_STATE_DISABLED);
    }
    if (s_qr_status_label && lv_obj_is_valid(s_qr_status_label)) {
        lv_label_set_text_fmt(s_qr_status_label, "Connecting to %s...", creds->ssid);
    }

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, creds->ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, creds->password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = creds->authmode;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    if (creds->hidden) {
        wifi_config.sta.scan_method = WIFI_FAST_SCAN;
    }

    esp_wifi_disconnect();
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_connect();

    free(creds);
}

static void qr_teardown_cb(void *ctx)
{
    (void)ctx;
    if (s_qr_overlay && lv_obj_is_valid(s_qr_overlay)) {
        lv_obj_del(s_qr_overlay);
    }
    qr_free_preview_buffers();
    qr_reset_overlay_refs();
}

static void qr_on_cancel(lv_event_t *e)
{
    (void)e;
    qr_post_label("Stopping scanner...", false);
    s_qr_stop_flag = true;
}

static bool qr_create_overlay(void)
{
    if (!s_page_root) {
        return false;
    }
    if (s_qr_overlay) {
        return true;
    }

    // Get cache line alignment for PSRAM
    // ESP32-P4 has 64-byte cache lines
    size_t cache_line_size = 64;

    // Allocate cache-aligned buffers for optimal DMA performance
    size_t aligned_size = ALIGN_UP(QR_PREVIEW_BUF_SIZE, cache_line_size);
    s_qr_preview_disp_buf = heap_caps_aligned_calloc(cache_line_size, 1, aligned_size, MALLOC_CAP_SPIRAM);
    s_qr_preview_work_buf = heap_caps_aligned_calloc(cache_line_size, 1, aligned_size, MALLOC_CAP_SPIRAM);
    if (!s_qr_preview_disp_buf || !s_qr_preview_work_buf) {
        ESP_LOGE(TAG, "Failed to allocate cache-aligned preview buffers");
        qr_free_preview_buffers();
        return false;
    }

    // Initialize to black
    memset(s_qr_preview_disp_buf, 0, QR_PREVIEW_BUF_SIZE);
    memset(s_qr_preview_work_buf, 0, QR_PREVIEW_BUF_SIZE);

    if (!s_qr_preview_sem) {
        s_qr_preview_sem = xSemaphoreCreateBinary();
        if (!s_qr_preview_sem) {
            qr_free_preview_buffers();
            return false;
        }
        xSemaphoreGive(s_qr_preview_sem);
    }

    memset(&s_qr_preview_dsc, 0, sizeof(s_qr_preview_dsc));
    s_qr_preview_dsc.header.always_zero = 0;
    s_qr_preview_dsc.header.w = QR_PREVIEW_WIDTH;
    s_qr_preview_dsc.header.h = QR_PREVIEW_HEIGHT;
    s_qr_preview_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    s_qr_preview_dsc.data = s_qr_preview_disp_buf;
    s_qr_preview_dsc.data_size = QR_PREVIEW_BUF_SIZE;

    s_qr_overlay = lv_obj_create(s_page_root);
    lv_obj_set_size(s_qr_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_qr_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_qr_overlay, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(s_qr_overlay, 20, 0);
    lv_obj_set_flex_flow(s_qr_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_qr_overlay, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_qr_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(s_qr_overlay);
    lv_label_set_text(title, "Scan Wi-Fi QR Code");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);

    s_qr_preview_img = lv_img_create(s_qr_overlay);
    lv_obj_set_size(s_qr_preview_img, QR_PREVIEW_WIDTH, QR_PREVIEW_HEIGHT);
    lv_img_set_src(s_qr_preview_img, &s_qr_preview_dsc);
    lv_obj_set_style_border_color(s_qr_preview_img, lv_color_hex(0x555555), 0);
    lv_obj_set_style_border_width(s_qr_preview_img, 2, 0);

    s_qr_spinner = lv_spinner_create(s_qr_overlay, 1000, 60);
    lv_obj_set_size(s_qr_spinner, 60, 60);

    s_qr_status_label = lv_label_create(s_qr_overlay);
    lv_label_set_text(s_qr_status_label, "Initializing camera...");
    lv_obj_set_width(s_qr_status_label, LV_PCT(90));
    lv_label_set_long_mode(s_qr_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_qr_status_label, LV_TEXT_ALIGN_CENTER, 0);

    s_qr_ssid_label = lv_label_create(s_qr_overlay);
    lv_label_set_text(s_qr_ssid_label, "");
    lv_obj_add_flag(s_qr_ssid_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_align(s_qr_ssid_label, LV_TEXT_ALIGN_CENTER, 0);

    s_qr_cancel_btn = lv_btn_create(s_qr_overlay);
    lv_obj_set_size(s_qr_cancel_btn, 150, 50);
    lv_obj_add_event_cb(s_qr_cancel_btn, qr_on_cancel, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_lbl = lv_label_create(s_qr_cancel_btn);
    lv_label_set_text(btn_lbl, "Cancel");
    lv_obj_center(btn_lbl);

    s_qr_overlay_visible = true;
    return true;
}

static const char* qr_start_session(void)
{
    if (s_qr_task_handle) {
        return "QR scanner already running";
    }
    if (!kc_touch_gui_camera_ready()) {
        ESP_LOGW(TAG, "Camera not ready when starting QR session");
        return "Camera driver not initialized";
    }
    if (!qr_create_overlay()) {
        ESP_LOGE(TAG, "Failed to create QR overlay UI");
        return "Unable to allocate QR UI";
    }

    s_qr_stop_flag = false;
    // Lower priority to 1 to allow other system tasks to run.
    BaseType_t created = xTaskCreatePinnedToCore(qr_scan_task,
                                                 "qr_scan",
                                                 8192,
                                                 NULL,
                                                 1,
                                                 &s_qr_task_handle,
                                                 tskNO_AFFINITY);
    if (created != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate failed for qr_scan");
        s_qr_task_handle = NULL;
        return "Failed to start QR scanner task";
    }
    return NULL;  // Success
}

// Render RGB565 camera frame directly to preview (no grayscale roundtrip)
static void qr_render_preview_rgb565(const uint8_t *frame, uint32_t src_w, uint32_t src_h, uint32_t stride)
{
    if (!frame || !s_qr_preview_work_buf || src_w == 0 || src_h == 0) {
        return;
    }

    uint16_t *dest = (uint16_t *)s_qr_preview_work_buf;
    for (uint32_t py = 0; py < QR_PREVIEW_HEIGHT; ++py) {
        if (s_qr_stop_flag) return;
        uint32_t sy = (uint64_t)py * src_h / QR_PREVIEW_HEIGHT;
        if (sy >= src_h) sy = src_h - 1;
        const uint16_t *src_row = (const uint16_t *)(frame + sy * stride);
        for (uint32_t px = 0; px < QR_PREVIEW_WIDTH; ++px) {
            uint32_t sx = (uint64_t)px * src_w / QR_PREVIEW_WIDTH;
            if (sx >= src_w) sx = src_w - 1;
            dest[py * QR_PREVIEW_WIDTH + px] = src_row[sx];
        }
    }
}

// Frame operation callback for app_video - processes each captured frame
static void qr_frame_operation_cb(uint8_t *frame, uint8_t buf_idx, uint32_t width, uint32_t height, size_t size)
{
    (void)buf_idx;
    
    if (!frame || !s_decoder) {
        return;
    }
    
    // Log first frame info for debugging
    static int frame_log_count = 0;
    if (frame_log_count < 3) {
        ESP_LOGI(TAG, "Frame cb: %ux%u size=%u stride=%u ptr=%p", 
                 (unsigned)width, (unsigned)height, (unsigned)size, (unsigned)s_stride, frame);
        // Hex dump first 16 bytes to verify we're reading real data
        if (size >= 16) {
            ESP_LOGI(TAG, "Frame bytes: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                     frame[0], frame[1], frame[2], frame[3],
                     frame[4], frame[5], frame[6], frame[7],
                     frame[8], frame[9], frame[10], frame[11],
                     frame[12], frame[13], frame[14], frame[15]);
        }
        frame_log_count++;
    }
    
    // Invalidate cache to ensure we read fresh DMA data from camera
    size_t cache_line_size = 64;
    void *aligned_addr = (void*)((uintptr_t)frame & ~(cache_line_size - 1));
    size_t offset = (uintptr_t)frame - (uintptr_t)aligned_addr;
    size_t aligned_size = ((size + offset + cache_line_size - 1) / cache_line_size) * cache_line_size;
    
    esp_err_t sync_err = esp_cache_msync(aligned_addr, aligned_size, 
                                         ESP_CACHE_MSYNC_FLAG_DIR_M2C | 
                                         ESP_CACHE_MSYNC_FLAG_INVALIDATE);
    if (sync_err != ESP_OK) {
        static bool logged_sync_err = false;
        if (!logged_sync_err) {
            ESP_LOGW(TAG, "Cache sync failed: %s (addr=%p, size=%u)", 
                     esp_err_to_name(sync_err), aligned_addr, (unsigned)aligned_size);
            logged_sync_err = true;
        }
    }
    
    // Use saved stride from format query (bytesperline), not width*2
    uint32_t stride = s_stride ? s_stride : width * 2;
    
    // Update preview directly from RGB565 camera data (skip grayscale roundtrip)
    // Do this BEFORE quirc_end which modifies buffers
    int64_t now = esp_timer_get_time();
    if (!s_qr_stop_flag && s_qr_preview_sem && now - s_last_preview > 150000) {
        if (xSemaphoreTake(s_qr_preview_sem, 0) == pdTRUE) {
            qr_render_preview_rgb565(frame, s_frame_width, s_frame_height, stride);
            if (qr_schedule_preview_copy(QR_PREVIEW_BUF_SIZE)) {
                s_last_preview = now;
            } else {
                xSemaphoreGive(s_qr_preview_sem);
            }
        }
    }
    
    // Convert to grayscale for QR decode
    int img_w = 0;
    int img_h = 0;
    uint8_t *gray = quirc_begin(s_decoder, &img_w, &img_h);
    if (!gray || img_w != (int)s_decode_width || img_h != (int)s_decode_height) {
        return;
    }
    
    qr_convert_frame_to_gray(frame,
                             s_frame_width,
                             s_frame_height,
                             s_pixel_format,
                             stride,
                             s_decode_width,
                             s_decode_height,
                             gray);
    quirc_end(s_decoder);
    
    // Try to decode QR codes
    bool matched = false;
    int codes = quirc_count(s_decoder);
    for (int i = 0; i < codes; ++i) {
        quirc_extract(s_decoder, i, s_code);
        if (quirc_decode(s_code, s_data) == QUIRC_SUCCESS) {
            qr_wifi_creds_t creds;
            if (qr_parse_wifi_payload((const char *)s_data->payload, s_data->payload_len, &creds)) {
                qr_wifi_creds_t *copy = malloc(sizeof(qr_wifi_creds_t));
                if (copy) {
                    *copy = creds;
                    if (creds.ssid[0]) {
                        qr_post_label(creds.ssid, true);
                    }
                    qr_post_label("QR code detected", false);
                    if (kc_touch_gui_dispatch(qr_apply_wifi_cb, copy, 0) == ESP_OK) {
                        s_qr_stop_flag = true;
                    } else {
                        ESP_LOGW(TAG, "Failed to enqueue Wi-Fi credential handler");
                        free(copy);
                    }
                }
                matched = true;
                break;
            } else if (!matched) {
                qr_post_label("QR does not contain Wi-Fi info", false);
                matched = true;
                break;
            }
        }
    }
}

static void qr_scan_task(void *arg)
{
    (void)arg;
    
    // Open camera using app_video wrapper
    s_video_fd = app_video_open(QR_DEVICE_PATH, V4L2_PIX_FMT_RGB565);
    if (s_video_fd < 0) {
        ESP_LOGE(TAG, "QR camera open failed");
        qr_post_label("Camera open failed", false);
        goto exit;
    }
    
    // Get current format
    struct v4l2_format fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(s_video_fd, VIDIOC_G_FMT, &fmt) != 0) {
        ESP_LOGE(TAG, "VIDIOC_G_FMT failed: %s", strerror(errno));
        qr_post_label("Unable to query camera format", false);
        goto exit;
    }
    
    s_frame_width = fmt.fmt.pix.width;
    s_frame_height = fmt.fmt.pix.height;
    s_pixel_format = fmt.fmt.pix.pixelformat;
    s_stride = fmt.fmt.pix.bytesperline;
    if (s_stride == 0) {
        s_stride = s_frame_width * 2;  // fallback for RGB565
    }
    
    char fourcc[5] = {0};
    fourcc[0] = s_pixel_format & 0xFF;
    fourcc[1] = (s_pixel_format >> 8) & 0xFF;
    fourcc[2] = (s_pixel_format >> 16) & 0xFF;
    fourcc[3] = (s_pixel_format >> 24) & 0xFF;
    ESP_LOGI(TAG, "QR camera: %ux%u fmt=%s stride=%u (width*2=%u)",
             (unsigned)s_frame_width,
             (unsigned)s_frame_height,
             fourcc,
             (unsigned)s_stride,
             (unsigned)(s_frame_width * 2));
    
    // Calculate decode resolution
    s_decode_width = s_frame_width;
    s_decode_height = s_frame_height;
    while ((s_decode_width > QR_DECODE_TARGET_WIDTH || s_decode_height > QR_DECODE_TARGET_HEIGHT) &&
           (s_decode_width > QR_DECODE_MIN_WIDTH && s_decode_height > QR_DECODE_MIN_HEIGHT)) {
        s_decode_width = (s_decode_width + 1) / 2;
        s_decode_height = (s_decode_height + 1) / 2;
    }
    if (s_decode_width == 0 || s_decode_height == 0) {
        s_decode_width = s_frame_width;
        s_decode_height = s_frame_height;
    }
    ESP_LOGI(TAG, "QR decode surface %ux%u", (unsigned)s_decode_width, (unsigned)s_decode_height);
    
    // Set up app_video buffers (3 buffers with mmap mode)
    if (app_video_set_bufs(s_video_fd, QR_V4L2_BUFFER_COUNT, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "app_video_set_bufs failed");
        qr_post_label("Camera buffer allocation failed", false);
        goto exit;
    }
    
    // Initialize QR decoder
    s_decoder = quirc_new();
    if (!s_decoder) {
        qr_post_label("QR decoder alloc failed", false);
        goto exit;
    }
    if (quirc_resize(s_decoder, s_decode_width, s_decode_height) < 0) {
        qr_post_label("QR decoder resize failed", false);
        goto exit;
    }
    
    s_code = malloc(sizeof(*s_code));
    s_data = malloc(sizeof(*s_data));
    if (!s_code || !s_data) {
        qr_post_label("QR decoder workspace alloc failed", false);
        goto exit;
    }
    
    s_last_preview = 0;
    
    // Register frame operation callback
    app_video_register_frame_operation_cb(qr_frame_operation_cb);
    
    qr_post_label("Point camera at Wi-Fi QR", false);
    
    // Start video stream task
    if (app_video_stream_task_start(s_video_fd, tskNO_AFFINITY) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start video stream task");
        qr_post_label("Unable to start camera stream", false);
        goto exit;
    }
    
    // Wait for stop signal
    while (!s_qr_stop_flag) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // Stop video stream
    app_video_stream_task_stop(s_video_fd);
    app_video_wait_video_stop();
    
exit:
    if (s_decoder) {
        quirc_destroy(s_decoder);
        s_decoder = NULL;
    }
    free(s_code);
    s_code = NULL;
    free(s_data);
    s_data = NULL;
    if (s_video_fd >= 0) {
        app_video_close(s_video_fd);
        s_video_fd = -1;
    }
    
    s_qr_task_handle = NULL;
    s_qr_stop_flag = false;
    kc_touch_gui_dispatch(qr_teardown_cb, NULL, 0);
    vTaskDelete(NULL);
}

static lv_obj_t *s_menu_cont = NULL;
static lv_obj_t *s_manual_cont = NULL;
static lv_obj_t *s_ta_ssid = NULL;
static lv_obj_t *s_ta_pass = NULL;
static lv_obj_t *s_kb = NULL;

static void show_manual_entry(void);
static void show_menu(void);

// -------------------------------------------------------------------------
// QR Code / AP Mode Actions
// -------------------------------------------------------------------------
static void on_qr_click(lv_event_t *e) {
    (void)e;
    const char* error = qr_start_session();
    if (error) {
        lv_obj_t *msgbox = lv_msgbox_create(NULL,
                                            "QR Scanner Error",
                                            error,
                                            NULL,
                                            true);
        lv_obj_center(msgbox);
    }
}

static void on_ap_click(lv_event_t *e) {
    ESP_LOGI(TAG, "Requesting AP Mode");
    kc_touch_gui_trigger_provisioning();
    // Maybe show a spinner or status text update?
    // The provisioning callback in app_main updates the display status text via kc_touch_display_set_status
}

// -------------------------------------------------------------------------
// Manual Entry Logic
// -------------------------------------------------------------------------
static void manual_connect_click(lv_event_t *e) {
    const char *ssid = lv_textarea_get_text(s_ta_ssid);
    const char *pass = lv_textarea_get_text(s_ta_pass);
    
    if (strlen(ssid) == 0) return;

    ESP_LOGI(TAG, "Manual Connect: %s", ssid);
    
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (strlen(pass) > 0) {
        strncpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password) - 1);
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
         wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }

    esp_wifi_disconnect();
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_connect();
    
    ESP_LOGI(TAG, "Connecting to %s...", ssid);
    
    // Return to menu or dashboard?
    // Let's go back to menu but keep status updated
    show_menu();
}

static void manual_cancel_click(lv_event_t *e) {
    show_menu();
}

static void ta_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target(e);
    if (code == LV_EVENT_CLICKED || code == LV_EVENT_FOCUSED) {
        if (s_kb) {
            lv_keyboard_set_textarea(s_kb, ta);
            lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
        }
    } else if (code == LV_EVENT_DEFOCUSED) {
        if (s_kb) {
            lv_keyboard_set_textarea(s_kb, NULL);
            lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void show_manual_entry(void) {
    if (s_menu_cont) lv_obj_add_flag(s_menu_cont, LV_OBJ_FLAG_HIDDEN);
    
    if (!s_manual_cont) {
        // Create container
        s_manual_cont = lv_obj_create(lv_scr_act()); // Use screen or parent? Parent is safer in tabview
                                                     // But s_menu_cont parent is 'parent' passed in init.
                                                     // We should re-use that parent.
        // Wait, page_wifi_init gets 'parent'. We need to store it or create s_manual_cont in init but hide it.
        // Let's create everything in init.
        // Just unhide it here.
    }
    lv_obj_clear_flag(s_manual_cont, LV_OBJ_FLAG_HIDDEN);
}

static void show_menu(void) {
    if (s_manual_cont) lv_obj_add_flag(s_manual_cont, LV_OBJ_FLAG_HIDDEN);
    if (s_menu_cont) lv_obj_clear_flag(s_menu_cont, LV_OBJ_FLAG_HIDDEN);
}

static void on_manual_mode_click(lv_event_t *e) {
    show_manual_entry();
}

// -------------------------------------------------------------------------
// Page Lifecycle
// -------------------------------------------------------------------------
static void page_cleanup(lv_event_t *e) {
    s_wifi_list = NULL;
    s_scan_btn_label = NULL;
    s_is_scanning = false;
    
    s_menu_cont = NULL;
    s_manual_cont = NULL;
    s_ta_ssid = NULL;
    s_ta_pass = NULL;
    s_kb = NULL;

    bool qr_active = (s_qr_task_handle != NULL);
    if (qr_active) {
        s_qr_stop_flag = true;
        qr_reset_overlay_refs();
    } else {
        if (s_qr_overlay && lv_obj_is_valid(s_qr_overlay)) {
            lv_obj_del(s_qr_overlay);
        }
        qr_free_preview_buffers();
        qr_reset_overlay_refs();
    }

    s_page_root = NULL;

    esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, wifi_scan_handler);
}

void page_wifi_init(lv_obj_t *parent)
{
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, wifi_scan_handler, NULL);
    
    // Root container
    lv_obj_t *root = lv_obj_create(parent);
    s_page_root = root;
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_add_event_cb(root, page_cleanup, LV_EVENT_DELETE, NULL);

    // ================== MENU CONTAINER ==================
    s_menu_cont = lv_obj_create(root);
    lv_obj_set_size(s_menu_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(s_menu_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_menu_cont, 0, 0);
    lv_obj_set_flex_flow(s_menu_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_menu_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(s_menu_cont, 20, 0);

    // Button Style
    static lv_style_t style_menu_btn;
    static bool style_init = false;
    if (!style_init) {
        lv_style_init(&style_menu_btn);
        lv_style_set_width(&style_menu_btn, 250);
        lv_style_set_height(&style_menu_btn, 60);
        lv_style_set_radius(&style_menu_btn, 10);
        lv_style_set_bg_color(&style_menu_btn, lv_color_hex(0x333333));
        lv_style_set_text_color(&style_menu_btn, lv_color_white());
        style_init = true;
    }

    // 1. Manual
    lv_obj_t *btn1 = lv_btn_create(s_menu_cont);
    lv_obj_add_style(btn1, &style_menu_btn, 0);
    lv_obj_t *lbl1 = lv_label_create(btn1);
    lv_label_set_text(lbl1, "Manual Entry");
    lv_obj_center(lbl1);
    lv_obj_add_event_cb(btn1, on_manual_mode_click, LV_EVENT_CLICKED, NULL);

    // 2. QR Code
    lv_obj_t *btn2 = lv_btn_create(s_menu_cont);
    lv_obj_add_style(btn2, &style_menu_btn, 0);
    lv_obj_t *lbl2 = lv_label_create(btn2);
    lv_label_set_text(lbl2, "QR Code Scan");
    lv_obj_center(lbl2);
    lv_obj_add_event_cb(btn2, on_qr_click, LV_EVENT_CLICKED, NULL);

    // 3. AP Mode
    lv_obj_t *btn3 = lv_btn_create(s_menu_cont);
    lv_obj_add_style(btn3, &style_menu_btn, 0);
    lv_obj_t *lbl3 = lv_label_create(btn3);
    lv_label_set_text(lbl3, "AP Mode");
    lv_obj_center(lbl3);
    lv_obj_add_event_cb(btn3, on_ap_click, LV_EVENT_CLICKED, NULL);
    
    // ================== MANUAL CONTAINER ==================
    s_manual_cont = lv_obj_create(root);
    lv_obj_set_size(s_manual_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(s_manual_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_manual_cont, 0, 0);
    lv_obj_set_flex_flow(s_manual_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(s_manual_cont, LV_OBJ_FLAG_HIDDEN); // Hide initially

    // SSID
    lv_obj_t *lbl_ssid = lv_label_create(s_manual_cont);
    lv_label_set_text(lbl_ssid, "SSID:");
    
    s_ta_ssid = lv_textarea_create(s_manual_cont);
    lv_obj_set_width(s_ta_ssid, LV_PCT(80));
    lv_textarea_set_one_line(s_ta_ssid, true);
    lv_obj_add_event_cb(s_ta_ssid, ta_event_cb, LV_EVENT_ALL, NULL);

    // Password
    lv_obj_t *lbl_pass = lv_label_create(s_manual_cont);
    lv_label_set_text(lbl_pass, "Password:");
    
    s_ta_pass = lv_textarea_create(s_manual_cont);
    lv_obj_set_width(s_ta_pass, LV_PCT(80));
    lv_textarea_set_password_mode(s_ta_pass, true);
    lv_textarea_set_one_line(s_ta_pass, true);
    lv_obj_add_event_cb(s_ta_pass, ta_event_cb, LV_EVENT_ALL, NULL);

    // Buttons Row
    lv_obj_t *row = lv_obj_create(s_manual_cont);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *btn_conn = lv_btn_create(row);
    lv_label_set_text(lv_label_create(btn_conn), "Connect");
    lv_obj_add_event_cb(btn_conn, manual_connect_click, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_cancel = lv_btn_create(row);
    lv_label_set_text(lv_label_create(btn_cancel), "Cancel");
    lv_obj_add_event_cb(btn_cancel, manual_cancel_click, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0x888888), 0);

    // Keyboard (Global for page)
    s_kb = lv_keyboard_create(root);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    
    // We do NOT start scan automatically anymore.
}

/*
static void start_scan(void) {
    // ... kept for future use if we add a "Scan" button in manual mode ...
    // Or we repurpose "Manual" to be "Scan List + Manual" later.
    // For now, based on user request, it's just the 3 buttons.
}
*/
