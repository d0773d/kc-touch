#include "yui_camera.h"

#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "linux/videodev2.h"

static const char *TAG = "yui_cam_stream";

#define YUI_CAMERA_MAX_BUFFER_COUNT 6
#define YUI_CAMERA_MIN_BUFFER_COUNT 2
#define YUI_CAMERA_TASK_STACK_SIZE (16 * 1024)
#define YUI_CAMERA_TASK_PRIORITY 5

typedef struct {
    uint8_t *camera_buffer[YUI_CAMERA_MAX_BUFFER_COUNT];
    size_t camera_buf_size;
    uint32_t camera_buf_width;
    uint32_t camera_buf_height;
    struct v4l2_buffer v4l2_buf;
    uint8_t camera_mem_mode;
    yui_camera_frame_cb_t frame_cb;
    TaskHandle_t stream_task_handle;
    bool stop_requested;
    SemaphoreHandle_t stop_sem;
} yui_camera_stream_runtime_t;

static yui_camera_stream_runtime_t s_stream = {0};

static uint32_t yui_camera_to_v4l2_format(yui_camera_pixel_format_t format)
{
    switch (format) {
    case YUI_CAMERA_FMT_RAW8:
        return V4L2_PIX_FMT_SBGGR8;
    case YUI_CAMERA_FMT_RAW10:
        return V4L2_PIX_FMT_SBGGR10;
    case YUI_CAMERA_FMT_GREY:
        return V4L2_PIX_FMT_GREY;
    case YUI_CAMERA_FMT_RGB565:
        return V4L2_PIX_FMT_RGB565;
    case YUI_CAMERA_FMT_RGB888:
        return V4L2_PIX_FMT_RGB24;
    case YUI_CAMERA_FMT_YUV422:
        return V4L2_PIX_FMT_YUV422P;
    case YUI_CAMERA_FMT_YUV420:
        return V4L2_PIX_FMT_YUV420;
    default:
        return V4L2_PIX_FMT_RGB565;
    }
}

int yui_camera_stream_open(const char *device_path, yui_camera_pixel_format_t init_fmt)
{
    if (!device_path) {
        return -1;
    }

    struct v4l2_format default_format = {0};
    struct v4l2_capability capability = {0};
    const int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    int fd = open(device_path, O_RDONLY);
    if (fd < 0) {
        ESP_LOGE(TAG, "Open video device failed: %s", device_path);
        return -1;
    }

    if (ioctl(fd, VIDIOC_QUERYCAP, &capability) != 0) {
        ESP_LOGE(TAG, "failed to query capability");
        close(fd);
        return -1;
    }

    default_format.type = type;
    if (ioctl(fd, VIDIOC_G_FMT, &default_format) != 0) {
        ESP_LOGE(TAG, "failed to get format");
        close(fd);
        return -1;
    }

    s_stream.camera_buf_width = default_format.fmt.pix.width;
    s_stream.camera_buf_height = default_format.fmt.pix.height;

    const uint32_t pixel_format = yui_camera_to_v4l2_format(init_fmt);
    if (default_format.fmt.pix.pixelformat != pixel_format) {
        struct v4l2_format format = {
            .type = type,
            .fmt.pix.width = default_format.fmt.pix.width,
            .fmt.pix.height = default_format.fmt.pix.height,
            .fmt.pix.pixelformat = pixel_format,
        };

        if (ioctl(fd, VIDIOC_S_FMT, &format) != 0) {
            ESP_LOGE(TAG, "failed to set format");
            close(fd);
            return -1;
        }
    }

    if (!s_stream.stop_sem) {
        s_stream.stop_sem = xSemaphoreCreateBinary();
    }

    return fd;
}

