#include "yamui_logging.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#define YAMUI_LOG_STACK_BUFFER 192

static const char *YAMUI_LOG_TAG = "yamui";
static const char *s_level_labels[] = {"ERROR", "WARN", "INFO", "DEBUG", "TRACE"};
static yamui_log_level_t s_min_level = YAMUI_LOG_LEVEL_INFO;
static yamui_log_sink_t s_log_sink = NULL;
static void *s_log_sink_ctx = NULL;
static yamui_telemetry_fn s_telemetry_cb = NULL;
static void *s_telemetry_ctx = NULL;

static void yamui_default_log_sink(yamui_log_level_t level, const char *category, const char *message, void *user_ctx)
{
    (void)user_ctx;
    const char *cat = category ? category : "-";
    const char *label = "?";
    if (level >= YAMUI_LOG_LEVEL_ERROR && level <= YAMUI_LOG_LEVEL_TRACE) {
        label = s_level_labels[level];
    }
    switch (level) {
        case YAMUI_LOG_LEVEL_ERROR:
            ESP_LOGE(YAMUI_LOG_TAG, "[%s] [%s] %s", label, cat, message);
            break;
        case YAMUI_LOG_LEVEL_WARN:
            ESP_LOGW(YAMUI_LOG_TAG, "[%s] [%s] %s", label, cat, message);
            break;
        case YAMUI_LOG_LEVEL_INFO:
            ESP_LOGI(YAMUI_LOG_TAG, "[%s] [%s] %s", label, cat, message);
            break;
        case YAMUI_LOG_LEVEL_DEBUG:
            ESP_LOGD(YAMUI_LOG_TAG, "[%s] [%s] %s", label, cat, message);
            break;
        case YAMUI_LOG_LEVEL_TRACE:
        default:
            ESP_LOGV(YAMUI_LOG_TAG, "[%s] [%s] %s", label, cat, message);
            break;
    }
}

static void yamui_dispatch_log(yamui_log_level_t level, const char *category, const char *message)
{
    if (!message) {
        return;
    }
    if (!s_log_sink) {
        s_log_sink = yamui_default_log_sink;
    }
    s_log_sink(level, category, message, s_log_sink_ctx);
}

void yamui_set_log_level(yamui_log_level_t level)
{
    s_min_level = level;
}

yamui_log_level_t yamui_get_log_level(void)
{
    return s_min_level;
}

void yamui_set_log_sink(yamui_log_sink_t sink, void *user_ctx)
{
    s_log_sink = sink ? sink : yamui_default_log_sink;
    s_log_sink_ctx = user_ctx;
}

void yamui_log(yamui_log_level_t level, const char *category, const char *fmt, ...)
{
    if (!fmt || level > s_min_level) {
        return;
    }

    char stack_buffer[YAMUI_LOG_STACK_BUFFER];
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(stack_buffer, sizeof(stack_buffer), fmt, args);
    va_end(args);

    if (written < 0) {
        return;
    }

    char *buffer = stack_buffer;
    if ((size_t)written >= sizeof(stack_buffer)) {
        buffer = (char *)malloc((size_t)written + 1U);
        if (!buffer) {
            return;
        }
        va_start(args, fmt);
        vsnprintf(buffer, (size_t)written + 1U, fmt, args);
        va_end(args);
    }

    yamui_dispatch_log(level, category, buffer);

    if (buffer != stack_buffer) {
        free(buffer);
    }
}

void yamui_set_telemetry_callback(yamui_telemetry_fn fn, void *user_ctx)
{
    s_telemetry_cb = fn;
    s_telemetry_ctx = user_ctx;
}

void yamui_emit_telemetry(const yamui_telemetry_event_t *event)
{
    if (s_telemetry_cb && event) {
        s_telemetry_cb(event, s_telemetry_ctx);
    }
}

static void yamui_emit_simple_telemetry(yamui_telemetry_type_t type, const char *subject, const char *detail, const char *arg0, const char *arg1, double value)
{
    yamui_telemetry_event_t event = {
        .type = type,
        .subject = subject,
        .detail = detail,
        .arg0 = arg0,
        .arg1 = arg1,
        .value = value,
    };
    yamui_emit_telemetry(&event);
}

void yamui_telemetry_screen_load(const char *screen)
{
    yamui_emit_simple_telemetry(YAMUI_TELEMETRY_SCREEN_LOAD, screen, NULL, NULL, NULL, 0.0);
}

void yamui_telemetry_widget_event(const char *widget, const char *event_name)
{
    yamui_emit_simple_telemetry(YAMUI_TELEMETRY_EVENT, widget, event_name, NULL, NULL, 0.0);
}

void yamui_telemetry_action(const char *action, const char *arg0, const char *arg1)
{
    yamui_emit_simple_telemetry(YAMUI_TELEMETRY_ACTION, action, NULL, arg0, arg1, 0.0);
}

void yamui_telemetry_state_change(const char *key, const char *value)
{
    yamui_emit_simple_telemetry(YAMUI_TELEMETRY_STATE_CHANGE, key, NULL, value, NULL, 0.0);
}

void yamui_telemetry_error(const char *category, const char *message)
{
    yamui_emit_simple_telemetry(YAMUI_TELEMETRY_ERROR, category, NULL, message, NULL, 0.0);
}

void yamui_telemetry_perf(const char *metric, const char *subject, double value)
{
    yamui_emit_simple_telemetry(YAMUI_TELEMETRY_PERF, metric, subject, NULL, NULL, value);
}

void yamui_telemetry_modal(const char *event_name, const char *component)
{
    yamui_emit_simple_telemetry(YAMUI_TELEMETRY_MODAL, component, event_name, NULL, NULL, 0.0);
}
