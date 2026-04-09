#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

LV_FONT_DECLARE(yui_font_14);
LV_FONT_DECLARE(yui_font_20);
LV_FONT_DECLARE(yui_font_32);

const lv_font_t *yui_font_pick(int32_t font_size, int32_t font_weight, const char *font_family);
const lv_font_t *yui_font_default(void);

#ifdef __cplusplus
}
#endif
