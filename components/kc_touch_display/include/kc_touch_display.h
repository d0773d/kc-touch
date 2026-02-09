#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the KC Touch display and (optional) touch drivers.
 *
 * The implementation boots the native M5Stack Tab5 (MIPI DSI) display via
 * M5Unified/M5GFX, wires LVGL draw buffers, and enables the integrated touch
 * controller when requested.
 */
esp_err_t kc_touch_display_init(void);

/**
 * @brief Manually toggle the backlight GPIO when available.
 */
esp_err_t kc_touch_display_backlight_set(bool enable);

typedef void (*kc_touch_display_prov_cb_t)(void *ctx);

/**
 * @brief Register a callback invoked when the demo button requests Wi-Fi provisioning.
 */
esp_err_t kc_touch_display_set_provisioning_cb(kc_touch_display_prov_cb_t cb, void *ctx);

/**
 * @brief Update the status text on the display.
 * Thread-safe (dispatches to GUI task).
 */
esp_err_t kc_touch_display_set_status(const char *fmt, ...);

/**
 * @brief Show QR code for provisioning.
 * Clears current screen and displays QR code with instructions.
 */
esp_err_t kc_touch_display_show_qr(const char *payload);

/**
 * @brief Enable or disable the Back button on the provisioning screen.
 * Thread-safe.
 */
esp_err_t kc_touch_display_prov_enable_back(bool enable);

typedef void (*kc_touch_display_cancel_cb_t)(void *ctx);

/**
 * @brief Register a callback for when the user cancels provisioning (Back button).
 */
esp_err_t kc_touch_display_set_cancel_cb(kc_touch_display_cancel_cb_t cb, void *ctx);

/**
 * @brief Reset internal UI text/button references.
 * Call this when the screen is cleared or switched externally to prevent
 * updates to invalid objects.
 */
void kc_touch_display_reset_ui_state(void);

bool kc_touch_display_is_ready(void);
bool kc_touch_touch_is_ready(void);

#ifdef __cplusplus
}
#endif
