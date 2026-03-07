set(KC_TOUCH_DISPLAY_SRCS
    "src/kc_touch_display.c"
    "src/kc_touch_display_waveshare_p4.c"
)
set(KC_TOUCH_DISPLAY_REQUIRES
    kc_touch_gui
    lvgl
    esp_lcd
    esp_driver_gpio
    esp_hw_support
)

idf_component_register(
    SRCS ${KC_TOUCH_DISPLAY_SRCS}
    INCLUDE_DIRS "include"
    REQUIRES ${KC_TOUCH_DISPLAY_REQUIRES}
    PRIV_REQUIRES esp_timer log
)
