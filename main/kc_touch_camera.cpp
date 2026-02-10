#include "kc_touch_camera.h"

#include <M5Unified.h>

#include <driver/i2c_master.h>
#include <esp_cam_sensor_xclk.h>
#include <esp_check.h>
#include <esp_log.h>
#include <esp_video_init.h>
#include <hal/gpio_types.h>

#define TAB5_CAM_SCCB_FREQ_HZ         400000
#define TAB5_CAM_XCLK_GPIO            GPIO_NUM_36
#define TAB5_CAM_XCLK_FREQ_HZ         24000000
#define TAB5_CAM_RESET_GPIO           ((gpio_num_t)-1)
#define TAB5_CAM_PWDN_GPIO            ((gpio_num_t)-1)

static const char *TAG = "kc_camera";
static esp_cam_sensor_xclk_handle_t s_xclk_handle;
static bool s_camera_ready;

static esp_video_init_csi_config_t s_tab5_csi_config = {
    .sccb_config = {
        .init_sccb = false,
        .i2c_handle = nullptr,
        .freq = TAB5_CAM_SCCB_FREQ_HZ,
    },
    .reset_pin = TAB5_CAM_RESET_GPIO,
    .pwdn_pin = TAB5_CAM_PWDN_GPIO,
    .dont_init_ldo = true,
};

static const esp_video_init_config_t s_video_config = {
    .csi = &s_tab5_csi_config,
};

static esp_err_t stop_xclk(void)
{
    if (!s_xclk_handle) {
        return ESP_OK;
    }

    esp_err_t err = esp_cam_sensor_xclk_stop(s_xclk_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to stop camera XCLK (%s)", esp_err_to_name(err));
    }

    err = esp_cam_sensor_xclk_free(s_xclk_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to free camera XCLK (%s)", esp_err_to_name(err));
    }
    s_xclk_handle = nullptr;
    return err;
}

static esp_err_t configure_shared_sccb(void)
{
    i2c_master_bus_handle_t shared_bus = M5.In_I2C.getBusHandle();

    if (!shared_bus || !M5.In_I2C.isEnabled()) {
        ESP_LOGE(TAG, "Internal I2C bus unavailable; camera SCCB cannot attach");
        return ESP_ERR_INVALID_STATE;
    }

    s_tab5_csi_config.sccb_config.init_sccb = false;
    s_tab5_csi_config.sccb_config.i2c_handle = shared_bus;
    s_tab5_csi_config.sccb_config.freq = TAB5_CAM_SCCB_FREQ_HZ;
    return ESP_OK;
}

extern "C" esp_err_t kc_touch_camera_init(void)
{
    if (s_camera_ready) {
        return ESP_OK;
    }

    esp_err_t err = configure_shared_sccb();
    if (err != ESP_OK) {
        return err;
    }

    err = esp_cam_sensor_xclk_allocate(ESP_CAM_SENSOR_XCLK_ESP_CLOCK_ROUTER, &s_xclk_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to allocate camera XCLK (%s)", esp_err_to_name(err));
        return err;
    }

    const esp_cam_sensor_xclk_config_t xclk_cfg = {
        .esp_clock_router_cfg = {
            .xclk_pin = TAB5_CAM_XCLK_GPIO,
            .xclk_freq_hz = TAB5_CAM_XCLK_FREQ_HZ,
        },
    };

    err = esp_cam_sensor_xclk_start(s_xclk_handle, &xclk_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start camera XCLK (%s)", esp_err_to_name(err));
        stop_xclk();
        return err;
    }

    err = esp_video_init(&s_video_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_video_init failed (%s)", esp_err_to_name(err));
        stop_xclk();
        return err;
    }

    s_camera_ready = true;
    const i2c_port_t shared_port = M5.In_I2C.getPort();
    const int scl_pin = M5.In_I2C.getSCL();
    const int sda_pin = M5.In_I2C.getSDA();
    ESP_LOGI(TAG, "Tab5 CSI ready (I2C%d SCL=%d SDA=%d @%d Hz, XCLK GPIO%d @%d Hz)",
             (int)shared_port,
             scl_pin,
             sda_pin,
             TAB5_CAM_SCCB_FREQ_HZ,
             TAB5_CAM_XCLK_GPIO,
             TAB5_CAM_XCLK_FREQ_HZ);
    return ESP_OK;
}

extern "C" esp_err_t kc_touch_camera_deinit(void)
{
    if (!s_camera_ready) {
        return ESP_OK;
    }

    esp_err_t err = esp_video_deinit();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_video_deinit failed (%s)", esp_err_to_name(err));
    }

    stop_xclk();
    s_camera_ready = false;
    return err;
}

extern "C" bool kc_touch_camera_ready(void)
{
    return s_camera_ready;
}
