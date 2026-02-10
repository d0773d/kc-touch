#!/usr/bin/env python3
"""
Refactor qr_scan_task to use app_video wrapper
"""

# Read the file
with open('/home/dottedquad/code/kc-touch/components/kc_touch_gui/src/screens/page_wifi.c', 'r') as f:
    content = f.read()

# Find the start and end of the function
start_marker = "static void qr_scan_task(void *arg)\n{"
end_marker = "    kc_touch_gui_dispatch(qr_teardown_cb, NULL, 0);\n    vTaskDelete(NULL);\n}"

start_idx = content.find(start_marker)
end_idx = content.find(end_marker)

if start_idx == -1 or end_idx == -1:
    print("ERROR: Could not find function markers")
    exit(1)

# Move to after the opening brace
start_idx += len(start_marker)
end_idx += len(end_marker)

# New function implementation
new_implementation = """
    (void)arg;
    
    // Open camera using app_video wrapper
    s_video_fd = app_video_open(QR_DEVICE_PATH, V4L2_PIX_FMT_RGB565);
    if (s_video_fd < 0) {
        ESP_LOGE(TAG, "QR camera open failed");
        qr_post_label("Camera open failed", false);
        goto exit;
    }
    
    // Get current format
    struct v4l2_format fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(s_video_fd, VIDIOC_G_FMT, &fmt) != 0) {
        ESP_LOGE(TAG, "VIDIOC_G_FMT failed: %s", strerror(errno));
        qr_post_label("Unable to query camera format", false);
        goto exit;
    }
    
    s_frame_width = fmt.fmt.pix.width;
    s_frame_height = fmt.fmt.pix.height;
    s_pixel_format = fmt.fmt.pix.pixelformat;
    
    char fourcc[5] = {0};
    fourcc[0] = s_pixel_format & 0xFF;
    fourcc[1] = (s_pixel_format >> 8) & 0xFF;
    fourcc[2] = (s_pixel_format >> 16) & 0xFF;
    fourcc[3] = (s_pixel_format >> 24) & 0xFF;
    ESP_LOGI(TAG, "QR camera mode %ux%u fmt=%s",
             (unsigned)s_frame_width,
             (unsigned)s_frame_height,
             fourcc);
    
    // Calculate decode resolution
    s_decode_width = s_frame_width;
    s_decode_height = s_frame_height;
    while ((s_decode_width > QR_DECODE_TARGET_WIDTH || s_decode_height > QR_DECODE_TARGET_HEIGHT) &&
           (s_decode_width > QR_DECODE_MIN_WIDTH && s_decode_height > QR_DECODE_MIN_HEIGHT)) {
        s_decode_width = (s_decode_width + 1) / 2;
        s_decode_height = (s_decode_height + 1) / 2;
    }
    if (s_decode_width == 0 || s_decode_height == 0) {
        s_decode_width = s_frame_width;
        s_decode_height = s_frame_height;
    }
    ESP_LOGI(TAG, "QR decode surface %ux%u", (unsigned)s_decode_width, (unsigned)s_decode_height);
    
    // Set up app_video buffers (3 buffers with mmap mode)
    if (app_video_set_bufs(s_video_fd, QR_V4L2_BUFFER_COUNT, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "app_video_set_bufs failed");
        qr_post_label("Camera buffer allocation failed", false);
        goto exit;
    }
    
    // Initialize QR decoder
    s_decoder = quirc_new();
    if (!s_decoder) {
        qr_post_label("QR decoder alloc failed", false);
        goto exit;
    }
    if (quirc_resize(s_decoder, s_decode_width, s_decode_height) < 0) {
        qr_post_label("QR decoder resize failed", false);
        goto exit;
    }
    
    s_code = malloc(sizeof(*s_code));
    s_data = malloc(sizeof(*s_data));
    if (!s_code || !s_data) {
        qr_post_label("QR decoder workspace alloc failed", false);
        goto exit;
    }
    
    s_last_preview = 0;
    
    // Register frame operation callback
    app_video_register_frame_operation_cb(qr_frame_operation_cb);
    
    qr_post_label("Point camera at Wi-Fi QR", false);
    
    // Start video stream task
    if (app_video_stream_task_start(s_video_fd, tskNO_AFFINITY) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start video stream task");
        qr_post_label("Unable to start camera stream", false);
        goto exit;
    }
    
    // Wait for stop signal
    while (!s_qr_stop_flag) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // Stop video stream
    app_video_stream_task_stop(s_video_fd);
    app_video_wait_video_stop();
    
exit:
    if (s_decoder) {
        quirc_destroy(s_decoder);
        s_decoder = NULL;
    }
    free(s_code);
    s_code = NULL;
    free(s_data);
    s_data = NULL;
    if (s_video_fd >= 0) {
        app_video_close(s_video_fd);
        s_video_fd = -1;
    }
    
    s_qr_task_handle = NULL;
    s_qr_stop_flag = false;
    kc_touch_gui_dispatch(qr_teardown_cb, NULL, 0);
    vTaskDelete(NULL);
}"""

# Replace the function body
new_content = content[:start_idx] + new_implementation + content[end_idx:]

# Write back
with open('/home/dottedquad/code/kc-touch/components/kc_touch_gui/src/screens/page_wifi.c', 'w') as f:
    f.write(new_content)

print("SUCCESS: qr_scan_task refactored to use app_video wrapper")
