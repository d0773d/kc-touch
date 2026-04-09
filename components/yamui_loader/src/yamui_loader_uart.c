#include "yamui_loader.h"

#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_crc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#ifdef CONFIG_YAMUI_LOADER_UART_ENABLE

static const char *TAG = "yamui_uart";

#define YAMUI_UART_START_MARKER     "YAML"
#define YAMUI_UART_START_MARKER_LEN 4
#define YAMUI_UART_HEADER_LEN       8  /* 4 marker + 4 length */

#ifndef CONFIG_YAMUI_LOADER_UART_TASK_STACK
#define CONFIG_YAMUI_LOADER_UART_TASK_STACK 4096
#endif

#ifndef CONFIG_YAMUI_LOADER_UART_RX_TIMEOUT_S
#define CONFIG_YAMUI_LOADER_UART_RX_TIMEOUT_S 30
#endif

#ifndef CONFIG_YAMUI_LOADER_MAX_YAML_SIZE
#define CONFIG_YAMUI_LOADER_MAX_YAML_SIZE 65536
#endif

#ifndef CONFIG_YAMUI_LOADER_UART_PORT
#define CONFIG_YAMUI_LOADER_UART_PORT 1
#endif

#ifndef CONFIG_YAMUI_LOADER_UART_BAUD
#define CONFIG_YAMUI_LOADER_UART_BAUD 115200
#endif

#ifndef CONFIG_YAMUI_LOADER_UART_TX_PIN
#define CONFIG_YAMUI_LOADER_UART_TX_PIN 43
#endif

#ifndef CONFIG_YAMUI_LOADER_UART_RX_PIN
#define CONFIG_YAMUI_LOADER_UART_RX_PIN 44
#endif

#define YAMUI_UART_RX_BUF_SIZE 1024

/** Response codes sent back to the uploader. */
#define YAMUI_UART_ACK  "YACK"
#define YAMUI_UART_NAK  "YNAK"

static TaskHandle_t s_uart_task = NULL;

/**
 * Read exactly `len` bytes from UART with a total timeout.
 * Returns the number of bytes actually read.
 */
static size_t uart_read_exact(int port, uint8_t *buf, size_t len,
                              uint32_t timeout_ms)
{
    size_t received = 0;
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);

    while (received < len) {
        TickType_t now = xTaskGetTickCount();
        if (now >= deadline) {
            break;
        }
        TickType_t remaining = deadline - now;
        int chunk = uart_read_bytes(port, buf + received, len - received,
                                    remaining);
        if (chunk > 0) {
            received += (size_t)chunk;
        } else if (chunk < 0) {
            break;
        }
    }
    return received;
}

static void uart_send_response(int port, const char *code)
{
    uart_write_bytes(port, code, 4);
}

/**
 * Wait for the 4-byte start marker "YAML" by scanning one byte at a time.
 * Returns true when the marker has been detected.
 */
