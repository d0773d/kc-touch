idf_component_register(SRCS "src/kc_touch_display.c" "src/kc_touch_display_tab5.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES kc_touch_gui lvgl M5Unified
                       PRIV_REQUIRES esp_timer log)
