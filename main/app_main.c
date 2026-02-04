/* Wi-Fi Example

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

#include "kc_touch_gui.h"
#include "kc_touch_display.h"
#include "wifi_copro_hw.h"
#include "wifi_copro_power.h"
#include "wifi_copro_transport.h"
#include "lvgl.h"
#include "extra/libs/qrcode/lv_qrcode.h"

static char prov_qr_payload[256];

static const char *TAG = "app";


/* Signal Wi-Fi events on this event-group */
const int WIFI_CONNECTED_EVENT = BIT0;
static EventGroupHandle_t wifi_event_group;

/* Handler for provisioning events */
static void wifi_prov_event_handler(void* arg, esp_event_base_t event_base,
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
                         "\n\tSSID     : %s"
                         "\n\tPassword : %s",
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
                break;
            }
            case WIFI_PROV_CRED_SUCCESS:
                ESP_LOGI(TAG, "Provisioning successful");
                break;
            case WIFI_PROV_END:
                /* De-initialize manager once provisioning is finished */
                wifi_prov_mgr_deinit();
                break;
            default:
                break;
        }
    }
}

/* Function to start provisioning manually - to be called by button press */
static void show_qr_code_ui(void *ctx)
{
    lv_obj_t * lcd_scr = lv_scr_act();
    lv_obj_t * qr = lv_qrcode_create(lcd_scr, 150, lv_color_black(), lv_color_white());
    lv_qrcode_update(qr, prov_qr_payload, strlen(prov_qr_payload));
    lv_obj_align(qr, LV_ALIGN_CENTER, 0, 80);
}

void start_wifi_provisioning(void)
{
    /* Initialize Wi-Fi Provisioning Manager */
    wifi_prov_mgr_config_t config = {
        /* SoftAP based provisioning */
        .scheme = wifi_prov_scheme_softap,
        /* Any custom scheme options */
        .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE
    };

    ESP_ERROR_CHECK(wifi_prov_mgr_init(config));

    /* Register listener for provisioning events */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &wifi_prov_event_handler, NULL));

    /* Start provisioning */
    const char *service_name = "PROV_DEVICE";
    const char *service_key = "password"; 
    
    /* Start provisioning service */
    esp_err_t err = wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1, (const char *) service_key, service_name, NULL);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Provisioning started with service name: %s", service_name);
        
        snprintf(prov_qr_payload, sizeof(prov_qr_payload), 
            "{\"ver\":\"v1\",\"name\":\"%s\",\"pop\":\"%s\",\"transport\":\"softap\"}", 
            service_name, service_key ? service_key : "");
        ESP_LOGI(TAG, "QR Code Payload: %s", prov_qr_payload);

        // Update UI on the GUI thread
        kc_touch_gui_dispatch(show_qr_code_ui, NULL, portMAX_DELAY);
    } else {
        ESP_LOGE(TAG, "Failed to start provisioning: %s", esp_err_to_name(err));
    }
}

/* Task to run provisioning manager in its own context */
static void provisioning_task(void * arg)
{
    start_wifi_provisioning();
    vTaskDelete(NULL);
}

static void btn_event_handler(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if(code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Provisioning button clicked");
        xTaskCreate(provisioning_task, "prov_task", 8192, NULL, 5, NULL);
        
        lv_obj_t * btn = lv_event_get_target(e);
        lv_obj_t * label = lv_obj_get_child(btn, 0);
        lv_label_set_text(label, "Provisioning...");
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    }
}

static void create_prov_ui(void *ctx)
{
    lv_obj_t * btn = lv_btn_create(lv_scr_act());
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(btn, btn_event_handler, LV_EVENT_ALL, NULL);
    lv_obj_set_size(btn, 200, 60);

    lv_obj_t * label = lv_label_create(btn);
    lv_label_set_text(label, "Start Provisioning");
    lv_obj_center(label);
}

/* Event handler for catching system events */
static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                /* Wi-Fi started */
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                 ESP_LOGI(TAG, "Disconnected.");
                break;
            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Connected with IP Address:" IPSTR, IP2STR(&event->ip_info.ip));
        /* Signal main application to continue execution */
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_EVENT);
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

    /* Register our event handler for Wi-Fi and IP related events */
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    /* Power the external Wi-Fi coprocessor before initializing Wi-Fi */
    /* Retry initialization to handle I2C hardware timing on cold boot */
    esp_err_t power_init_err = ESP_FAIL;
    for (int retry = 0; retry < 3; retry++) {
        if (retry > 0) {
            ESP_LOGW(TAG, "Retrying Wi-Fi copro power init (attempt %d/3)", retry + 1);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        power_init_err = wifi_copro_power_init();
        if (power_init_err == ESP_OK) {
            break;
        }
    }
    ESP_ERROR_CHECK(power_init_err);
    ESP_ERROR_CHECK(wifi_copro_power_set(true));
    ESP_ERROR_CHECK(wifi_copro_reset_slave(WIFI_COPRO_RESET_GPIO));
    ESP_ERROR_CHECK(wifi_copro_transport_connect());

    /* Initialize Wi-Fi including netif with default config */
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    /* Start Wi-Fi driver */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Initialize display */
    kc_touch_gui_config_t gui_cfg = kc_touch_gui_default_config();
    esp_err_t gui_err = kc_touch_gui_init(&gui_cfg);
    if (gui_err != ESP_OK) {
        ESP_LOGE(TAG, "GUI init failed (%d)", gui_err);
    } else {
        esp_err_t display_err = kc_touch_display_init();
        if (display_err != ESP_OK) {
            ESP_LOGE(TAG, "Display init failed (%s)", esp_err_to_name(display_err));
        } else {
            kc_touch_gui_dispatch(create_prov_ui, NULL, portMAX_DELAY);
        }
    }
    
    /* Start main application now */
     while (1) {
         ESP_LOGI(TAG, "Hello World!");
         vTaskDelay(1000 / portTICK_PERIOD_MS);
     }

}
