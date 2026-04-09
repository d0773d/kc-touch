#include "yamui_loader.h"
#include "yamui_loader_fs.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "kc_touch_gui.h"
#include "lvgl_yaml_gui.h"
#include "sdkconfig.h"

static const char *TAG = "yamui_loader";

#ifndef CONFIG_YAMUI_LOADER_STORAGE_MOUNT
#define CONFIG_YAMUI_LOADER_STORAGE_MOUNT "/storage"
#endif

#ifndef CONFIG_YAMUI_LOADER_STORAGE_PARTITION
#define CONFIG_YAMUI_LOADER_STORAGE_PARTITION "storage"
#endif

#ifndef CONFIG_YAMUI_LOADER_YAML_FILENAME
#define CONFIG_YAMUI_LOADER_YAML_FILENAME "ui.yml"
#endif

#ifndef CONFIG_YAMUI_LOADER_MAX_YAML_SIZE
#define CONFIG_YAMUI_LOADER_MAX_YAML_SIZE 65536
#endif

static struct {
    bool initialized;
    yamui_loader_config_t config;
    yamui_source_t active_source;
    char yaml_path[64];
} s_loader = {0};

yamui_loader_config_t yamui_loader_default_config(void)
{
    yamui_loader_config_t cfg = {
#ifdef CONFIG_YAMUI_LOADER_UART_ENABLE
        .enable_uart_listener = true,
        .uart_port = CONFIG_YAMUI_LOADER_UART_PORT,
        .uart_baud = CONFIG_YAMUI_LOADER_UART_BAUD,
#else
        .enable_uart_listener = false,
        .uart_port = 1,
        .uart_baud = 115200,
#endif
#ifdef CONFIG_YAMUI_LOADER_HTTPS_ENABLE
        .enable_https_poll = true,
        .https_poll_interval_s = CONFIG_YAMUI_LOADER_HTTPS_POLL_INTERVAL_S,
#else
        .enable_https_poll = false,
        .https_poll_interval_s = 0,
#endif
#ifdef CONFIG_YAMUI_LOADER_HTTPD_ENABLE
        .enable_httpd = true,
        .httpd_port = CONFIG_YAMUI_LOADER_HTTPD_PORT,
#else
        .enable_httpd = false,
        .httpd_port = 80,
#endif
        .storage_mount = CONFIG_YAMUI_LOADER_STORAGE_MOUNT,
    };

#ifdef CONFIG_YAMUI_LOADER_HTTPS_URL
    strncpy(cfg.https_url, CONFIG_YAMUI_LOADER_HTTPS_URL, sizeof(cfg.https_url) - 1);
#endif

    return cfg;
}

esp_err_t yamui_loader_init(const yamui_loader_config_t *config)
{
    if (s_loader.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    s_loader.config = *config;
    s_loader.active_source = YAMUI_SOURCE_EMBEDDED;

    const char *mount = config->storage_mount;
    if (!mount || mount[0] == '\0') {
        mount = CONFIG_YAMUI_LOADER_STORAGE_MOUNT;
    }

    snprintf(s_loader.yaml_path, sizeof(s_loader.yaml_path),
             "%s/%s", mount, CONFIG_YAMUI_LOADER_YAML_FILENAME);

    esp_err_t ret = yamui_fs_init(CONFIG_YAMUI_LOADER_STORAGE_PARTITION, mount);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "LittleFS init failed (%s), filesystem features disabled",
                 esp_err_to_name(ret));
    }

    s_loader.initialized = true;

    if (config->enable_uart_listener) {
        ret = yamui_loader_start_uart_listener();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "UART listener start failed: %s", esp_err_to_name(ret));
        }
    }

    if (config->enable_httpd) {
        ret = yamui_loader_start_httpd();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "HTTP server start failed: %s", esp_err_to_name(ret));
        }
    }

    if (config->enable_https_poll && config->https_url[0] != '\0') {
        extern esp_err_t yamui_loader_start_https_poll(const char *url);
        ret = yamui_loader_start_https_poll(config->https_url);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "HTTPS poll start failed: %s", esp_err_to_name(ret));
        }
    }

    ESP_LOGI(TAG, "Initialized (uart=%s, https=%s, httpd=%s)",
             config->enable_uart_listener ? "on" : "off",
             config->enable_https_poll ? "on" : "off",
             config->enable_httpd ? "on" : "off");

    return ESP_OK;
}

/** Dispatch context for GUI-thread reload. */
typedef struct {
    char *data;
    size_t length;
    yamui_source_t source;
} yamui_reload_ctx_t;

