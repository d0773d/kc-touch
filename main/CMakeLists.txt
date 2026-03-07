idf_component_register(
    SRCS "app_main.c"
    PRIV_REQUIRES nvs_flash
    REQUIRES
        kc_touch_gui
        kc_touch_display
        sensor_manager
    INCLUDE_DIRS "."
)
