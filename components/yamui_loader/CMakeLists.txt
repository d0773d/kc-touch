set(YAMUI_LOADER_SRCS
    "src/yamui_loader.c"
    "src/yamui_loader_fs.c"
)

if(CONFIG_YAMUI_LOADER_UART_ENABLE)
    list(APPEND YAMUI_LOADER_SRCS "src/yamui_loader_uart.c")
endif()

if(CONFIG_YAMUI_LOADER_HTTPS_ENABLE)
    list(APPEND YAMUI_LOADER_SRCS "src/yamui_loader_https.c")
endif()

if(CONFIG_YAMUI_LOADER_HTTPD_ENABLE)
    list(APPEND YAMUI_LOADER_SRCS "src/yamui_loader_httpd.c")
endif()

set(YAMUI_LOADER_REQUIRES
    "yaml_core"
    "yaml_ui"
    "lvgl_yaml_gui"
    "kc_touch_gui"
    "ui_schemas"
    "esp_partition"
    "driver"
)

set(YAMUI_LOADER_PRIV_REQUIRES
    "nvs_flash"
)

if(CONFIG_YAMUI_LOADER_HTTPS_ENABLE)
    list(APPEND YAMUI_LOADER_REQUIRES "esp_http_client" "esp-tls")
endif()

# esp_http_server is always required (httpd enabled by default in Kconfig)
list(APPEND YAMUI_LOADER_REQUIRES "esp_http_server")

idf_component_register(
    SRCS ${YAMUI_LOADER_SRCS}
    INCLUDE_DIRS "include"
    REQUIRES ${YAMUI_LOADER_REQUIRES}
    PRIV_REQUIRES ${YAMUI_LOADER_PRIV_REQUIRES}
)
