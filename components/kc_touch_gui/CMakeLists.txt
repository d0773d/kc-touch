idf_component_register(SRCS "src/kc_touch_gui.c"
                              "src/screens/ui_theme.c"
                              "src/screens/ui_root.c"
                              "src/screens/page_dashboard.c"
                              "src/screens/page_sensors.c"
                              "src/screens/page_wifi.c"
                              "src/screens/page_settings.c"
                       INCLUDE_DIRS "include"
                       REQUIRES lvgl esp_timer freertos
                       PRIV_REQUIRES esp_wifi esp_event nvs_flash log)
