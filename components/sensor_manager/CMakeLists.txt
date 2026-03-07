set(SENSOR_MANAGER_REQUIRES driver)

if(CONFIG_KC_TOUCH_DISPLAY_BACKEND_TAB5)
    list(APPEND SENSOR_MANAGER_REQUIRES M5Unified)
endif()

idf_component_register(
    SRCS "src/sensor_manager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES ${SENSOR_MANAGER_REQUIRES}
)
