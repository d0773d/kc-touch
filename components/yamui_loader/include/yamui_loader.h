#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Source from which the active YAML schema was loaded. */
typedef enum {
    YAMUI_SOURCE_EMBEDDED = 0,
    YAMUI_SOURCE_FILESYSTEM,
    YAMUI_SOURCE_UART,
    YAMUI_SOURCE_HTTPS,
} yamui_source_t;

/** Configuration for the YAML loader subsystem. */
typedef struct {
    /** Enable the UART listener task that accepts framed YAML uploads. */
    bool enable_uart_listener;
    /** UART port number (e.g. UART_NUM_1). Ignored when enable_uart_listener is false. */
    int uart_port;
    /** UART baud rate. Ignored when enable_uart_listener is false. */
    int uart_baud;

    /** Enable periodic HTTPS polling for YAML updates. */
    bool enable_https_poll;
    /** URL to GET the YAML schema from. */
    char https_url[256];
    /** Polling interval in seconds. 0 = one-shot fetch on init only. */
    uint32_t https_poll_interval_s;

    /** Enable the built-in HTTP server for receiving YAML via WiFi POST. */
    bool enable_httpd;
    /** HTTP server listen port. Default 80. */
    uint16_t httpd_port;

    /** LittleFS mount point for persistent YAML storage. */
    const char *storage_mount;
} yamui_loader_config_t;

/**
 * @brief Return a config struct populated with sensible defaults.
 */
yamui_loader_config_t yamui_loader_default_config(void);

/**
 * @brief Initialize the YAML loader subsystem.
 *
 * Mounts the LittleFS storage partition, and optionally starts the UART
 * listener, HTTPS poller, and/or HTTP server based on the config.
 */
esp_err_t yamui_loader_init(const yamui_loader_config_t *config);

/**
 * @brief Load the best available YAML schema.
 *
 * Priority: filesystem (/storage/ui.yml) > embedded default.
 * After loading, the LVGL UI is rebuilt via the GUI dispatch queue.
 */
esp_err_t yamui_loader_load_best(void);

/**
 * @brief Fetch a YAML schema from the given HTTPS URL.
 *
 * Downloads the YAML, saves it to /storage/ui.yml, and triggers a UI reload.
 */
esp_err_t yamui_loader_fetch_https(const char *url);

/**
 * @brief Start the UART listener task.
 *
 * Called automatically by yamui_loader_init() when enable_uart_listener is set.
 * Can also be called manually later.
 */
esp_err_t yamui_loader_start_uart_listener(void);

/**
 * @brief Start the HTTP server for receiving YAML via WiFi.
 *
 * Called automatically by yamui_loader_init() when enable_httpd is set.
 */
esp_err_t yamui_loader_start_httpd(void);

/**
 * @brief Query which source the currently active schema was loaded from.
 */
esp_err_t yamui_loader_get_active_source(yamui_source_t *out);

/**
 * @brief Reload the UI from the given buffer and optionally persist to storage.
 *
 * This is the common path used by UART, HTTPS, and HTTP server handlers
 * after receiving new YAML data.
 */
esp_err_t yamui_loader_apply_yaml(const char *data, size_t length,
                                  yamui_source_t source, bool persist);

#ifdef __cplusplus
}
#endif
