idf_component_register(SRCS "src/wifi_copro_power.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES wifi_copro_hw driver freertos
                       PRIV_REQUIRES M5Unified esp_common)
