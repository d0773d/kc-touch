#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "yaml_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    YUI_ACTION_SET = 0,
    YUI_ACTION_GOTO,
    YUI_ACTION_PUSH,
    YUI_ACTION_POP,
    YUI_ACTION_MODAL,
    YUI_ACTION_CLOSE_MODAL,
    YUI_ACTION_CALL,
    YUI_ACTION_EMIT,
} yui_action_type_t;

typedef struct {
    yui_action_type_t type;
    char *arg0;
    char *arg1;
    char *arg2;
} yui_action_t;

typedef struct {
    yui_action_t *items;
    size_t count;
} yui_action_list_t;

typedef const char *(*yui_symbol_resolver_t)(const char *symbol, void *user_ctx, char *buffer, size_t buffer_len);

typedef struct {
    yui_symbol_resolver_t resolver;
    void *resolver_ctx;
} yui_action_eval_ctx_t;

typedef struct {
    esp_err_t (*goto_screen)(const char *screen);
    esp_err_t (*push_screen)(const char *screen);
    esp_err_t (*pop_screen)(void);
    esp_err_t (*show_modal)(const char *component);
    esp_err_t (*close_modal)(void);
    esp_err_t (*call_native)(const char *function, const char **args, size_t arg_count);
    esp_err_t (*emit_event)(const char *event, const char **args, size_t arg_count);
} yui_action_runtime_t;

/**
 * @brief Parse a YAML scalar or sequence into an executable action list.
 */
esp_err_t yui_action_list_from_node(const yml_node_t *node, yui_action_list_t *out);

/**
 * @brief Free all dynamically allocated strings associated with the list.
 */
void yui_action_list_free(yui_action_list_t *list);

/**
 * @brief Execute the list using the provided evaluation context.
 */
esp_err_t yui_action_list_execute(const yui_action_list_t *list, const yui_action_eval_ctx_t *ctx);

/**
 * @brief Register runtime callbacks used to satisfy navigation, modal, and native actions.
 */
void yui_events_set_runtime(const yui_action_runtime_t *runtime);

#ifdef __cplusplus
}
#endif
