#ifndef UI_ROOT_H
#define UI_ROOT_H

#include "lvgl.h"

// Navigation IDs
typedef enum {
    NAV_DASHBOARD = 0,
    NAV_SENSORS,
    NAV_WIFI,
    NAV_SETTINGS,
    NAV_COUNT
} nav_id_t;

void ui_root_init(void);

#endif // UI_ROOT_H
