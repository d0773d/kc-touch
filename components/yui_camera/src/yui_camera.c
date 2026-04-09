#include "yui_camera.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "bsp/esp-bsp.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_video_device.h"
#include "esp_video_init.h"
#include "linux/videodev2.h"
#include "sdkconfig.h"

#if CONFIG_YUI_CAMERA_INTERFACE_MIPI_CSI
#include "esp_cam_sensor_xclk.h"
#endif

static const char *TAG = "yui_camera";
static bool s_is_ready;

#if CONFIG_YUI_CAMERA_INTERFACE_MIPI_CSI
static esp_cam_sensor_xclk_handle_t s_xclk_handle;
#endif

static esp_err_t yui_camera_probe_device(const char *device_path)
{
    if (!device_path) {
        return ESP_ERR_INVALID_ARG;
    }

    int fd = open(device_path, O_RDONLY);
    if (fd < 0) {
        ESP_LOGE(TAG, "Open video device failed during probe: %s", device_path);
        return ESP_ERR_NOT_FOUND;
    }

    struct v4l2_capability capability = {0};
    if (ioctl(fd, VIDIOC_QUERYCAP, &capability) != 0) {
        ESP_LOGE(TAG, "VIDIOC_QUERYCAP failed during probe");
        close(fd);
        return ESP_FAIL;
    }

    struct v4l2_format format = {
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
    };
    if (ioctl(fd, VIDIOC_G_FMT, &format) != 0) {
        ESP_LOGE(TAG, "VIDIOC_G_FMT failed during probe");
        close(fd);
        return ESP_FAIL;
    }

    close(fd);
    return ESP_OK;
}

