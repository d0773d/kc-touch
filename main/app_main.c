/* Wi-Fi Provisioning Manager Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include <driver/gpio.h>

#include <wifi_provisioning/manager.h>

#include <wifi_provisioning/scheme_softap.h>
#include "kc_touch_provisioning.h"
#include "kc_touch_gui.h"
#include "kc_touch_display.h"
#include "wifi_copro_hw.h"
#include "wifi_copro_power.h"
#include "wifi_copro_transport.h"

static const char *TAG = "app";


/* Signal Wi-Fi events on this event-group */
const int WIFI_CONNECTED_EVENT = BIT0;
static EventGroupHandle_t wifi_event_group;
static bool s_wifi_prov_mgr_initialized;

#define PROV_TRANSPORT_SOFTAP   "softap"
static const char *KC_TOUCH_POP = "abcd1234";

static esp_err_t kc_touch_prov_manager_start(void);
static void kc_touch_prov_manager_stop(void);
static esp_err_t kc_touch_start_softap_provisioning(bool force_reset);
static void kc_touch_display_force_provision(void *ctx);

/* Event handler for catching system events */
static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if (event_base == WIFI_PROV_EVENT) {
        switch (event_id) {
            case WIFI_PROV_START:
                ESP_LOGI(TAG, "Provisioning started");
                break;
            case WIFI_PROV_CRED_RECV: {
                wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
                ESP_LOGI(TAG, "Received Wi-Fi credentials"
                         "\n\tSSID     : %s\n\tPassword : %s",
                         (const char *) wifi_sta_cfg->ssid,
                         (const char *) wifi_sta_cfg->password);
                break;
            }
            case WIFI_PROV_CRED_FAIL: {
                wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
                ESP_LOGE(TAG, "Provisioning failed!\n\tReason : %s"
                         "\n\tPlease reset to factory and retry provisioning",
                         (*reason == WIFI_PROV_STA_AUTH_ERROR) ?
                         "Wi-Fi station authentication failed" : "Wi-Fi access-point not found");
#ifdef CONFIG_KC_TOUCH_RESET_PROV_MGR_ON_FAILURE
                /* Reset the state machine on provisioning failure.
                 * This is enabled by the CONFIG_KC_TOUCH_RESET_PROV_MGR_ON_FAILURE configuration.
                 * It allows the provisioning manager to retry the provisioning process
                 * based on the number of attempts specified in wifi_conn_attempts. After attempting
                 * the maximum number of retries, the provisioning manager will reset the state machine
                 * and the provisioning process will be terminated.
                 */
                wifi_prov_mgr_reset_sm_state_on_failure();
#endif
                break;
            }
            case WIFI_PROV_CRED_SUCCESS:
                ESP_LOGI(TAG, "Provisioning successful");
                break;
            case WIFI_PROV_END:
                /* De-initialize manager once provisioning is finished */
                kc_touch_prov_manager_stop();
                break;
            default:
                break;
        }
    } else if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI(TAG, "Disconnected. Connecting to the AP again...");
                esp_wifi_connect();
                break;
            case WIFI_EVENT_AP_STACONNECTED:
                ESP_LOGI(TAG, "SoftAP transport: Connected!");
                break;
            case WIFI_EVENT_AP_STADISCONNECTED:
                ESP_LOGI(TAG, "SoftAP transport: Disconnected!");
                break;
            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Connected with IP Address:" IPSTR, IP2STR(&event->ip_info.ip));
        /* Signal main application to continue execution */
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_EVENT);
    } else if (event_base == PROTOCOMM_SECURITY_SESSION_EVENT) {
        switch (event_id) {
            case PROTOCOMM_SECURITY_SESSION_SETUP_OK:
                ESP_LOGI(TAG, "Secured session established!");
                break;
            case PROTOCOMM_SECURITY_SESSION_INVALID_SECURITY_PARAMS:
                ESP_LOGE(TAG, "Received invalid security parameters for establishing secure session!");
                break;
            case PROTOCOMM_SECURITY_SESSION_CREDENTIALS_MISMATCH:
                ESP_LOGE(TAG, "Received incorrect username and/or PoP for establishing secure session!");
                break;
            default:
                break;
        }
    }
}

