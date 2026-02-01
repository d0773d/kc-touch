idf_build_get_property(idf_path IDF_PATH)
set(_orig_dir "${idf_path}/components/wifi_provisioning")

set(srcs
    "${_orig_dir}/src/wifi_config.c"
    "${_orig_dir}/src/wifi_scan.c"
    "${_orig_dir}/src/wifi_ctrl.c"
    "${_orig_dir}/src/manager.c"
    "${_orig_dir}/src/handlers.c"
    "${_orig_dir}/src/scheme_console.c"
    "${_orig_dir}/proto-c/wifi_config.pb-c.c"
    "${_orig_dir}/proto-c/wifi_scan.pb-c.c"
    "${_orig_dir}/proto-c/wifi_ctrl.pb-c.c"
    "${_orig_dir}/proto-c/wifi_constants.pb-c.c")

if(CONFIG_ESP_WIFI_SOFTAP_SUPPORT)
    list(APPEND srcs "${_orig_dir}/src/scheme_softap.c")
endif()

if(CONFIG_BT_ENABLED)
    if(CONFIG_BT_BLUEDROID_ENABLED OR CONFIG_BT_NIMBLE_ENABLED)
        list(APPEND srcs "${_orig_dir}/src/scheme_ble.c")
    endif()
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "${_orig_dir}/include"
    PRIV_INCLUDE_DIRS "${_orig_dir}/src" "${_orig_dir}/proto-c"
    REQUIRES lwip protocomm
    PRIV_REQUIRES protobuf-c bt json esp_timer esp_wifi)

set(_wifi_prov_config_override "${CMAKE_CURRENT_LIST_DIR}/config_overrides.h")
target_compile_options(${COMPONENT_LIB} PRIVATE "-include" "${_wifi_prov_config_override}")
