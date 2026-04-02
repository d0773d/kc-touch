#include "yamui_async.h"

#include <stdio.h>
#include <string.h>

#include "yamui_state.h"

#define YUI_ASYNC_KEY_BUFFER_MAX 96

static const char *yui_async_normalize_operation(const char *operation)
{
    if (!operation || operation[0] == '\0') {
        return NULL;
    }
    if (strncmp(operation, "async.", 6) == 0 && operation[6] != '\0') {
        return operation + 6;
    }
    return operation;
}

static esp_err_t yui_async_format_key(char *buffer, size_t buffer_len, const char *operation, const char *field)
{
    if (!buffer || buffer_len == 0U || !field) {
        return ESP_ERR_INVALID_ARG;
    }
    const char *normalized = yui_async_normalize_operation(operation);
    if (!normalized) {
        return ESP_ERR_INVALID_ARG;
    }
    int written = snprintf(buffer, buffer_len, "async.%s.%s", normalized, field);
    if (written < 0 || (size_t)written >= buffer_len) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

static esp_err_t yui_async_set_string_field(const char *operation, const char *field, const char *value)
{
    char key[YUI_ASYNC_KEY_BUFFER_MAX];
    esp_err_t err = yui_async_format_key(key, sizeof(key), operation, field);
    if (err != ESP_OK) {
        return err;
    }
    return yui_state_set(key, value ? value : "");
}

static esp_err_t yui_async_set_bool_field(const char *operation, const char *field, bool value)
{
    char key[YUI_ASYNC_KEY_BUFFER_MAX];
    esp_err_t err = yui_async_format_key(key, sizeof(key), operation, field);
    if (err != ESP_OK) {
        return err;
    }
    return yui_state_set_bool(key, value);
}

static esp_err_t yui_async_set_int_field(const char *operation, const char *field, int32_t value)
{
    char key[YUI_ASYNC_KEY_BUFFER_MAX];
    esp_err_t err = yui_async_format_key(key, sizeof(key), operation, field);
    if (err != ESP_OK) {
        return err;
    }
    return yui_state_set_int(key, value);
}

static int32_t yui_async_clamp_progress(int32_t progress)
{
    if (progress < 0) {
        return 0;
    }
    if (progress > 100) {
        return 100;
    }
    return progress;
}

esp_err_t yamui_async_reset(const char *operation, const char *message)
{
    esp_err_t first_err = yui_async_set_bool_field(operation, "running", false);
    esp_err_t err = yui_async_set_int_field(operation, "progress", 0);
    if (first_err == ESP_OK && err != ESP_OK) {
        first_err = err;
    }
    err = yui_async_set_string_field(operation, "status", "idle");
    if (first_err == ESP_OK && err != ESP_OK) {
        first_err = err;
    }
    err = yui_async_set_string_field(operation, "message", message ? message : "");
    if (first_err == ESP_OK && err != ESP_OK) {
        first_err = err;
    }
    err = yui_async_set_string_field(operation, "error", "");
    if (first_err == ESP_OK && err != ESP_OK) {
        first_err = err;
    }
    return first_err;
}

esp_err_t yamui_async_begin(const char *operation, const char *message)
{
    esp_err_t first_err = yui_async_set_bool_field(operation, "running", true);
    esp_err_t err = yui_async_set_int_field(operation, "progress", 0);
    if (first_err == ESP_OK && err != ESP_OK) {
        first_err = err;
    }
    err = yui_async_set_string_field(operation, "status", "running");
    if (first_err == ESP_OK && err != ESP_OK) {
        first_err = err;
    }
    err = yui_async_set_string_field(operation, "message", message ? message : "");
    if (first_err == ESP_OK && err != ESP_OK) {
        first_err = err;
    }
    err = yui_async_set_string_field(operation, "error", "");
    if (first_err == ESP_OK && err != ESP_OK) {
        first_err = err;
    }
    return first_err;
}

esp_err_t yamui_async_progress(const char *operation, int32_t progress, const char *message)
{
    esp_err_t first_err = yui_async_set_bool_field(operation, "running", true);
    esp_err_t err = yui_async_set_int_field(operation, "progress", yui_async_clamp_progress(progress));
    if (first_err == ESP_OK && err != ESP_OK) {
        first_err = err;
    }
    err = yui_async_set_string_field(operation, "status", "running");
    if (first_err == ESP_OK && err != ESP_OK) {
        first_err = err;
    }
    if (message) {
        err = yui_async_set_string_field(operation, "message", message);
        if (first_err == ESP_OK && err != ESP_OK) {
            first_err = err;
        }
    }
    return first_err;
}

esp_err_t yamui_async_complete(const char *operation, const char *message)
{
    esp_err_t first_err = yui_async_set_bool_field(operation, "running", false);
    esp_err_t err = yui_async_set_int_field(operation, "progress", 100);
    if (first_err == ESP_OK && err != ESP_OK) {
        first_err = err;
    }
    err = yui_async_set_string_field(operation, "status", "success");
    if (first_err == ESP_OK && err != ESP_OK) {
        first_err = err;
    }
    err = yui_async_set_string_field(operation, "message", message ? message : "");
    if (first_err == ESP_OK && err != ESP_OK) {
        first_err = err;
    }
    err = yui_async_set_string_field(operation, "error", "");
    if (first_err == ESP_OK && err != ESP_OK) {
        first_err = err;
    }
    return first_err;
}

esp_err_t yamui_async_fail(const char *operation, const char *message)
{
    esp_err_t first_err = yui_async_set_bool_field(operation, "running", false);
    esp_err_t err = yui_async_set_string_field(operation, "status", "error");
    if (first_err == ESP_OK && err != ESP_OK) {
        first_err = err;
    }
    err = yui_async_set_string_field(operation, "message", message ? message : "");
    if (first_err == ESP_OK && err != ESP_OK) {
        first_err = err;
    }
    err = yui_async_set_string_field(operation, "error", message ? message : "");
    if (first_err == ESP_OK && err != ESP_OK) {
        first_err = err;
    }
    return first_err;
}