static esp_err_t yui_camera_validate_pins(void)
{
#if !CONFIG_YUI_CAMERA_ENABLE
    return ESP_ERR_NOT_SUPPORTED;
#elif CONFIG_YUI_CAMERA_INTERFACE_DVP
    if (CONFIG_YUI_CAMERA_SCCB_SCL_PIN < 0 || CONFIG_YUI_CAMERA_SCCB_SDA_PIN < 0 ||
        CONFIG_YUI_CAMERA_DVP_XCLK_PIN < 0 || CONFIG_YUI_CAMERA_DVP_PCLK_PIN < 0 ||
        CONFIG_YUI_CAMERA_DVP_VSYNC_PIN < 0 || CONFIG_YUI_CAMERA_DVP_DE_PIN < 0) {
        ESP_LOGW(TAG, "DVP camera pins are not configured");
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
#elif CONFIG_YUI_CAMERA_INTERFACE_MIPI_CSI
    if (CONFIG_YUI_CAMERA_SCCB_SCL_PIN < 0 || CONFIG_YUI_CAMERA_SCCB_SDA_PIN < 0) {
        ESP_LOGW(TAG, "MIPI-CSI SCCB pins are not configured");
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t yui_camera_init(void)
{
#if !CONFIG_YUI_CAMERA_ENABLE
    ESP_LOGI(TAG, "Camera support is disabled in Kconfig");
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (s_is_ready) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(yui_camera_validate_pins(), TAG, "invalid camera configuration");

#if CONFIG_YUI_CAMERA_INTERFACE_MIPI_CSI
    const esp_video_init_csi_config_t csi_config = {
        .sccb_config = {
            .init_sccb = false,
            .i2c_handle = NULL,
            .freq = CONFIG_YUI_CAMERA_SCCB_I2C_FREQ,
        },
        .reset_pin = CONFIG_YUI_CAMERA_RESET_PIN,
        .pwdn_pin = CONFIG_YUI_CAMERA_PWDN_PIN,
    };
    esp_video_init_csi_config_t csi_runtime_config = csi_config;

    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "initialize shared BSP I2C bus");
    csi_runtime_config.sccb_config.i2c_handle = bsp_i2c_get_handle();
    ESP_RETURN_ON_FALSE(csi_runtime_config.sccb_config.i2c_handle != NULL,
                        ESP_ERR_INVALID_STATE,
                        TAG,
                        "shared BSP I2C bus is unavailable");

    const esp_video_init_config_t init_config = {
        .csi = &csi_runtime_config,
    };

    if (CONFIG_YUI_CAMERA_MIPI_XCLK_PIN >= 0) {
        esp_cam_sensor_xclk_config_t xclk_config = {
            .esp_clock_router_cfg = {
                .xclk_pin = CONFIG_YUI_CAMERA_MIPI_XCLK_PIN,
                .xclk_freq_hz = CONFIG_YUI_CAMERA_MIPI_XCLK_FREQ,
            },
        };

        ESP_RETURN_ON_ERROR(
            esp_cam_sensor_xclk_allocate(ESP_CAM_SENSOR_XCLK_ESP_CLOCK_ROUTER, &s_xclk_handle),
            TAG, "allocate MIPI xclk");
        esp_err_t xclk_err = esp_cam_sensor_xclk_start(s_xclk_handle, &xclk_config);
        if (xclk_err != ESP_OK) {
            esp_cam_sensor_xclk_free(s_xclk_handle);
            s_xclk_handle = NULL;
            return xclk_err;
        }
    }

    esp_err_t ret = esp_video_init(&init_config);
    if (ret != ESP_OK) {
        if (s_xclk_handle) {
            esp_cam_sensor_xclk_stop(s_xclk_handle);
            esp_cam_sensor_xclk_free(s_xclk_handle);
            s_xclk_handle = NULL;
        }
        return ret;
    }

    ret = yui_camera_probe_device(yui_camera_device_path());
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Camera probe failed after esp_video_init");
        esp_video_deinit();
        if (s_xclk_handle) {
            esp_cam_sensor_xclk_stop(s_xclk_handle);
            esp_cam_sensor_xclk_free(s_xclk_handle);
            s_xclk_handle = NULL;
        }
        return ret;
    }
#elif CONFIG_YUI_CAMERA_INTERFACE_DVP
    const esp_video_init_dvp_config_t dvp_config = {
        .sccb_config = {
            .init_sccb = true,
            .i2c_config = {
                .port = CONFIG_YUI_CAMERA_SCCB_I2C_PORT,
                .scl_pin = CONFIG_YUI_CAMERA_SCCB_SCL_PIN,
                .sda_pin = CONFIG_YUI_CAMERA_SCCB_SDA_PIN,
            },
            .freq = CONFIG_YUI_CAMERA_SCCB_I2C_FREQ,
        },
        .reset_pin = CONFIG_YUI_CAMERA_RESET_PIN,
        .pwdn_pin = CONFIG_YUI_CAMERA_PWDN_PIN,
        .dvp_pin = {
            .data_width = CAM_CTLR_DATA_WIDTH_8,
            .data_io = {
                CONFIG_YUI_CAMERA_DVP_D0_PIN,
                CONFIG_YUI_CAMERA_DVP_D1_PIN,
                CONFIG_YUI_CAMERA_DVP_D2_PIN,
                CONFIG_YUI_CAMERA_DVP_D3_PIN,
                CONFIG_YUI_CAMERA_DVP_D4_PIN,
                CONFIG_YUI_CAMERA_DVP_D5_PIN,
                CONFIG_YUI_CAMERA_DVP_D6_PIN,
                CONFIG_YUI_CAMERA_DVP_D7_PIN,
            },
            .vsync_io = CONFIG_YUI_CAMERA_DVP_VSYNC_PIN,
            .de_io = CONFIG_YUI_CAMERA_DVP_DE_PIN,
            .pclk_io = CONFIG_YUI_CAMERA_DVP_PCLK_PIN,
            .xclk_io = CONFIG_YUI_CAMERA_DVP_XCLK_PIN,
        },
        .xclk_freq = CONFIG_YUI_CAMERA_DVP_XCLK_FREQ,
    };

    const esp_video_init_config_t init_config = {
        .dvp = &dvp_config,
    };

    ESP_RETURN_ON_ERROR(esp_video_init(&init_config), TAG, "initialize DVP camera");
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif

    s_is_ready = true;
    ESP_LOGI(TAG, "Camera initialized on %s", yui_camera_device_path());
    return ESP_OK;
#endif
}

esp_err_t yui_camera_deinit(void)
{
#if !CONFIG_YUI_CAMERA_ENABLE
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!s_is_ready) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(esp_video_deinit(), TAG, "deinitialize video");

#if CONFIG_YUI_CAMERA_INTERFACE_MIPI_CSI
    if (s_xclk_handle) {
        ESP_RETURN_ON_ERROR(esp_cam_sensor_xclk_stop(s_xclk_handle), TAG, "stop xclk");
        ESP_RETURN_ON_ERROR(esp_cam_sensor_xclk_free(s_xclk_handle), TAG, "free xclk");
        s_xclk_handle = NULL;
    }
#endif

    s_is_ready = false;
    return ESP_OK;
#endif
}

bool yui_camera_is_ready(void)
{
    return s_is_ready;
}

const char *yui_camera_device_path(void)
{
#if CONFIG_YUI_CAMERA_INTERFACE_MIPI_CSI
    return ESP_VIDEO_MIPI_CSI_DEVICE_NAME;
#elif CONFIG_YUI_CAMERA_INTERFACE_DVP
    return ESP_VIDEO_DVP_DEVICE_NAME;
#else
    return NULL;
#endif
}

const char *yui_camera_preview_device_path(void)
{
    /* esp_video's ISP node is metadata/statistics-oriented on this platform.
     * Live preview frames still come from the camera capture device path. */
    return yui_camera_device_path();
}
