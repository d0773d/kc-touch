#include "lvgl.h"
#include "ui_theme.h"

void page_settings_init(lv_obj_t *parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    
    // Setting Item 1
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_add_style(cont, &style_card, 0);
    lv_obj_set_width(cont, LV_PCT(100));
    lv_obj_set_height(cont, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    lv_obj_t *lbl = lv_label_create(cont);
    lv_label_set_text(lbl, "Pump Always On");
    lv_obj_add_style(lbl, &style_text_body, 0);
    
    lv_obj_t *sw = lv_switch_create(cont);
    lv_obj_add_state(sw, LV_STATE_CHECKED); // Default on
}
