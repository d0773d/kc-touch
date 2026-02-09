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
#include <esp_netif.h>
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

static char prov_qr_payload[256];

static const char *TAG = "app";


/* Signal Wi-Fi events on this event-group */
const int WIFI_CONNECTED_EVENT = BIT0;
static EventGroupHandle_t wifi_event_group;
static bool s_is_provisioning = false;
static int s_retry_num = 0;

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
                kc_touch_display_set_status("Credentials Received\nConnecting to %s...", (const char *) wifi_sta_cfg->ssid);
                kc_touch_display_prov_enable_back(false);
                break;
            }
            case WIFI_PROV_CRED_FAIL: {
                wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
                ESP_LOGE(TAG, "Provisioning failed!\n\tReason : %s"
                         "\n\tPlease reset to factory and retry provisioning",
                         (*reason == WIFI_PROV_STA_AUTH_ERROR) ?
                         "Wi-Fi station authentication failed" : "Wi-Fi access-point not found");
                kc_touch_display_set_status("Provisioning Failed\nSee Logs");
                kc_touch_display_prov_enable_back(true);
                break;
            }
            case WIFI_PROV_CRED_SUCCESS:
                ESP_LOGI(TAG, "Provisioning successful");
                kc_touch_display_set_status("Provisioning Successful\nVerifying...");
                kc_touch_display_prov_enable_back(true);
                break;
            case WIFI_PROV_END:
                /* De-initialize manager once provisioning is finished */
                wifi_prov_mgr_deinit();
                s_is_provisioning = false;

                /* Check if we are connected and have an IP */
                esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
                esp_netif_ip_info_t ip_info;
                if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
                     ESP_LOGI(TAG, "Provisioning ended. Already connected. IP: " IPSTR, IP2STR(&ip_info.ip));
                     kc_touch_display_set_status("Online\nIP: " IPSTR, IP2STR(&ip_info.ip));
                     xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_EVENT);
                } else {
                     /* Not connected, force a reconnect attempt */
                     ESP_LOGI(TAG, "Provisioning ended. Connecting to Wi-Fi...");
                     kc_touch_display_set_status("Connecting...");
                     esp_wifi_connect();
                }
                break;
            default:
                break;
        }
    }
}

/* Function to start provisioning manually - to be called by button press */
// static void show_qr_code_ui(void *ctx)
// {
//     lv_obj_t * lcd_scr = lv_scr_act();
//     lv_obj_clean(lcd_scr);
    
//     // Scale QR code for 720px wide screen
//     lv_obj_t * qr = lv_qrcode_create(lcd_scr, 500, lv_color_black(), lv_color_white());
//     lv_qrcode_update(qr, prov_qr_payload, strlen(prov_qr_payload));
//     lv_obj_center(qr);
//     lv_obj_align(qr, LV_ALIGN_CENTER, 0, -50);

//     lv_obj_t * label = lv_label_create(lcd_scr);
//     lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
//     lv_label_set_text(label, "Scan QR Code with App");
//     lv_obj_align_to(label, qr, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);
// }


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
    
    s_is_provisioning = true;
    s_retry_num = 0;
    /* Force disconnect to ensure clean slate for provisioning */
    esp_wifi_disconnect();

    /* Start provisioning service */
    esp_err_t err = wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1, (const char *) service_key, service_name, NULL);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Provisioning started with service name: %s", service_name);
        
        snprintf(prov_qr_payload, sizeof(prov_qr_payload), 
            "{\"ver\":\"v1\",\"name\":\"%s\",\"pop\":\"%s\",\"transport\":\"softap\"}", 
            service_name, service_key ? service_key : "");
        ESP_LOGI(TAG, "QR Code Payload: %s", prov_qr_payload);

        // Update UI on the GUI thread
        kc_touch_display_show_qr(prov_qr_payload);
        
        kc_touch_display_set_status("Provisioning...\nConnect to Wi-Fi: %s", service_name);
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

