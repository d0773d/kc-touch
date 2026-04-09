set(SENSOR_MANAGER_REQUIRES driver)

idf_component_register(
    SRCS "src/sensor_manager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES ${SENSOR_MANAGER_REQUIRES}
)
