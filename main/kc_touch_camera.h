#pragma once

#include <stdbool.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the Tab5 camera stack (esp_video + SC2356 sensor).
 *
 * Starts the SCCB bus, drives the external XCLK via the ESP clock router,
 * and boots the esp_video CSI pipeline so that higher level code can begin
 * requesting frames.
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