static void start_provisioning_callback(void *ctx)
{
    ESP_LOGI(TAG, "Provisioning requested from Display UI");
    xTaskCreate(provisioning_task, "prov_task", 8192, NULL, 5, NULL);
}

static void cancel_provisioning_callback(void *ctx)
{
    ESP_LOGI(TAG, "Provisioning cancelled by user");
    wifi_prov_mgr_stop_provisioning();
    // Returning to root is handled by the display if we don't do it here,
    // but the display won't know we stopped provisioning unless we tell it.
    // Actually, kc_touch_display calls this callback.
    // If we want to return to root, we should do:
    kc_touch_gui_show_root();
}

/* Event handler for catching system events */
static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                if (!s_is_provisioning) {
                   if (!kc_touch_gui_is_scanning()) {
                        esp_wifi_connect();
                        kc_touch_display_set_status("Connecting...");
                   } else {
                        kc_touch_display_set_status("Ready to Scan");
                   }
                }
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                if (s_is_provisioning) {
                    ESP_LOGI(TAG, "Disconnected ignored due to provisioning");
                    break;
                }
                
                if (kc_touch_gui_is_scanning()) {
                    ESP_LOGI(TAG, "Disconnected (Scanning Active) - Auto-reconnect skipped");
                    kc_touch_display_set_status("Scanning Networks...");
                    break;
                }

                if (s_retry_num < 5) {
                    esp_wifi_connect();
                    s_retry_num++;
                    ESP_LOGI(TAG, "retry to connect to the AP");
                    kc_touch_display_set_status("Retrying Connection...\nAttempt %d", s_retry_num);
                } else {
                    ESP_LOGI(TAG, "connect to the AP fail");
                    kc_touch_display_set_status("Connection Failed\nCheck Settings");
                    xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_EVENT);
                }
                break;
            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        if (s_is_provisioning) {
            ESP_LOGI(TAG, "Got IP ignored due to provisioning (likely AP mode)");
            // Provisioning manager handles this usually, or we don't want to show "Online" if we are in softAP mode for provisioning.
            // Actually, in softAP mode, we get AP_STACONNECTED, but GOT_IP is for STA interface. 
            // If we somehow get an IP on STA inside provisioning (e.g. at the end), we might want to respect it.
            // But usually, the sequence is Provisioning -> Creds Recv -> Connect -> GOT_IP.
            // If s_is_provisioning is true, we might be in the verification phase.
        } else {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
            ESP_LOGI(TAG, "Connected with IP Address:" IPSTR, IP2STR(&event->ip_info.ip));
            s_retry_num = 0;
            kc_touch_display_set_status("Online\nIP: " IPSTR, IP2STR(&event->ip_info.ip));
            /* Signal main application to continue execution */
            xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_EVENT);
        }
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

    /* Initialize display early to setup I2C (M5Unified) */
    kc_touch_gui_config_t gui_cfg = kc_touch_gui_default_config();
    esp_err_t gui_err = kc_touch_gui_init(&gui_cfg);
    if (gui_err != ESP_OK) {
        ESP_LOGE(TAG, "GUI init failed (%d)", gui_err);
    } else {
        esp_err_t display_err = kc_touch_display_init();
        if (display_err != ESP_OK) {
            ESP_LOGE(TAG, "Display init failed (%s)", esp_err_to_name(display_err));
        } else {
            // Display is ready, show the main UI
            kc_touch_gui_show_root();
            
            kc_touch_display_set_provisioning_cb(start_provisioning_callback, NULL);
            kc_touch_gui_set_provisioning_cb(start_provisioning_callback, NULL);
            kc_touch_display_set_cancel_cb(cancel_provisioning_callback, NULL);
        }
    }

    /* Register our event handler for Wi-Fi and IP related events */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
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
    
    /* Start main application now */
     while (1) {
         ESP_LOGI(TAG, "Hello World!");
         vTaskDelay(1000 / portTICK_PERIOD_MS);
     }

}