static bool uart_wait_for_marker(int port)
{
    uint8_t ring[YAMUI_UART_START_MARKER_LEN] = {0};
    size_t pos = 0;

    while (true) {
        uint8_t byte;
        int n = uart_read_bytes(port, &byte, 1, pdMS_TO_TICKS(1000));
        if (n <= 0) {
            continue;
        }
        ring[pos % YAMUI_UART_START_MARKER_LEN] = byte;
        pos++;
        if (pos >= YAMUI_UART_START_MARKER_LEN) {
            bool match = true;
            for (size_t i = 0; i < YAMUI_UART_START_MARKER_LEN; ++i) {
                size_t idx = (pos - YAMUI_UART_START_MARKER_LEN + i) %
                             YAMUI_UART_START_MARKER_LEN;
                if (ring[idx] != (uint8_t)YAMUI_UART_START_MARKER[i]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                return true;
            }
        }
    }
}

static void yamui_uart_task(void *arg)
{
    int port = (int)(intptr_t)arg;

    ESP_LOGI(TAG, "UART listener started on port %d", port);

    while (true) {
        /* Wait for start marker */
        if (!uart_wait_for_marker(port)) {
            continue;
        }
        ESP_LOGI(TAG, "Start marker received");

        uint32_t timeout_ms = CONFIG_YAMUI_LOADER_UART_RX_TIMEOUT_S * 1000U;

        /* Read 4-byte payload length (little-endian) */
        uint8_t len_buf[4];
        if (uart_read_exact(port, len_buf, 4, timeout_ms) != 4) {
            ESP_LOGE(TAG, "Timeout reading payload length");
            uart_send_response(port, YAMUI_UART_NAK);
            continue;
        }

        uint32_t payload_len = (uint32_t)len_buf[0] |
                               ((uint32_t)len_buf[1] << 8) |
                               ((uint32_t)len_buf[2] << 16) |
                               ((uint32_t)len_buf[3] << 24);

        if (payload_len == 0 || payload_len > CONFIG_YAMUI_LOADER_MAX_YAML_SIZE) {
            ESP_LOGE(TAG, "Invalid payload length: %lu", (unsigned long)payload_len);
            uart_send_response(port, YAMUI_UART_NAK);
            continue;
        }

        /* Allocate buffer for payload */
        char *payload = (char *)malloc(payload_len);
        if (!payload) {
            ESP_LOGE(TAG, "Out of memory for %lu byte payload",
                     (unsigned long)payload_len);
            uart_send_response(port, YAMUI_UART_NAK);
            continue;
        }

        /* Read payload */
        size_t got = uart_read_exact(port, (uint8_t *)payload, payload_len,
                                     timeout_ms);
        if (got != payload_len) {
            ESP_LOGE(TAG, "Incomplete payload: %u/%lu bytes",
                     (unsigned)got, (unsigned long)payload_len);
            free(payload);
            uart_send_response(port, YAMUI_UART_NAK);
            continue;
        }

        /* Read 4-byte CRC32 */
        uint8_t crc_buf[4];
        if (uart_read_exact(port, crc_buf, 4, timeout_ms) != 4) {
            ESP_LOGE(TAG, "Timeout reading CRC32");
            free(payload);
            uart_send_response(port, YAMUI_UART_NAK);
            continue;
        }

        uint32_t received_crc = (uint32_t)crc_buf[0] |
                                ((uint32_t)crc_buf[1] << 8) |
                                ((uint32_t)crc_buf[2] << 16) |
                                ((uint32_t)crc_buf[3] << 24);

        uint32_t computed_crc = esp_crc32_le(0, (const uint8_t *)payload,
                                             payload_len);

        if (received_crc != computed_crc) {
            ESP_LOGE(TAG, "CRC32 mismatch: received 0x%08lx, computed 0x%08lx",
                     (unsigned long)received_crc, (unsigned long)computed_crc);
            free(payload);
            uart_send_response(port, YAMUI_UART_NAK);
            continue;
        }

        ESP_LOGI(TAG, "Received %lu bytes YAML via UART, CRC OK",
                 (unsigned long)payload_len);

        /* Apply and persist the YAML */
        esp_err_t err = yamui_loader_apply_yaml(payload, payload_len,
                                                YAMUI_SOURCE_UART, true);
        free(payload);

        if (err == ESP_OK) {
            uart_send_response(port, YAMUI_UART_ACK);
            ESP_LOGI(TAG, "YAML applied successfully");
        } else {
            uart_send_response(port, YAMUI_UART_NAK);
            ESP_LOGE(TAG, "Failed to apply YAML: %s", esp_err_to_name(err));
        }
    }
}

esp_err_t yamui_loader_start_uart_listener(void)
{
    if (s_uart_task) {
        ESP_LOGW(TAG, "UART listener already running");
        return ESP_OK;
    }

    int port = CONFIG_YAMUI_LOADER_UART_PORT;

    uart_config_t uart_config = {
        .baud_rate = CONFIG_YAMUI_LOADER_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(port, YAMUI_UART_RX_BUF_SIZE * 2,
                                        YAMUI_UART_RX_BUF_SIZE, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_param_config(port, &uart_config);
    if (err != ESP_OK) {
        uart_driver_delete(port);
        return err;
    }

    err = uart_set_pin(port,
                       CONFIG_YAMUI_LOADER_UART_TX_PIN,
                       CONFIG_YAMUI_LOADER_UART_RX_PIN,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        uart_driver_delete(port);
        return err;
    }

    BaseType_t created = xTaskCreate(yamui_uart_task, "yamui_uart",
                                     CONFIG_YAMUI_LOADER_UART_TASK_STACK,
                                     (void *)(intptr_t)port,
                                     tskIDLE_PRIORITY + 2,
                                     &s_uart_task);
    if (created != pdPASS) {
        uart_driver_delete(port);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "UART listener started (port %d, baud %d, TX=%d, RX=%d)",
             port, CONFIG_YAMUI_LOADER_UART_BAUD,
             CONFIG_YAMUI_LOADER_UART_TX_PIN, CONFIG_YAMUI_LOADER_UART_RX_PIN);
    return ESP_OK;
}

#endif /* CONFIG_YAMUI_LOADER_UART_ENABLE */