static void wifi_init_sta(void)
{
    /* Start Wi-Fi in station mode */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static esp_err_t kc_touch_prov_manager_start(void)
{
    if (s_wifi_prov_mgr_initialized) {
        return ESP_OK;
    }

    wifi_prov_mgr_config_t config;
    kc_touch_prov_init_manager_config(&config);
    esp_err_t err = wifi_prov_mgr_init(config);
    if (err == ESP_OK) {
        s_wifi_prov_mgr_initialized = true;
    }
    return err;
}

static void kc_touch_prov_manager_stop(void)
{
    if (!s_wifi_prov_mgr_initialized) {
        return;
    }
    wifi_prov_mgr_deinit();
    s_wifi_prov_mgr_initialized = false;
}

static esp_err_t kc_touch_start_softap_provisioning(bool force_reset)
{
    esp_err_t err = kc_touch_prov_manager_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init provisioning manager (%s)", esp_err_to_name(err));
        return err;
    }

    if (force_reset) {
        wifi_prov_mgr_reset_sm_state_for_reprovision();
    }

    char service_name[KC_TOUCH_PROV_SERVICE_NAME_MAX] = {0};
    kc_touch_prov_generate_service_name(service_name, sizeof(service_name));

    err = kc_touch_prov_start_security1(service_name, KC_TOUCH_POP, PROV_TRANSPORT_SOFTAP);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Provisioning start failed (%s)", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Provisioning SoftAP available (SSID: %s)", service_name);
    return ESP_OK;
}

static void kc_touch_display_force_provision(void *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "Display button requested Wi-Fi provisioning");
    esp_err_t err = kc_touch_start_softap_provisioning(true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Unable to start provisioning (%s)", esp_err_to_name(err));
    }
}

void app_main(void)
{
    /* Initialize NVS partition */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* NVS partition was truncated
         * and needs to be erased */
        ESP_ERROR_CHECK(nvs_flash_erase());

        /* Retry nvs_flash_init */
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    /* Initialize TCP/IP */
    ESP_ERROR_CHECK(esp_netif_init());

    /* Initialize the event loop */
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_event_group = xEventGroupCreate();

    /* Register our event handler for Wi-Fi, IP and Provisioning related events */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(PROTOCOMM_SECURITY_SESSION_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    /* Power the external Wi-Fi coprocessor before initializing Wi-Fi */
    ESP_ERROR_CHECK(wifi_copro_power_init());
    ESP_ERROR_CHECK(wifi_copro_power_set(true));
    ESP_ERROR_CHECK(wifi_copro_reset_slave(WIFI_COPRO_RESET_GPIO));
    ESP_ERROR_CHECK(wifi_copro_transport_connect());

    /* Initialize Wi-Fi including netif with default config */
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Initialize provisioning manager so we can query state */
    ESP_ERROR_CHECK(kc_touch_prov_manager_start());

    esp_err_t gui_err = kc_touch_gui_init(NULL);
    if (gui_err != ESP_OK) {
        ESP_LOGW(TAG, "GUI init skipped (%d)", gui_err);
    } else {
        esp_err_t display_err = kc_touch_display_init();
        if (display_err != ESP_OK) {
            ESP_LOGW(TAG, "Display init skipped (%s)", esp_err_to_name(display_err));
        } else {
            (void)kc_touch_display_set_provisioning_cb(kc_touch_display_force_provision, NULL);
        }
    }

    bool provisioned = false;
#ifdef CONFIG_KC_TOUCH_RESET_PROVISIONED
    wifi_prov_mgr_reset_provisioning();
#else
    /* Let's find out if the device is provisioned */
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));

#endif
    /* If device is not yet provisioned start provisioning service */
    if (!provisioned) {
        ESP_LOGI(TAG, "Starting provisioning");
        ESP_ERROR_CHECK(kc_touch_start_softap_provisioning(false));
    } else {
        ESP_LOGI(TAG, "Already provisioned, starting Wi-Fi STA");

        /* We don't need the manager as device is already provisioned,
         * so let's release it's resources */
        kc_touch_prov_manager_stop();

        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
        /* Start Wi-Fi station */
        wifi_init_sta();
    }

    /* Wait for Wi-Fi connection */
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_EVENT, true, true, portMAX_DELAY);

    /* Start main application now */
#if CONFIG_KC_TOUCH_REPROVISIONING
    while (1) {
        for (int i = 0; i < 10; i++) {
            ESP_LOGI(TAG, "Hello World!");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }

        /* Resetting provisioning state machine to enable re-provisioning */
        ESP_ERROR_CHECK(kc_touch_start_softap_provisioning(true));

        /* Wait for Wi-Fi connection */
        xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_EVENT, true, true, portMAX_DELAY);
    }
#else
     while (1) {
         ESP_LOGI(TAG, "Hello World!");
         vTaskDelay(1000 / portTICK_PERIOD_MS);
     }
#endif

}
