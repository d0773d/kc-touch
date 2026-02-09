#ifndef UI_THEME_H
#define UI_THEME_H

#include "lvgl.h"

// Color Palette
#define COLOR_BG        lv_color_hex(0x121212) // Deep Charcoal
#define COLOR_SURFACE   lv_color_hex(0x1E1E1E) // Slightly lighter for sidebar/cards
#define COLOR_PRIMARY   lv_color_hex(0x00E676) // Hydro Green
#define COLOR_SECONDARY lv_color_hex(0x40C4FF) // Hydro Blue
#define COLOR_ALERT     lv_color_hex(0xFF5252) // Red/Pink
#define COLOR_TEXT      lv_color_hex(0xFFFFFF)
#define COLOR_TEXT_DIM  lv_color_hex(0xB0B0B0)

// Standard Dimensions
#define SIDEBAR_WIDTH   100
#define HEADER_HEIGHT   50

// Shared Styles
void ui_theme_init(void);

extern lv_style_t style_screen;
extern lv_style_t style_sidebar;
extern lv_style_t style_sidebar_btn;
extern lv_style_t style_sidebar_btn_checked;
extern lv_style_t style_header;
extern lv_style_t style_card;
extern lv_style_t style_text_title;
extern lv_style_t style_text_body;

#endif // UI_THEME_H
