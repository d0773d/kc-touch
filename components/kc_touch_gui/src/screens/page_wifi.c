#include "lvgl.h"
#include "ui_theme.h"
#include "kc_touch_gui.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "PageWiFi";
static lv_obj_t *s_wifi_list = NULL;
static lv_obj_t *s_scan_btn_label = NULL;
static bool s_is_scanning = false;

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

static void on_scan_click(lv_event_t *e) {
    if(s_is_scanning) return;
    start_scan();
}

// -------------------------------------------------------------------------
// Page Lifecycle
// -------------------------------------------------------------------------
static void page_cleanup(lv_event_t *e) {
    s_wifi_list = NULL;
    s_scan_btn_label = NULL;
    s_is_scanning = false;
    // Unregister locally registered handler
    esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, wifi_scan_handler);
}

void page_wifi_init(lv_obj_t *parent)
{
    // Register handler locally for this page's lifetime
    // Note: If multiple pages are pushed, this might duplicate, but we switch pages by clearing parent
    // so previous page is deleted and cleanup calls unregister.
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, wifi_scan_handler, NULL);
    
    // Main Layout (Column)
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(cont, 15, 0);
    lv_obj_add_event_cb(cont, page_cleanup, LV_EVENT_DELETE, NULL);

    // 1. Controls Row
    lv_obj_t *controls = lv_obj_create(cont);
    lv_obj_set_width(controls, LV_PCT(100));
    lv_obj_set_height(controls, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(controls, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(controls, 0, 0);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *btn = lv_btn_create(controls);
    lv_obj_set_height(btn, 40);
    lv_obj_add_style(btn, &style_sidebar_btn_checked, 0); 
    lv_obj_add_event_cb(btn, on_scan_click, LV_EVENT_CLICKED, NULL);
    
    s_scan_btn_label = lv_label_create(btn);
    lv_label_set_text(s_scan_btn_label, "Scan Networks");
    lv_obj_center(s_scan_btn_label);

    // 2. Scan Results List
    s_wifi_list = lv_obj_create(cont);
    lv_obj_set_width(s_wifi_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_wifi_list, 1); // Fill remaining space
    lv_obj_set_style_bg_opa(s_wifi_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_wifi_list, 0, 0);
    lv_obj_set_flex_flow(s_wifi_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(s_wifi_list, 10, 0);
    
    // Initial Scan
    start_scan();
}
