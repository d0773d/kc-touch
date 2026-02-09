#include "ui_root.h"
#include "ui_theme.h"
#include <stdio.h>

// Forward declarations of page init functions
// We will implement these in separate files later or merge them here temporarily
// external prototypes
void page_dashboard_init(lv_obj_t *parent);
void page_sensors_init(lv_obj_t *parent);
void page_wifi_init(lv_obj_t *parent);
void page_settings_init(lv_obj_t *parent);

// Global references
static lv_obj_t *g_content_obj = NULL;
static lv_obj_t *g_header_title = NULL;
static lv_obj_t *g_active_btn[NAV_COUNT];
static lv_obj_t *g_header_battery_label = NULL;
static lv_obj_t *g_header_wifi_icon = NULL;
static lv_obj_t *g_header_time_label = NULL;

static const char *NAV_TITLES[] = {
    "Dashboard",
    "Sensors",
    "WiFi Connection",
    "Settings"
};

// --- Page Switch Logic ---
static void load_page(nav_id_t id) {
    if (!g_content_obj) return;
    
    // Clear current content
    lv_obj_clean(g_content_obj);
    
    // Update Title
    if (g_header_title) {
        lv_label_set_text(g_header_title, NAV_TITLES[id]);
    }

    // Initialize new page content
    switch(id) {
        case NAV_DASHBOARD: page_dashboard_init(g_content_obj); break;
        case NAV_SENSORS:   page_sensors_init(g_content_obj); break;
        case NAV_WIFI:      page_wifi_init(g_content_obj); break;
        case NAV_SETTINGS:  page_settings_init(g_content_obj); break;
        default: break;
    }
}

// --- Navigation Event Handler ---
static void nav_event_cb(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);
    (void)btn;
    nav_id_t id = (nav_id_t)(intptr_t)lv_event_get_user_data(e);
    
    // Update active state visual
    for(int i=0; i<NAV_COUNT; i++) {
        if(i == id) {
            lv_obj_add_state(g_active_btn[i], LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(g_active_btn[i], LV_STATE_CHECKED);
        }
    }
    
    load_page(id);
}

// --- Status Bar Updates (Public hooks) ---
void ui_status_update_battery(int level, bool charging) {
    if(g_header_battery_label) {
        lv_label_set_text_fmt(g_header_battery_label, "%d%% %s", level, charging ? "⚡" : "");
    }
}

void ui_status_update_wifi(int rssi) {
    // Todo: icon based on RSSI
    if(g_header_wifi_icon) {
        lv_label_set_text(g_header_wifi_icon, LV_SYMBOL_WIFI);
    }
}

void ui_status_update_time(const char* time_str) {
    if(g_header_time_label) {
        lv_label_set_text(g_header_time_label, time_str);
    }
}

// --- Element Builders ---
static lv_obj_t* create_nav_btn(lv_obj_t *parent, const char* icon, const char* label, nav_id_t id) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_add_style(btn, &style_sidebar_btn, 0);
    lv_obj_add_style(btn, &style_sidebar_btn_checked, LV_STATE_CHECKED);
    lv_obj_set_width(btn, LV_PCT(100));
    lv_obj_set_height(btn, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    lv_obj_t *icon_lbl = lv_label_create(btn);
    lv_label_set_text(icon_lbl, icon);
    lv_obj_set_style_text_font(icon_lbl, &lv_font_montserrat_28, 0); // Large icon
    
    lv_obj_t *txt_lbl = lv_label_create(btn);
    lv_label_set_text(txt_lbl, label);
    lv_obj_set_style_text_font(txt_lbl, &lv_font_montserrat_14, 0);
    
    lv_obj_add_event_cb(btn, nav_event_cb, LV_EVENT_CLICKED, (void*)(intptr_t)id);
    
    return btn;
}

// --- Main Init ---
void ui_root_init(void) {
    // 0. Init Theme System
    ui_theme_init();
    
    // 1. Master Container (The physical screen)
    lv_obj_t *scr = lv_scr_act();
    lv_obj_add_style(scr, &style_screen, 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_ROW); // Left-Right Split
    lv_obj_set_style_pad_all(scr, 0, 0);

    // 2. Left Sidebar (Navigation)
    lv_obj_t *sidebar = lv_obj_create(scr);
    lv_obj_add_style(sidebar, &style_sidebar, 0);
    lv_obj_set_width(sidebar, SIDEBAR_WIDTH);
    lv_obj_set_height(sidebar, LV_PCT(100));
    lv_obj_set_flex_flow(sidebar, LV_FLEX_FLOW_COLUMN);
    // Remove scrollbar
    lv_obj_clear_flag(sidebar, LV_OBJ_FLAG_SCROLLABLE);

    // --- Sidebar Buttons ---
    g_active_btn[NAV_DASHBOARD] = create_nav_btn(sidebar, LV_SYMBOL_HOME, "Dash", NAV_DASHBOARD);
    g_active_btn[NAV_SENSORS]   = create_nav_btn(sidebar, LV_SYMBOL_EYE_OPEN, "Sensors", NAV_SENSORS);
    g_active_btn[NAV_WIFI]      = create_nav_btn(sidebar, LV_SYMBOL_WIFI, "WiFi", NAV_WIFI);
    g_active_btn[NAV_SETTINGS]  = create_nav_btn(sidebar, LV_SYMBOL_SETTINGS, "Settings", NAV_SETTINGS);
    
    // Set default active
    lv_obj_add_state(g_active_btn[NAV_DASHBOARD], LV_STATE_CHECKED);

    // 3. Right Main Area (Header + Content)
    lv_obj_t *right_panel = lv_obj_create(scr);
    lv_obj_set_flex_grow(right_panel, 1);
    lv_obj_set_height(right_panel, LV_PCT(100));
    lv_obj_set_flex_flow(right_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(right_panel, 0, 0);
    lv_obj_set_style_bg_opa(right_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_panel, 0, 0);
    lv_obj_clear_flag(right_panel, LV_OBJ_FLAG_SCROLLABLE);

    // --- Header ---
    lv_obj_t *header = lv_obj_create(right_panel);
    lv_obj_add_style(header, &style_header, 0);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, HEADER_HEIGHT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    // Header Title
    g_header_title = lv_label_create(header);
    lv_obj_add_style(g_header_title, &style_text_title, 0);
    lv_label_set_text(g_header_title, "Dashboard");

    // Header Status Icons Container
    lv_obj_t *status_cont = lv_obj_create(header);
    lv_obj_set_size(status_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(status_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(status_cont, 0, 0);
    lv_obj_set_style_pad_gap(status_cont, 15, 0);
    lv_obj_set_style_bg_opa(status_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(status_cont, 0, 0);

    g_header_wifi_icon = lv_label_create(status_cont);
    lv_label_set_text(g_header_wifi_icon, LV_SYMBOL_WIFI);
    
    g_header_battery_label = lv_label_create(status_cont);
    lv_label_set_text(g_header_battery_label, "100%");

    g_header_time_label = lv_label_create(status_cont);
    lv_label_set_text(g_header_time_label, "12:00");

    // --- Content Area ---
    g_content_obj = lv_obj_create(right_panel);
    lv_obj_set_flex_grow(g_content_obj, 1);
    lv_obj_set_width(g_content_obj, LV_PCT(100));
    lv_obj_set_style_bg_opa(g_content_obj, LV_OPA_TRANSP, 0); // content handles its own bg if needed
    lv_obj_set_style_border_width(g_content_obj, 0, 0);
    lv_obj_set_style_pad_all(g_content_obj, 15, 0);
    
    // Load default
    load_page(NAV_DASHBOARD);
}
