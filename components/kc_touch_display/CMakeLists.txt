set(KC_TOUCH_DISPLAY_SRCS "src/kc_touch_display.c")
set(KC_TOUCH_DISPLAY_REQUIRES kc_touch_gui lvgl)

if(CONFIG_KC_TOUCH_DISPLAY_BACKEND_TAB5)
    list(APPEND KC_TOUCH_DISPLAY_SRCS "src/kc_touch_display_tab5.cpp")
    list(APPEND KC_TOUCH_DISPLAY_REQUIRES M5Unified)
elseif(CONFIG_KC_TOUCH_DISPLAY_BACKEND_WAVESHARE_P4)
    list(APPEND KC_TOUCH_DISPLAY_SRCS "src/kc_touch_display_waveshare_p4.c")
    list(APPEND KC_TOUCH_DISPLAY_REQUIRES esp_lcd)
endif()

idf_component_register(
    SRCS ${KC_TOUCH_DISPLAY_SRCS}
    INCLUDE_DIRS "include"
    REQUIRES ${KC_TOUCH_DISPLAY_REQUIRES}
    PRIV_REQUIRES esp_timer log
)
