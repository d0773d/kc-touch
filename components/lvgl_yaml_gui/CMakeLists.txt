idf_component_register(
    SRCS
        "src/lvgl_yaml_gui.c"
        "src/yui_navigation_queue.c"
    INCLUDE_DIRS "include"
    REQUIRES yaml_core yaml_ui ui_schemas sensor_manager kc_touch_display lvgl esp_timer
)
