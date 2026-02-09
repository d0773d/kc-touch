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

/*
static void on_scan_click(lv_event_t *e) {
    if(s_is_scanning) return;
    start_scan();
}
*/

static lv_obj_t *s_menu_cont = NULL;
static lv_obj_t *s_manual_cont = NULL;
static lv_obj_t *s_ta_ssid = NULL;
static lv_obj_t *s_ta_pass = NULL;
static lv_obj_t *s_kb = NULL;

static void show_manual_entry(void);
static void show_menu(void);

// -------------------------------------------------------------------------
// QR Code / AP Mode Placeholders
// -------------------------------------------------------------------------
static void on_qr_click(lv_event_t *e) {
    lv_obj_t *msgbox = lv_msgbox_create(NULL, "Not Available", "Camera driver not found.", NULL, true);
    lv_obj_center(msgbox);
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

    esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, wifi_scan_handler);
}

void page_wifi_init(lv_obj_t *parent)
{
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, wifi_scan_handler, NULL);
    
    // Root container
    lv_obj_t *root = lv_obj_create(parent);
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
