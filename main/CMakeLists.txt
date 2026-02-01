idf_component_register(SRCS "app_main.c" "tab5_wifi_power.c"
                       PRIV_REQUIRES esp_wifi nvs_flash wifi_provisioning driver
                       INCLUDE_DIRS ".")
