idf_component_register(SRCS "src/kc_touch_gui.c"
                              "src/app_video.c"
                       INCLUDE_DIRS "include"
                       REQUIRES lvgl esp_timer freertos esp_video lvgl_yaml_gui
                       PRIV_REQUIRES esp_wifi esp_event nvs_flash log quirc)
