#include "yui_fonts.h"

#include <string.h>

const lv_font_t *yui_font_default(void)
{
    return &yui_font_14;
}

const lv_font_t *yui_font_pick(int32_t font_size, int32_t font_weight, const char *font_family)
{
    (void)font_weight;
    (void)font_family;

    if (font_size >= 26) {
        return &yui_font_32;
    }
    if (font_size >= 18) {
        return &yui_font_20;
    }
    return &yui_font_14;
}
