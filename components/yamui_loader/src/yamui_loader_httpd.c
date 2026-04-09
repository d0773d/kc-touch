#include "yamui_loader.h"

#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "sdkconfig.h"

#ifdef CONFIG_YAMUI_LOADER_HTTPD_ENABLE

static const char *TAG = "yamui_httpd";

#ifndef CONFIG_YAMUI_LOADER_MAX_YAML_SIZE
#define CONFIG_YAMUI_LOADER_MAX_YAML_SIZE 65536
#endif

#ifndef CONFIG_YAMUI_LOADER_HTTPD_PORT
#define CONFIG_YAMUI_LOADER_HTTPD_PORT 80
#endif

static httpd_handle_t s_httpd = NULL;

/* ---------- POST /api/yaml  – upload new YAML schema ---------- */

static esp_err_t yaml_post_handler(httpd_req_t *req)
{
    size_t content_len = req->content_len;
    if (content_len == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    if (content_len > CONFIG_YAMUI_LOADER_MAX_YAML_SIZE) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Payload too large");
        return ESP_FAIL;
    }

    char *buf = (char *)malloc(content_len + 1);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }

    size_t received = 0;
    while (received < content_len) {
        int ret = httpd_req_recv(req, buf + received,
                                 content_len - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            free(buf);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                "Receive error");
            return ESP_FAIL;
        }
        received += (size_t)ret;
    }
    buf[content_len] = '\0';

    ESP_LOGI(TAG, "Received %u bytes YAML via HTTP POST", (unsigned)content_len);

    esp_err_t err = yamui_loader_apply_yaml(buf, content_len,
                                            YAMUI_SOURCE_HTTPS, true);
    free(buf);

    if (err == ESP_OK) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"YAML applied\"}");
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "Failed to parse/apply YAML");
    }
    return err;
}

static const httpd_uri_t s_yaml_post_uri = {
    .uri = "/api/yaml",
    .method = HTTP_POST,
    .handler = yaml_post_handler,
    .user_ctx = NULL,
};

/* ---------- GET /api/yaml – return current active YAML ---------- */

static esp_err_t yaml_get_handler(httpd_req_t *req)
{
    /* Try to load from filesystem */
    extern esp_err_t yamui_fs_load(const char *path, char **out_data,
                                   size_t *out_length);
    extern bool yamui_fs_exists(const char *path);

    /* Build the path */
    char path[64];
    snprintf(path, sizeof(path), "%s/%s",
             CONFIG_YAMUI_LOADER_STORAGE_MOUNT,
             CONFIG_YAMUI_LOADER_YAML_FILENAME);

    char *data = NULL;
    size_t length = 0;

    if (yamui_fs_exists(path)) {
        esp_err_t err = yamui_fs_load(path, &data, &length);
        if (err == ESP_OK && data && length > 0) {
            httpd_resp_set_type(req, "text/yaml");
            httpd_resp_send(req, data, (ssize_t)length);
            free(data);
            return ESP_OK;
        }
        free(data);
    }

    /* No filesystem YAML available, report embedded */
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req,
        "{\"status\":\"embedded\","
        "\"message\":\"No custom YAML on filesystem; using embedded schema\"}");
    return ESP_OK;
}

static const httpd_uri_t s_yaml_get_uri = {
    .uri = "/api/yaml",
    .method = HTTP_GET,
    .handler = yaml_get_handler,
    .user_ctx = NULL,
};

/* ---------- GET /api/status – device info ---------- */

static esp_err_t status_get_handler(httpd_req_t *req)
{
    yamui_source_t source = YAMUI_SOURCE_EMBEDDED;
    yamui_loader_get_active_source(&source);

    const char *source_str;
    switch (source) {
    case YAMUI_SOURCE_FILESYSTEM: source_str = "filesystem"; break;
    case YAMUI_SOURCE_UART:       source_str = "uart";       break;
    case YAMUI_SOURCE_HTTPS:      source_str = "https";      break;
    default:                      source_str = "embedded";   break;
    }

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    char json[256];
    snprintf(json, sizeof(json),
        "{\"chip\":\"ESP32 (%d cores)\","
        "\"free_heap\":%lu,"
        "\"active_source\":\"%s\","
        "\"max_yaml_size\":%d}",
        chip_info.cores,
        (unsigned long)esp_get_free_heap_size(),
        source_str,
        CONFIG_YAMUI_LOADER_MAX_YAML_SIZE);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    return ESP_OK;
}

static const httpd_uri_t s_status_uri = {
    .uri = "/api/status",
    .method = HTTP_GET,
    .handler = status_get_handler,
    .user_ctx = NULL,
};

/* ---------- DELETE /api/yaml – remove custom YAML, revert to embedded --- */

static esp_err_t yaml_delete_handler(httpd_req_t *req)
{
    extern esp_err_t yamui_fs_delete(const char *path);

    char path[64];
    snprintf(path, sizeof(path), "%s/%s",
             CONFIG_YAMUI_LOADER_STORAGE_MOUNT,
             CONFIG_YAMUI_LOADER_YAML_FILENAME);

    yamui_fs_delete(path);

    /* Trigger reload — will fall back to embedded */
    yamui_loader_load_best();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req,
        "{\"status\":\"ok\",\"message\":\"Custom YAML removed, reverted to embedded\"}");
    return ESP_OK;
}

static const httpd_uri_t s_yaml_delete_uri = {
    .uri = "/api/yaml",
    .method = HTTP_DELETE,
    .handler = yaml_delete_handler,
    .user_ctx = NULL,
};

/* ---------- Server lifecycle ---------- */

esp_err_t yamui_loader_start_httpd(void)
{
    if (s_httpd) {
        ESP_LOGW(TAG, "HTTP server already running");
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = CONFIG_YAMUI_LOADER_HTTPD_PORT;
    config.stack_size = 8192;
    config.max_uri_handlers = 8;

    esp_err_t err = httpd_start(&s_httpd, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    httpd_register_uri_handler(s_httpd, &s_yaml_post_uri);
    httpd_register_uri_handler(s_httpd, &s_yaml_get_uri);
    httpd_register_uri_handler(s_httpd, &s_status_uri);
    httpd_register_uri_handler(s_httpd, &s_yaml_delete_uri);

    ESP_LOGI(TAG, "HTTP server started on port %d", CONFIG_YAMUI_LOADER_HTTPD_PORT);
    return ESP_OK;
}

#endif /* CONFIG_YAMUI_LOADER_HTTPD_ENABLE */