esp_err_t yui_camera_stream_set_buffers(int video_fd, uint32_t fb_num, const void **fb)
{
    if (fb_num > YUI_CAMERA_MAX_BUFFER_COUNT || fb_num < YUI_CAMERA_MIN_BUFFER_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    struct v4l2_requestbuffers req = {0};
    const int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.count = fb_num;
    req.type = type;
    s_stream.camera_mem_mode = req.memory = fb ? V4L2_MEMORY_USERPTR : V4L2_MEMORY_MMAP;

    if (ioctl(video_fd, VIDIOC_REQBUFS, &req) != 0) {
        ESP_LOGE(TAG, "request buffers failed");
        close(video_fd);
        return ESP_FAIL;
    }

    for (uint32_t i = 0; i < fb_num; ++i) {
        struct v4l2_buffer buf = {0};
        buf.type = type;
        buf.memory = req.memory;
        buf.index = i;

        if (ioctl(video_fd, VIDIOC_QUERYBUF, &buf) != 0) {
            ESP_LOGE(TAG, "query buffer failed");
            close(video_fd);
            return ESP_FAIL;
        }

        if (req.memory == V4L2_MEMORY_MMAP) {
            s_stream.camera_buffer[i] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, video_fd, buf.m.offset);
            if (!s_stream.camera_buffer[i]) {
                ESP_LOGE(TAG, "mmap failed");
                close(video_fd);
                return ESP_FAIL;
            }
        } else {
            if (!fb[i]) {
                ESP_LOGE(TAG, "frame buffer is NULL");
                close(video_fd);
                return ESP_FAIL;
            }
            buf.m.userptr = (unsigned long)fb[i];
            s_stream.camera_buffer[i] = (uint8_t *)fb[i];
        }

        s_stream.camera_buf_size = buf.length;

        if (ioctl(video_fd, VIDIOC_QBUF, &buf) != 0) {
            ESP_LOGE(TAG, "queue frame buffer failed");
            close(video_fd);
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

esp_err_t yui_camera_stream_get_buffers(int fb_num, void **fb)
{
    if (fb_num > YUI_CAMERA_MAX_BUFFER_COUNT || fb_num < YUI_CAMERA_MIN_BUFFER_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    for (int i = 0; i < fb_num; ++i) {
        if (!s_stream.camera_buffer[i]) {
            return ESP_FAIL;
        }
        fb[i] = s_stream.camera_buffer[i];
    }

    return ESP_OK;
}

esp_err_t yui_camera_stream_register_frame_cb(yui_camera_frame_cb_t frame_cb)
{
    s_stream.frame_cb = frame_cb;
    return ESP_OK;
}

static esp_err_t yui_camera_receive_frame(int video_fd)
{
    memset(&s_stream.v4l2_buf, 0, sizeof(s_stream.v4l2_buf));
    s_stream.v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    s_stream.v4l2_buf.memory = s_stream.camera_mem_mode;
    return ioctl(video_fd, VIDIOC_DQBUF, &s_stream.v4l2_buf) == 0 ? ESP_OK : ESP_FAIL;
}

static esp_err_t yui_camera_release_frame(int video_fd)
{
    return ioctl(video_fd, VIDIOC_QBUF, &s_stream.v4l2_buf) == 0 ? ESP_OK : ESP_FAIL;
}

static void yui_camera_dispatch_frame(void)
{
    if (!s_stream.frame_cb) {
        return;
    }

    const uint8_t buf_index = s_stream.v4l2_buf.index;
    s_stream.v4l2_buf.m.userptr = (unsigned long)s_stream.camera_buffer[buf_index];
    s_stream.v4l2_buf.length = s_stream.camera_buf_size;
    s_stream.frame_cb(s_stream.camera_buffer[buf_index],
                      buf_index,
                      s_stream.camera_buf_width,
                      s_stream.camera_buf_height,
                      s_stream.camera_buf_size);
}

static void yui_camera_stream_task(void *arg)
{
    int video_fd = *((int *)arg);

    while (true) {
        ESP_ERROR_CHECK(yui_camera_receive_frame(video_fd));
        yui_camera_dispatch_frame();
        ESP_ERROR_CHECK(yui_camera_release_frame(video_fd));

        if (s_stream.stop_requested) {
            s_stream.stop_requested = false;
            int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            ESP_ERROR_CHECK(ioctl(video_fd, VIDIOC_STREAMOFF, &type) == 0 ? ESP_OK : ESP_FAIL);
            xSemaphoreGive(s_stream.stop_sem);
            vTaskDelete(NULL);
        }
    }
}

esp_err_t yui_camera_stream_start(int video_fd, int core_id)
{
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ESP_RETURN_ON_FALSE(ioctl(video_fd, VIDIOC_STREAMON, &type) == 0, ESP_FAIL, TAG, "start stream");

    BaseType_t result = xTaskCreatePinnedToCore(yui_camera_stream_task,
                                                "yui_cam_stream",
                                                YUI_CAMERA_TASK_STACK_SIZE,
                                                &video_fd,
                                                YUI_CAMERA_TASK_PRIORITY,
                                                &s_stream.stream_task_handle,
                                                core_id);
    return result == pdPASS ? ESP_OK : ESP_FAIL;
}

esp_err_t yui_camera_stream_stop(int video_fd)
{
    (void)video_fd;
    s_stream.stop_requested = true;
    return ESP_OK;
}

esp_err_t yui_camera_stream_wait_for_stop(void)
{
    return (s_stream.stop_sem && xSemaphoreTake(s_stream.stop_sem, portMAX_DELAY) == pdTRUE) ? ESP_OK : ESP_FAIL;
}

esp_err_t yui_camera_stream_close(int video_fd)
{
    close(video_fd);
    return ESP_OK;
}
