#pragma once

#include <stdarg.h>

typedef enum {
    YAMUI_LOG_LEVEL_ERROR = 0,
    YAMUI_LOG_LEVEL_WARN,
    YAMUI_LOG_LEVEL_INFO,
    YAMUI_LOG_LEVEL_DEBUG,
    YAMUI_LOG_LEVEL_TRACE,
} yamui_log_level_t;

#define YAMUI_LOG_CAT_PARSER "parser"
#define YAMUI_LOG_CAT_STATE "state"
#define YAMUI_LOG_CAT_EXPR "expr"
#define YAMUI_LOG_CAT_EVENT "event"
#define YAMUI_LOG_CAT_ACTION "action"
#define YAMUI_LOG_CAT_LVGL "lvgl"
#define YAMUI_LOG_CAT_MODAL "modal"
#define YAMUI_LOG_CAT_NAV "nav"
#define YAMUI_LOG_CAT_RUNTIME "runtime"
#define YAMUI_LOG_CAT_NATIVE "native"

typedef void (*yamui_log_sink_t)(yamui_log_level_t level, const char *category, const char *message, void *user_ctx);

void yamui_set_log_level(yamui_log_level_t level);
yamui_log_level_t yamui_get_log_level(void);
void yamui_set_log_sink(yamui_log_sink_t sink, void *user_ctx);
void yamui_log(yamui_log_level_t level, const char *category, const char *fmt, ...);

typedef enum {
    YAMUI_TELEMETRY_SCREEN_LOAD = 0,
    YAMUI_TELEMETRY_EVENT,
    YAMUI_TELEMETRY_ACTION,
    YAMUI_TELEMETRY_STATE_CHANGE,
    YAMUI_TELEMETRY_ERROR,
    YAMUI_TELEMETRY_PERF,
    YAMUI_TELEMETRY_MODAL,
} yamui_telemetry_type_t;

typedef struct {
    yamui_telemetry_type_t type;
    const char *subject;
    const char *detail;
    const char *arg0;
    const char *arg1;
    double value;
} yamui_telemetry_event_t;

typedef void (*yamui_telemetry_fn)(const yamui_telemetry_event_t *event, void *user_ctx);

void yamui_set_telemetry_callback(yamui_telemetry_fn fn, void *user_ctx);
void yamui_emit_telemetry(const yamui_telemetry_event_t *event);

void yamui_telemetry_screen_load(const char *screen);
void yamui_telemetry_widget_event(const char *widget, const char *event_name);
void yamui_telemetry_action(const char *action, const char *arg0, const char *arg1);
void yamui_telemetry_state_change(const char *key, const char *value);
void yamui_telemetry_error(const char *category, const char *message);
void yamui_telemetry_perf(const char *metric, const char *subject, double value);
void yamui_telemetry_modal(const char *event_name, const char *component);