static void yamui_reload_on_gui(void *arg)
{
    yamui_reload_ctx_t *ctx = (yamui_reload_ctx_t *)arg;
    if (!ctx) {
        return;
    }

    esp_err_t err;
    if (ctx->data && ctx->length > 0) {
        const char *name = (ctx->source == YAMUI_SOURCE_UART) ? "uart" :
                           (ctx->source == YAMUI_SOURCE_HTTPS) ? "https" :
                           "dynamic";
        err = lvgl_yaml_gui_load_from_buffer(ctx->data, ctx->length, name);
        free(ctx->data);
    } else {
        err = lvgl_yaml_gui_load_from_file(s_loader.yaml_path);
    }

    if (err == ESP_OK) {
        s_loader.active_source = ctx->source;
        ESP_LOGI(TAG, "UI reloaded from source %d", (int)ctx->source);
    } else {
        ESP_LOGE(TAG, "UI reload failed (%s), falling back to embedded",
                 esp_err_to_name(err));
        lvgl_yaml_gui_load_default();
        s_loader.active_source = YAMUI_SOURCE_EMBEDDED;
    }
    free(ctx);
}

static void yamui_load_best_on_gui(void *arg)
{
    (void)arg;

    if (yamui_fs_exists(s_loader.yaml_path)) {
        ESP_LOGI(TAG, "Loading YAML from filesystem: %s", s_loader.yaml_path);
        esp_err_t err = lvgl_yaml_gui_load_from_file(s_loader.yaml_path);
        if (err == ESP_OK) {
            s_loader.active_source = YAMUI_SOURCE_FILESYSTEM;
            return;
        }
        ESP_LOGW(TAG, "Filesystem YAML invalid (%s), falling back to embedded",
                 esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "Loading embedded YAML schema");
    esp_err_t err = lvgl_yaml_gui_load_default();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load embedded schema (%s)", esp_err_to_name(err));
    }
    s_loader.active_source = YAMUI_SOURCE_EMBEDDED;
}

esp_err_t yamui_loader_load_best(void)
{
    if (!s_loader.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    return kc_touch_gui_dispatch(yamui_load_best_on_gui, NULL, pdMS_TO_TICKS(500));
}

esp_err_t yamui_loader_apply_yaml(const char *data, size_t length,
                                  yamui_source_t source, bool persist)
{
    if (!data || length == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_loader.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (length > CONFIG_YAMUI_LOADER_MAX_YAML_SIZE) {
        ESP_LOGE(TAG, "YAML payload too large: %u > %u",
                 (unsigned)length, (unsigned)CONFIG_YAMUI_LOADER_MAX_YAML_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }

    if (persist) {
        esp_err_t save_err = yamui_fs_save(s_loader.yaml_path, data, length);
        if (save_err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to persist YAML to %s: %s",
                     s_loader.yaml_path, esp_err_to_name(save_err));
        }
    }

    yamui_reload_ctx_t *ctx = (yamui_reload_ctx_t *)calloc(1, sizeof(*ctx));
    if (!ctx) {
        return ESP_ERR_NO_MEM;
    }
    ctx->data = (char *)malloc(length);
    if (!ctx->data) {
        free(ctx);
        return ESP_ERR_NO_MEM;
    }
    memcpy(ctx->data, data, length);
    ctx->length = length;
    ctx->source = source;

    esp_err_t err = kc_touch_gui_dispatch(yamui_reload_on_gui, ctx,
                                          pdMS_TO_TICKS(500));
    if (err != ESP_OK) {
        free(ctx->data);
        free(ctx);
    }
    return err;
}

esp_err_t yamui_loader_get_active_source(yamui_source_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = s_loader.active_source;
    return ESP_OK;
}

/* --- Stubs for optional submodules when disabled via Kconfig --- */

#ifndef CONFIG_YAMUI_LOADER_UART_ENABLE
esp_err_t yamui_loader_start_uart_listener(void)
{
    ESP_LOGW(TAG, "UART listener not compiled (CONFIG_YAMUI_LOADER_UART_ENABLE=n)");
    return ESP_ERR_NOT_SUPPORTED;
}
#endif

#ifndef CONFIG_YAMUI_LOADER_HTTPS_ENABLE
esp_err_t yamui_loader_fetch_https(const char *url)
{
    (void)url;
    ESP_LOGW(TAG, "HTTPS client not compiled (CONFIG_YAMUI_LOADER_HTTPS_ENABLE=n)");
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t yamui_loader_start_https_poll(const char *url)
{
    (void)url;
    ESP_LOGW(TAG, "HTTPS polling not compiled (CONFIG_YAMUI_LOADER_HTTPS_ENABLE=n)");
    return ESP_ERR_NOT_SUPPORTED;
}
#endif

#ifndef CONFIG_YAMUI_LOADER_HTTPD_ENABLE
esp_err_t yamui_loader_start_httpd(void)
{
    ESP_LOGW(TAG, "HTTP server not compiled (CONFIG_YAMUI_LOADER_HTTPD_ENABLE=n)");
    return ESP_ERR_NOT_SUPPORTED;
}
#endif
