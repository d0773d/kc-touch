idf_component_register(SRCS "src/wifi_copro_hw.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver esp_driver_gpio esp_driver_i2c)
