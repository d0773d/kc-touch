set(MAIN_REQUIRES
    wifi_copro_power
    wifi_copro_transport
    wifi_copro_hw
    kc_touch_gui
    kc_touch_display
    sensor_manager
    espressif__esp_video
    espressif__esp_cam_sensor
    espressif__esp_sccb_intf
    espressif__esp_ipa
)

if(CONFIG_KC_TOUCH_DISPLAY_BACKEND_TAB5)
    list(APPEND MAIN_REQUIRES M5Unified)
endif()

idf_component_register(
    SRCS "app_main.c" "kc_touch_camera.cpp"
    PRIV_REQUIRES esp_wifi nvs_flash driver wifi_provisioning
    REQUIRES ${MAIN_REQUIRES}
    INCLUDE_DIRS "."
)
