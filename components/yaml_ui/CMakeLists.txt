idf_component_register(
    SRCS
        "src/yaml_ui.c"
        "src/yamui_state.c"
        "src/yamui_events.c"
        "src/yamui_expr.c"
        "src/yamui_runtime.c"
    INCLUDE_DIRS "include"
    REQUIRES yaml_core freertos
)
