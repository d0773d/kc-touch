#include "kc_touch_provisioning.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "qrcode.h"
#include "wifi_provisioning/scheme_softap.h"

#define PROV_QR_VERSION       "v1"
#define QRCODE_BASE_URL       "https://espressif.github.io/esp-jumpstart/qrcode.html"
#define CUSTOM_ENDPOINT_NAME  "custom-data"

static const char *TAG = "kc_touch_prov";

static void kc_touch_prov_print_qr(const char *name, const char *pop, const char *transport)
{
    if (!name || !transport) {
        ESP_LOGW(TAG, "Cannot generate QR code payload. Data missing.");
        return;
    }

    char payload[150] = {0};
    if (pop) {
        snprintf(payload, sizeof(payload),
                 "{\"ver\":\"%s\",\"name\":\"%s\",\"pop\":\"%s\",\"transport\":\"%s\"}",
                 PROV_QR_VERSION, name, pop, transport);
    } else {
        snprintf(payload, sizeof(payload),
                 "{\"ver\":\"%s\",\"name\":\"%s\",\"transport\":\"%s\"}",
                 PROV_QR_VERSION, name, transport);
    }

#ifdef CONFIG_KC_TOUCH_PROV_SHOW_QR
    ESP_LOGI(TAG, "Scan this QR code from the provisioning application for Provisioning.");
    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
    esp_qrcode_generate(&cfg, payload);
#endif
    ESP_LOGI(TAG, "If QR code is not visible, copy paste the below URL in a browser.\n%s?data=%s",
             QRCODE_BASE_URL, payload);
}

static esp_err_t custom_prov_data_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                                          uint8_t **outbuf, ssize_t *outlen, void *priv_data)
{
    (void)session_id;
    (void)priv_data;

    if (inbuf) {
        ESP_LOGI(TAG, "Received data: %.*s", inlen, (const char *)inbuf);
    }

    static const char response[] = "SUCCESS";
    *outbuf = (uint8_t *)strdup(response);
    if (*outbuf == NULL) {
        ESP_LOGE(TAG, "System out of memory");
        return ESP_ERR_NO_MEM;
    }
    *outlen = sizeof(response);
    return ESP_OK;
}

#ifdef CONFIG_KC_TOUCH_PROV_ENABLE_APP_CALLBACK
static void wifi_prov_app_callback(void *user_data, wifi_prov_cb_event_t event, void *event_data)
{
    (void)user_data;
    switch (event) {
        case WIFI_PROV_SET_STA_CONFIG:
            /* STA config can be tweaked here before enabling Wi-Fi. */
            (void)event_data;
            break;
        default:
            break;
    }
}

static const wifi_prov_event_handler_t s_wifi_prov_event_handler = {
    .event_cb = wifi_prov_app_callback,
    .user_data = NULL,
};
#endif

void kc_touch_prov_init_manager_config(wifi_prov_mgr_config_t *config)
{
    if (!config) {
        return;
    }

    *config = (wifi_prov_mgr_config_t) {
        .scheme = wifi_prov_scheme_softap,
        .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE,
    };

#if defined(CONFIG_KC_TOUCH_RESET_PROV_MGR_ON_FAILURE) && (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0))
    config->wifi_prov_conn_cfg = (wifi_prov_conn_cfg_t) {
        .wifi_conn_attempts = CONFIG_KC_TOUCH_PROV_MGR_CONNECTION_CNT,
    };
#endif

#ifdef CONFIG_KC_TOUCH_PROV_ENABLE_APP_CALLBACK
    config->app_event_handler = &s_wifi_prov_event_handler;
#endif
}

void kc_touch_prov_generate_service_name(char *service_name, size_t max)
{
    if (!service_name || max == 0) {
        return;
    }

    uint8_t eth_mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
    const char *ssid_prefix = "PROV_";
    snprintf(service_name, max, "%s%02X%02X%02X",
             ssid_prefix, eth_mac[3], eth_mac[4], eth_mac[5]);
}

esp_err_t kc_touch_prov_start_security1(const char *service_name, const char *pop, const char *transport)
{
    if (!service_name || !pop || !transport) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_prov_security_t security = WIFI_PROV_SECURITY_1;
    wifi_prov_security1_params_t *sec_params = (wifi_prov_security1_params_t *)pop;
    const char *service_key = NULL;

    ESP_ERROR_CHECK_WITHOUT_ABORT(wifi_prov_mgr_endpoint_create(CUSTOM_ENDPOINT_NAME));

    esp_err_t err = wifi_prov_mgr_start_provisioning(security, (const void *)sec_params,
                                                     service_name, service_key);
    if (err != ESP_OK) {
        return err;
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(wifi_prov_mgr_endpoint_register(CUSTOM_ENDPOINT_NAME,
                                                                  custom_prov_data_handler, NULL));

#ifdef CONFIG_KC_TOUCH_REPROVISIONING
    wifi_prov_mgr_disable_auto_stop(1000);
#endif

    kc_touch_prov_print_qr(service_name, pop, transport);
    return ESP_OK;
}
