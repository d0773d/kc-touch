#include "yamui_runtime.h"

#include <stdlib.h>
#include <string.h>

#include "yamui_logging.h"

typedef struct {
    char *name;
    yamui_native_fn_t fn;
} yui_native_entry_t;

typedef struct {
    char *event;
    yamui_event_listener_t listener;
    void *user_ctx;
} yui_event_listener_entry_t;

static yui_native_entry_t *s_native_functions;
static size_t s_native_count;
static yui_event_listener_entry_t *s_event_listeners;
static size_t s_event_listener_count;

static char *yui_strdup(const char *src)
{
    if (!src) {
        return NULL;
    }
    size_t len = strlen(src) + 1U;
    char *copy = (char *)malloc(len);
    if (copy) {
        memcpy(copy, src, len);
    }
    return copy;
}

static void yui_remove_native_at(size_t index)
{
    if (!s_native_functions || index >= s_native_count) {
        return;
    }
    free(s_native_functions[index].name);
    for (size_t i = index + 1; i < s_native_count; ++i) {
        s_native_functions[i - 1] = s_native_functions[i];
    }
    s_native_count--;
}

static void yui_remove_event_listener_at(size_t index)
{
    if (!s_event_listeners || index >= s_event_listener_count) {
        return;
    }
    free(s_event_listeners[index].event);
    for (size_t i = index + 1; i < s_event_listener_count; ++i) {
        s_event_listeners[i - 1] = s_event_listeners[i];
    }
    s_event_listener_count--;
}

esp_err_t yamui_runtime_init(void)
{
    yamui_log(YAMUI_LOG_LEVEL_INFO, YAMUI_LOG_CAT_RUNTIME, "Runtime initialized");
    return ESP_OK;
}

esp_err_t yamui_runtime_register_function(const char *name, yamui_native_fn_t fn)
{
    if (!name || !fn) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < s_native_count; ++i) {
        if (strcmp(s_native_functions[i].name, name) == 0) {
            s_native_functions[i].fn = fn;
            yamui_log(YAMUI_LOG_LEVEL_DEBUG, YAMUI_LOG_CAT_NATIVE, "Updated native function '%s'", name);
            return ESP_OK;
        }
    }
    yui_native_entry_t *resized = (yui_native_entry_t *)realloc(s_native_functions, (s_native_count + 1U) * sizeof(yui_native_entry_t));
    if (!resized) {
        yamui_log(YAMUI_LOG_LEVEL_ERROR, YAMUI_LOG_CAT_NATIVE, "Failed to allocate slot for '%s'", name);
        return ESP_ERR_NO_MEM;
    }
    s_native_functions = resized;
    s_native_functions[s_native_count].name = yui_strdup(name);
    if (!s_native_functions[s_native_count].name) {
        yamui_log(YAMUI_LOG_LEVEL_ERROR, YAMUI_LOG_CAT_NATIVE, "Failed to store native name '%s'", name);
        return ESP_ERR_NO_MEM;
    }
    s_native_functions[s_native_count].fn = fn;
    s_native_count++;
    yamui_log(YAMUI_LOG_LEVEL_INFO, YAMUI_LOG_CAT_NATIVE, "Registered native function '%s'", name);
    return ESP_OK;
}

