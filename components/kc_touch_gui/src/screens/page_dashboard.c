#include "lvgl.h"
#include "ui_theme.h"

void page_dashboard_init(lv_obj_t *parent)
{
    // Simple Grid Layout for Quick Status
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(parent, 20, 0);

    // Card 1: System Health
    lv_obj_t *card1 = lv_obj_create(parent);
    lv_obj_add_style(card1, &style_card, 0);
    lv_obj_set_size(card1, 300, 150);
    
    lv_obj_t *lbl1 = lv_label_create(card1);
    lv_label_set_text(lbl1, "System Health");
    lv_obj_add_style(lbl1, &style_text_title, 0);
    lv_obj_set_style_text_color(lbl1, COLOR_PRIMARY, 0);
    
    lv_obj_t *lbl_stat = lv_label_create(card1);
    lv_label_set_text(lbl_stat, "Normal Operation\nAll sensors active.");
    lv_obj_add_style(lbl_stat, &style_text_body, 0);
    lv_obj_align(lbl_stat, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    // Card 2: Environment
    lv_obj_t *card2 = lv_obj_create(parent);
    lv_obj_add_style(card2, &style_card, 0);
    lv_obj_set_size(card2, 300, 150);
    
    lv_obj_t *lbl2 = lv_label_create(card2);
    lv_label_set_text(lbl2, "Environment");
    lv_obj_add_style(lbl2, &style_text_title, 0);
    lv_obj_set_style_text_color(lbl2, COLOR_SECONDARY, 0);

    lv_obj_t *lbl_env = lv_label_create(card2);
    lv_label_set_text(lbl_env, "Air Temp: 24.5 C\nHumidity: 60%");
    lv_obj_add_style(lbl_env, &style_text_body, 0);
    lv_obj_align(lbl_env, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}
