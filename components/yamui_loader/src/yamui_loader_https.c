#include "yamui_loader.h"

#include <stdlib.h>
#include <string.h>

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#ifdef CONFIG_YAMUI_LOADER_HTTPS_ENABLE

static const char *TAG = "yamui_https";

#ifndef CONFIG_YAMUI_LOADER_MAX_YAML_SIZE
#define CONFIG_YAMUI_LOADER_MAX_YAML_SIZE 65536
#endif

#ifndef CONFIG_YAMUI_LOADER_HTTPS_POLL_INTERVAL_S
#define CONFIG_YAMUI_LOADER_HTTPS_POLL_INTERVAL_S 0
#endif

static TaskHandle_t s_poll_task = NULL;

esp_err_t yamui_loader_fetch_https(const char *url)
{
    if (!url || url[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Fetching YAML from %s", url);

    esp_http_client_config_t http_cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) {
        ESP_LOGE(TAG, "HTTP client init failed");
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);

    if (status_code != 200) {
        ESP_LOGE(TAG, "HTTP status %d", status_code);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    /* Determine buffer size */
    size_t buf_size;
    if (content_length > 0) {
        if ((size_t)content_length > CONFIG_YAMUI_LOADER_MAX_YAML_SIZE) {
            ESP_LOGE(TAG, "Content too large: %d bytes", content_length);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_ERR_INVALID_SIZE;
        }
        buf_size = (size_t)content_length;
    } else {
        /* Chunked or unknown length: read up to max */
        buf_size = CONFIG_YAMUI_LOADER_MAX_YAML_SIZE;
    }

    char *buf = (char *)malloc(buf_size + 1);
    if (!buf) {
        ESP_LOGE(TAG, "OOM allocating %u bytes", (unsigned)buf_size);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    /* Read the body */
    size_t total_read = 0;
    while (total_read < buf_size) {
        int read_len = esp_http_client_read(client, buf + total_read,
                                            (int)(buf_size - total_read));
        if (read_len <= 0) {
            break;
        }
        total_read += (size_t)read_len;
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (total_read == 0) {
        ESP_LOGE(TAG, "Empty response body");
        free(buf);
        return ESP_FAIL;
    }

    buf[total_read] = '\0';
    ESP_LOGI(TAG, "Downloaded %u bytes of YAML", (unsigned)total_read);

    err = yamui_loader_apply_yaml(buf, total_read, YAMUI_SOURCE_HTTPS, true);
    free(buf);
    return err;
}

static void yamui_https_poll_task(void *arg)
{
    const char *url = (const char *)arg;
    uint32_t interval_s = CONFIG_YAMUI_LOADER_HTTPS_POLL_INTERVAL_S;

    ESP_LOGI(TAG, "HTTPS poll task started (interval=%lus, url=%s)",
             (unsigned long)interval_s, url);

    /* Initial fetch */
    yamui_loader_fetch_https(url);

    if (interval_s == 0) {
        ESP_LOGI(TAG, "One-shot fetch complete, poll task exiting");
        s_poll_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(interval_s * 1000));
        yamui_loader_fetch_https(url);
    }
}

/**
 * Called internally when HTTPS polling is enabled. The URL is stored in the
 * loader config struct which outlives this task.
 */
esp_err_t yamui_loader_start_https_poll(const char *url)
{
    if (s_poll_task) {
        ESP_LOGW(TAG, "HTTPS poll task already running");
        return ESP_OK;
    }
    if (!url || url[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    BaseType_t created = xTaskCreate(yamui_https_poll_task, "yamui_https",
                                     6144, (void *)url,
                                     tskIDLE_PRIORITY + 1, &s_poll_task);
    if (created != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

#endif /* CONFIG_YAMUI_LOADER_HTTPS_ENABLE */
