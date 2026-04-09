#pragma once

#include <stdbool.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the camera stack when supported by the active backend.
 *
 * Backends that do not provide a camera implementation should return
 * `ESP_ERR_NOT_SUPPORTED`.
 */
esp_err_t kc_touch_camera_init(void);

/**
 * @brief Shut down the camera pipeline and release clocks/buses.
 */
esp_err_t kc_touch_camera_deinit(void);

/**
 * @brief Returns true once the video pipeline has been initialized.
 */
bool kc_touch_camera_ready(void);

#ifdef __cplusplus
}
#endif
