idf_component_register(SRCS "app_main.c" "wifi_copro_power.c" "wifi_copro_transport.c"
                       PRIV_REQUIRES esp_wifi nvs_flash wifi_provisioning driver
                       INCLUDE_DIRS ".")
