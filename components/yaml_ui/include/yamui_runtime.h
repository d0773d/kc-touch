#pragma once

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*yamui_native_fn_t)(int argc, const char **argv);

typedef void (*yamui_event_listener_t)(const char *event, const char **args, size_t arg_count, void *user_ctx);

esp_err_t yamui_runtime_init(void);
esp_err_t yamui_runtime_register_function(const char *name, yamui_native_fn_t fn);
esp_err_t yamui_runtime_unregister_function(const char *name);
esp_err_t yamui_runtime_call_function(const char *name, const char **args, size_t arg_count);

esp_err_t yamui_runtime_add_event_listener(const char *event, yamui_event_listener_t listener, void *user_ctx);
void yamui_runtime_remove_event_listener(yamui_event_listener_t listener, void *user_ctx);
esp_err_t yamui_runtime_emit_event(const char *event, const char **args, size_t arg_count);

#ifdef __cplusplus
}
#endif
