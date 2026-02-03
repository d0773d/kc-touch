idf_component_register(SRCS "src/wifi_copro_power.c"
                       INCLUDE_DIRS "include"
                       REQUIRES wifi_copro_hw driver freertos
                       PRIV_REQUIRES esp_common)
