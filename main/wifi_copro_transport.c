#include "wifi_copro_transport.h"

#include "esp_hosted.h"
#include "esp_hosted_transport_config.h"
#include "esp_log.h"
#include "wifi_copro_hw.h"

static const char *TAG = "wifi_copro_transport";

#ifdef CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE
static void wifi_copro_fill_sdio_config(struct esp_hosted_sdio_config *sdio_cfg)
{
    *sdio_cfg = INIT_DEFAULT_HOST_SDIO_CONFIG();

    sdio_cfg->pin_clk.pin   = WIFI_COPRO_SDIO_CLK_GPIO;
    sdio_cfg->pin_cmd.pin   = WIFI_COPRO_SDIO_CMD_GPIO;
    sdio_cfg->pin_d0.pin    = WIFI_COPRO_SDIO_D0_GPIO;
    sdio_cfg->pin_d1.pin    = WIFI_COPRO_SDIO_D1_GPIO;
    sdio_cfg->pin_d2.pin    = WIFI_COPRO_SDIO_D2_GPIO;
    sdio_cfg->pin_d3.pin    = WIFI_COPRO_SDIO_D3_GPIO;
    sdio_cfg->pin_reset.pin = WIFI_COPRO_RESET_GPIO;

    sdio_cfg->clock_freq_khz = WIFI_COPRO_SDIO_CLOCK_KHZ;
    sdio_cfg->bus_width      = WIFI_COPRO_SDIO_BUS_WIDTH;
    sdio_cfg->tx_queue_size  = WIFI_COPRO_SDIO_TX_QUEUE;
    sdio_cfg->rx_queue_size  = WIFI_COPRO_SDIO_RX_QUEUE;
}

static void __attribute__((constructor(110))) wifi_copro_prime_transport(void)
{
    struct esp_hosted_sdio_config sdio_cfg;
    wifi_copro_fill_sdio_config(&sdio_cfg);

    esp_hosted_transport_err_t transport_ret = esp_hosted_sdio_set_config(&sdio_cfg);
    if (transport_ret == ESP_TRANSPORT_OK) {
        ESP_EARLY_LOGI(TAG,
                       "Primed ESP-Hosted SDIO config (CLK=%d CMD=%d D0=%d D1=%d D2=%d D3=%d RESET=%d)",
                       sdio_cfg.pin_clk.pin, sdio_cfg.pin_cmd.pin, sdio_cfg.pin_d0.pin,
                       sdio_cfg.pin_d1.pin, sdio_cfg.pin_d2.pin, sdio_cfg.pin_d3.pin,
                       sdio_cfg.pin_reset.pin);
    } else if (transport_ret != ESP_TRANSPORT_ERR_ALREADY_SET) {
        ESP_EARLY_LOGE(TAG, "Failed to prime ESP-Hosted SDIO config (%d)", transport_ret);
    }
}
#endif

esp_err_t wifi_copro_transport_connect(void)
{
#ifdef CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE
    if (!esp_hosted_is_config_valid()) {
        struct esp_hosted_sdio_config temp_cfg;
        wifi_copro_fill_sdio_config(&temp_cfg);
        esp_hosted_transport_err_t set_ret = esp_hosted_sdio_set_config(&temp_cfg);
        if (set_ret != ESP_TRANSPORT_OK) {
            ESP_LOGE(TAG, "ESP-Hosted SDIO configuration failed (%d)", set_ret);
            return (esp_err_t)set_ret;
        }
    }

    struct esp_hosted_sdio_config *sdio_cfg = NULL;
    esp_hosted_transport_err_t transport_ret = esp_hosted_sdio_get_config(&sdio_cfg);
    if (transport_ret != ESP_TRANSPORT_OK || !sdio_cfg) {
        ESP_LOGE(TAG, "Failed to fetch ESP-Hosted SDIO config (%d)", transport_ret);
        return (esp_err_t)transport_ret;
    }

    int hosted_ret = esp_hosted_connect_to_slave();
    if (hosted_ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_hosted_connect_to_slave failed (%d)", hosted_ret);
        return hosted_ret;
    }

    ESP_LOGI(TAG,
             "ESP-Hosted SDIO pins configured (CLK=%d CMD=%d D0=%d D1=%d D2=%d D3=%d RESET=%d)",
             sdio_cfg->pin_clk.pin, sdio_cfg->pin_cmd.pin, sdio_cfg->pin_d0.pin,
             sdio_cfg->pin_d1.pin, sdio_cfg->pin_d2.pin, sdio_cfg->pin_d3.pin,
             sdio_cfg->pin_reset.pin);
    return ESP_OK;
#else
    ESP_LOGW(TAG, "ESP-Hosted SDIO host interface not enabled; using default transport config");
    return ESP_OK;
#endif
}
