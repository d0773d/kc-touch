idf_component_register(SRCS "src/kc_touch_provisioning.c"
                       INCLUDE_DIRS "include"
                       REQUIRES wifi_provisioning esp_wifi qrcode)
