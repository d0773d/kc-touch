idf_component_register(SRCS "app_main.c"
                       PRIV_REQUIRES esp_wifi nvs_flash driver wifi_provisioning
                       REQUIRES wifi_copro_power wifi_copro_transport wifi_copro_hw kc_touch_gui kc_touch_display
                       INCLUDE_DIRS ".")
