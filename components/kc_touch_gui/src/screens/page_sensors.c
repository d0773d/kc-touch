#include "lvgl.h"
#include "ui_theme.h"

void page_sensors_init(lv_obj_t *parent)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, "Sensor List (To Be Implemented)");
    lv_obj_add_style(lbl, &style_text_title, 0);
    lv_obj_center(lbl);
}
