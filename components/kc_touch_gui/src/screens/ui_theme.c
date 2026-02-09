#include "ui_theme.h"

// LV_FONT_DECLARE(lv_font_montserrat_20); // Not compiled in yet

lv_style_t style_screen;
lv_style_t style_sidebar;
lv_style_t style_sidebar_btn;
lv_style_t style_sidebar_btn_checked;
lv_style_t style_header;
lv_style_t style_card;
lv_style_t style_text_title;
lv_style_t style_text_body;

void ui_theme_init(void)
{
    // Screen (Background)
    lv_style_init(&style_screen);
    lv_style_set_bg_color(&style_screen, COLOR_BG);
    lv_style_set_bg_opa(&style_screen, LV_OPA_COVER);
    lv_style_set_text_color(&style_screen, COLOR_TEXT);

    // Sidebar
    lv_style_init(&style_sidebar);
    lv_style_set_bg_color(&style_sidebar, COLOR_SURFACE);
    lv_style_set_bg_opa(&style_sidebar, LV_OPA_COVER);
    lv_style_set_border_width(&style_sidebar, 1);
    lv_style_set_border_color(&style_sidebar, lv_color_darken(COLOR_SURFACE, 20));
    lv_style_set_border_side(&style_sidebar, LV_BORDER_SIDE_RIGHT);
    lv_style_set_pad_all(&style_sidebar, 5);
    lv_style_set_pad_row(&style_sidebar, 10);

    // Sidebar Buttons
    lv_style_init(&style_sidebar_btn);
    lv_style_set_bg_opa(&style_sidebar_btn, LV_OPA_TRANSP);
    lv_style_set_border_width(&style_sidebar_btn, 0);
    lv_style_set_radius(&style_sidebar_btn, 8);
    lv_style_set_text_color(&style_sidebar_btn, COLOR_TEXT_DIM);
    lv_style_set_pad_all(&style_sidebar_btn, 10);

    // Sidebar Buttons (Checked/Active)
    lv_style_init(&style_sidebar_btn_checked);
    lv_style_set_bg_opa(&style_sidebar_btn_checked, LV_OPA_20);
    lv_style_set_bg_color(&style_sidebar_btn_checked, COLOR_PRIMARY);
    lv_style_set_text_color(&style_sidebar_btn_checked, COLOR_PRIMARY);

    // Header
    lv_style_init(&style_header);
    lv_style_set_bg_opa(&style_header, LV_OPA_0); // Transparent to show BG color
    lv_style_set_pad_hor(&style_header, 20);
    lv_style_set_pad_ver(&style_header, 10);
    lv_style_set_border_width(&style_header, 1);
    lv_style_set_border_color(&style_header, lv_color_darken(COLOR_BG, 10));
    lv_style_set_border_side(&style_header, LV_BORDER_SIDE_BOTTOM);

    // Dashboard Cards
    lv_style_init(&style_card);
    lv_style_set_bg_color(&style_card, COLOR_SURFACE);
    lv_style_set_bg_opa(&style_card, LV_OPA_COVER);
    lv_style_set_radius(&style_card, 12);
    lv_style_set_shadow_width(&style_card, 20);
    lv_style_set_shadow_color(&style_card, lv_color_black());
    lv_style_set_shadow_opa(&style_card, LV_OPA_30);
    lv_style_set_pad_all(&style_card, 15);

    // Typography
    lv_style_init(&style_text_title);
    lv_style_set_text_font(&style_text_title, &lv_font_montserrat_28);
    
    lv_style_init(&style_text_body);
    lv_style_set_text_font(&style_text_body, &lv_font_montserrat_14);
}
