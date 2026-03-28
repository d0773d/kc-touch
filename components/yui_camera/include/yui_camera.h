#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    YUI_CAMERA_FMT_RAW8 = 0,
    YUI_CAMERA_FMT_RAW10,
    YUI_CAMERA_FMT_GREY,
    YUI_CAMERA_FMT_RGB565,
    YUI_CAMERA_FMT_RGB888,
    YUI_CAMERA_FMT_YUV422,
    YUI_CAMERA_FMT_YUV420,
} yui_camera_pixel_format_t;

typedef void (*yui_camera_frame_cb_t)(uint8_t *frame_buf,
                                      uint8_t frame_index,
                                      uint32_t width,
                                      uint32_t height,
                                      size_t frame_len);

esp_err_t yui_camera_init(void);
esp_err_t yui_camera_deinit(void);
bool yui_camera_is_ready(void);
const char *yui_camera_device_path(void);

int yui_camera_stream_open(const char *device_path, yui_camera_pixel_format_t init_fmt);
esp_err_t yui_camera_stream_set_buffers(int video_fd, uint32_t fb_num, const void **fb);
esp_err_t yui_camera_stream_get_buffers(int fb_num, void **fb);
esp_err_t yui_camera_stream_register_frame_cb(yui_camera_frame_cb_t frame_cb);
esp_err_t yui_camera_stream_start(int video_fd, int core_id);
esp_err_t yui_camera_stream_stop(int video_fd);
esp_err_t yui_camera_stream_wait_for_stop(void);
esp_err_t yui_camera_stream_close(int video_fd);

#ifdef __cplusplus
}
#endif
