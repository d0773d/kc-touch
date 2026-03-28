idf_component_register(
    SRCS
        "src/yui_camera.c"
        "src/yui_camera_stream.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_video freertos
    PRIV_REQUIRES esp_driver_gpio esp_driver_i2c waveshare__esp32_p4_platform
)