esp_err_t yamui_runtime_unregister_function(const char *name)
{
    if (!name) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < s_native_count; ++i) {
        if (strcmp(s_native_functions[i].name, name) == 0) {
            yui_remove_native_at(i);
            yamui_log(YAMUI_LOG_LEVEL_INFO, YAMUI_LOG_CAT_NATIVE, "Unregistered native function '%s'", name);
            return ESP_OK;
        }
    }
    yamui_log(YAMUI_LOG_LEVEL_WARN, YAMUI_LOG_CAT_NATIVE, "Native function '%s' not registered", name);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t yamui_runtime_call_function(const char *name, const char **args, size_t arg_count)
{
    if (!name) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < s_native_count; ++i) {
        if (strcmp(s_native_functions[i].name, name) == 0) {
            yamui_native_fn_t fn = s_native_functions[i].fn;
            if (fn) {
                yamui_log(YAMUI_LOG_LEVEL_DEBUG, YAMUI_LOG_CAT_NATIVE, "Call native '%s' (%u args)", name, (unsigned)arg_count);
                fn((int)arg_count, args);
                return ESP_OK;
            }
            yamui_log(YAMUI_LOG_LEVEL_ERROR, YAMUI_LOG_CAT_NATIVE, "Native function '%s' missing implementation", name);
            yamui_telemetry_error("native", "missing_impl");
            return ESP_ERR_INVALID_STATE;
        }
    }
    yamui_log(YAMUI_LOG_LEVEL_WARN, YAMUI_LOG_CAT_NATIVE, "Native function '%s' not registered", name);
    yamui_telemetry_error("native", "not_registered");
    return ESP_ERR_NOT_FOUND;
}

esp_err_t yamui_runtime_add_event_listener(const char *event, yamui_event_listener_t listener, void *user_ctx)
{
    if (!event || !listener) {
        return ESP_ERR_INVALID_ARG;
    }
    yui_event_listener_entry_t *resized = (yui_event_listener_entry_t *)realloc(s_event_listeners, (s_event_listener_count + 1U) * sizeof(yui_event_listener_entry_t));
    if (!resized) {
        yamui_log(YAMUI_LOG_LEVEL_ERROR, YAMUI_LOG_CAT_EVENT, "Failed to track listener for '%s'", event);
        return ESP_ERR_NO_MEM;
    }
    s_event_listeners = resized;
    s_event_listeners[s_event_listener_count].event = yui_strdup(event);
    if (!s_event_listeners[s_event_listener_count].event) {
        yamui_log(YAMUI_LOG_LEVEL_ERROR, YAMUI_LOG_CAT_EVENT, "Failed to copy event key '%s'", event);
        return ESP_ERR_NO_MEM;
    }
    s_event_listeners[s_event_listener_count].listener = listener;
    s_event_listeners[s_event_listener_count].user_ctx = user_ctx;
    s_event_listener_count++;
    yamui_log(YAMUI_LOG_LEVEL_DEBUG, YAMUI_LOG_CAT_EVENT, "Registered event listener for '%s'", event);
    return ESP_OK;
}

void yamui_runtime_remove_event_listener(yamui_event_listener_t listener, void *user_ctx)
{
    if (!listener || !s_event_listeners) {
        return;
    }
    for (size_t i = 0; i < s_event_listener_count;) {
        if (s_event_listeners[i].listener == listener && s_event_listeners[i].user_ctx == user_ctx) {
            yamui_log(YAMUI_LOG_LEVEL_DEBUG, YAMUI_LOG_CAT_EVENT, "Removed event listener for '%s'", s_event_listeners[i].event ? s_event_listeners[i].event : "<any>");
            yui_remove_event_listener_at(i);
        } else {
            ++i;
        }
    }
}

esp_err_t yamui_runtime_emit_event(const char *event, const char **args, size_t arg_count)
{
    if (!event) {
        return ESP_ERR_INVALID_ARG;
    }
    yamui_log(YAMUI_LOG_LEVEL_DEBUG, YAMUI_LOG_CAT_EVENT, "Emit event '%s' (%u args)", event, (unsigned)arg_count);
    size_t delivered = 0;
    for (size_t i = 0; i < s_event_listener_count; ++i) {
        if (strcmp(s_event_listeners[i].event, event) == 0 && s_event_listeners[i].listener) {
            s_event_listeners[i].listener(event, args, arg_count, s_event_listeners[i].user_ctx);
            delivered++;
        }
    }
    if (delivered == 0U) {
        yamui_log(YAMUI_LOG_LEVEL_TRACE, YAMUI_LOG_CAT_EVENT, "Event '%s' had no listeners", event);
    }
    return ESP_OK;
}
