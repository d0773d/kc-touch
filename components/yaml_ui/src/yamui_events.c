#include "yamui_events.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_log.h"
#include "yamui_state.h"

#define YUI_ACTION_MAX_ARGS 3
#define YUI_ACTION_EVAL_BUFFER 128
#define YUI_ACTION_INVALID ((yui_action_type_t)-1)

static const char *TAG = "yamui_events";
static yui_action_runtime_t s_runtime = {0};

void yui_events_set_runtime(const yui_action_runtime_t *runtime)
{
    if (runtime) {
        s_runtime = *runtime;
    } else {
        memset(&s_runtime, 0, sizeof(s_runtime));
    }
}

static char *yui_strdup(const char *src)
{
    if (!src) {
        return NULL;
    }
    size_t len = strlen(src);
    char *copy = (char *)malloc(len + 1U);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, src, len + 1U);
    return copy;
}

static char *yui_trim_inplace(char *str)
{
    if (!str) {
        return NULL;
    }
    while (*str && isspace((unsigned char)*str)) {
        ++str;
    }
    char *end = str + strlen(str);
    while (end > str && isspace((unsigned char)end[-1])) {
        --end;
    }
    *end = '\0';
    return str;
}

static void yui_strip_quotes(char *text)
{
    if (!text) {
        return;
    }
    size_t len = strlen(text);
    if (len < 2U) {
        return;
    }
    char first = text[0];
    char last = text[len - 1U];
    if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
        memmove(text, text + 1, len - 1U);
        text[len - 2U] = '\0';
    }
}

static yui_action_type_t yui_action_type_from_name(const char *name)
{
    if (!name) {
        return YUI_ACTION_INVALID;
    }
    if (strcasecmp(name, "set") == 0) {
        return YUI_ACTION_SET;
    }
    if (strcasecmp(name, "goto") == 0) {
        return YUI_ACTION_GOTO;
    }
    if (strcasecmp(name, "push") == 0) {
        return YUI_ACTION_PUSH;
    }
    if (strcasecmp(name, "pop") == 0) {
        return YUI_ACTION_POP;
    }
    if (strcasecmp(name, "modal") == 0) {
        return YUI_ACTION_MODAL;
    }
    if (strcasecmp(name, "close_modal") == 0) {
        return YUI_ACTION_CLOSE_MODAL;
    }
    if (strcasecmp(name, "call") == 0) {
        return YUI_ACTION_CALL;
    }
    if (strcasecmp(name, "emit") == 0) {
        return YUI_ACTION_EMIT;
    }
    return YUI_ACTION_INVALID;
}

static void yui_free_tmp_args(char **args, size_t count)
{
    if (!args) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        free(args[i]);
    }
}

static esp_err_t yui_collect_args(char *text, char **out_args, size_t max_args, size_t *out_count)
{
    if (!out_args || !out_count) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_count = 0;
    if (!text) {
        return ESP_OK;
    }

    char *token_start = text;
    bool in_quotes = false;
    char quote_char = '\0';
    int brace_depth = 0;
    bool warned_overflow = false;

    for (char *cursor = text;; ++cursor) {
        char ch = *cursor;
        bool at_end = (ch == '\0');
        if (!at_end) {
            if ((ch == '"' || ch == '\'') && (cursor == text || cursor[-1] != '\\')) {
                if (in_quotes && quote_char == ch) {
                    in_quotes = false;
                    quote_char = '\0';
                } else if (!in_quotes) {
                    in_quotes = true;
                    quote_char = ch;
                }
            } else if (!in_quotes) {
                if (ch == '{' && cursor[1] == '{') {
                    ++brace_depth;
                } else if (ch == '}' && cursor[1] == '}' && brace_depth > 0) {
                    --brace_depth;
                } else if (ch == ',' && brace_depth == 0) {
                    *cursor = '\0';
                    char *trimmed = yui_trim_inplace(token_start);
                    if (*trimmed != '\0') {
                        yui_strip_quotes(trimmed);
                        if (*out_count < max_args) {
                            out_args[*out_count] = yui_strdup(trimmed);
                            if (!out_args[*out_count]) {
                                return ESP_ERR_NO_MEM;
                            }
                            (*out_count)++;
                        } else if (!warned_overflow) {
                            ESP_LOGW(TAG, "Dropping extra action argument '%s'", trimmed);
                            warned_overflow = true;
                        }
                    }
                    token_start = cursor + 1;
                }
            }
        }

        if (at_end) {
            char *trimmed = yui_trim_inplace(token_start);
            if (*trimmed != '\0') {
                yui_strip_quotes(trimmed);
                if (*out_count < max_args) {
                    out_args[*out_count] = yui_strdup(trimmed);
                    if (!out_args[*out_count]) {
                        return ESP_ERR_NO_MEM;
                    }
                    (*out_count)++;
                } else if (!warned_overflow) {
                    ESP_LOGW(TAG, "Dropping extra action argument '%s'", trimmed);
                    warned_overflow = true;
                }
            }
            break;
        }
    }

    return ESP_OK;
}

static esp_err_t yui_action_parse_text(const char *text, yui_action_t *out)
{
    if (!text || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    char *scratch = yui_strdup(text);
    if (!scratch) {
        return ESP_ERR_NO_MEM;
    }
    char *trimmed = yui_trim_inplace(scratch);
    if (*trimmed == '\0') {
        free(scratch);
        return ESP_ERR_INVALID_ARG;
    }

    char *arg_block = NULL;
    char *paren = strchr(trimmed, '(');
    if (paren) {
        *paren = '\0';
        arg_block = paren + 1;
        char *close = strrchr(arg_block, ')');
        if (close) {
            *close = '\0';
        }
    }

    yui_action_type_t type = yui_action_type_from_name(trimmed);
    if (type == YUI_ACTION_INVALID) {
        ESP_LOGW(TAG, "Unsupported action '%s'", trimmed);
        free(scratch);
        return ESP_ERR_INVALID_ARG;
    }
    out->type = type;

    if (arg_block) {
        char *args[YUI_ACTION_MAX_ARGS] = {0};
        size_t arg_count = 0;
        esp_err_t err = yui_collect_args(arg_block, args, YUI_ACTION_MAX_ARGS, &arg_count);
        if (err != ESP_OK) {
            yui_free_tmp_args(args, arg_count);
            free(scratch);
            return err;
        }
        if (arg_count > 0U) {
            out->arg0 = args[0];
        }
        if (arg_count > 1U) {
            out->arg1 = args[1];
        }
        if (arg_count > 2U) {
            out->arg2 = args[2];
        }
    }

    free(scratch);
    return ESP_OK;
}

static const char *yui_eval_arg(const char *arg, const yui_action_eval_ctx_t *ctx, char *buffer, size_t buffer_len)
{
    if (!arg) {
        return "";
    }
    if (!ctx || !ctx->resolver) {
        return arg;
    }
    size_t len = strlen(arg);
    if (len < 4U) {
        return arg;
    }
    const char *start = arg;
    while (*start && isspace((unsigned char)*start)) {
        ++start;
        --len;
    }
    while (len > 0U && isspace((unsigned char)start[len - 1U])) {
        --len;
    }
    if (len >= 4U && start[0] == '{' && start[1] == '{' && start[len - 2U] == '}' && start[len - 1U] == '}') {
        size_t expr_len = len - 4U;
        char *expr = (char *)malloc(expr_len + 1U);
        if (!expr) {
            buffer[0] = '\0';
            return buffer;
        }
        memcpy(expr, start + 2, expr_len);
        expr[expr_len] = '\0';
        const char *result = ctx->resolver(expr, ctx->resolver_ctx, buffer, buffer_len);
        free(expr);
        if (!result) {
            buffer[0] = '\0';
            return buffer;
        }
        return result;
    }
    return arg;
}

static esp_err_t yui_execute_set(const yui_action_t *action, const yui_action_eval_ctx_t *ctx)
{
    if (!action->arg0) {
        ESP_LOGW(TAG, "set action missing key argument");
        return ESP_ERR_INVALID_ARG;
    }
    char key_buffer[YUI_ACTION_EVAL_BUFFER];
    char value_buffer[YUI_ACTION_EVAL_BUFFER];
    const char *key = yui_eval_arg(action->arg0, ctx, key_buffer, sizeof(key_buffer));
    const char *value = action->arg1 ? yui_eval_arg(action->arg1, ctx, value_buffer, sizeof(value_buffer)) : "";
    if (!key || key[0] == '\0') {
        ESP_LOGW(TAG, "set action resolved to empty key");
        return ESP_ERR_INVALID_ARG;
    }
    if (!value) {
        value = "";
    }
    return yui_state_set(key, value);
}

static esp_err_t yui_execute_goto(const yui_action_t *action, const yui_action_eval_ctx_t *ctx)
{
    if (!s_runtime.goto_screen) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    char buffer[YUI_ACTION_EVAL_BUFFER];
    const char *target = yui_eval_arg(action->arg0, ctx, buffer, sizeof(buffer));
    return s_runtime.goto_screen(target);
}

static esp_err_t yui_execute_push(const yui_action_t *action, const yui_action_eval_ctx_t *ctx)
{
    if (!s_runtime.push_screen) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    char buffer[YUI_ACTION_EVAL_BUFFER];
    const char *target = yui_eval_arg(action->arg0, ctx, buffer, sizeof(buffer));
    return s_runtime.push_screen(target);
}

static esp_err_t yui_execute_pop(void)
{
    if (!s_runtime.pop_screen) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    return s_runtime.pop_screen();
}

static esp_err_t yui_execute_modal(const yui_action_t *action, const yui_action_eval_ctx_t *ctx)
{
    if (!s_runtime.show_modal) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    char buffer[YUI_ACTION_EVAL_BUFFER];
    const char *component = yui_eval_arg(action->arg0, ctx, buffer, sizeof(buffer));
    if (!component || component[0] == '\0') {
        ESP_LOGW(TAG, "modal action missing component argument");
        return ESP_ERR_INVALID_ARG;
    }
    return s_runtime.show_modal(component);
}

static esp_err_t yui_execute_close_modal(void)
{
    if (!s_runtime.close_modal) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    return s_runtime.close_modal();
}

static esp_err_t yui_execute_call(const yui_action_t *action, const yui_action_eval_ctx_t *ctx)
{
    if (!s_runtime.call_native) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!action->arg0) {
        ESP_LOGW(TAG, "call action missing function name");
        return ESP_ERR_INVALID_ARG;
    }
    char fn_buffer[YUI_ACTION_EVAL_BUFFER];
    const char *function = yui_eval_arg(action->arg0, ctx, fn_buffer, sizeof(fn_buffer));
    if (!function || function[0] == '\0') {
        ESP_LOGW(TAG, "call action resolved to empty function name");
        return ESP_ERR_INVALID_ARG;
    }

    const char *args[YUI_ACTION_MAX_ARGS - 1] = {0};
    char arg_buffers[YUI_ACTION_MAX_ARGS - 1][YUI_ACTION_EVAL_BUFFER];
    size_t arg_count = 0;
    if (action->arg1) {
        args[arg_count] = yui_eval_arg(action->arg1, ctx, arg_buffers[arg_count], sizeof(arg_buffers[arg_count]));
        arg_count++;
    }
    if (action->arg2) {
        args[arg_count] = yui_eval_arg(action->arg2, ctx, arg_buffers[arg_count], sizeof(arg_buffers[arg_count]));
        arg_count++;
    }

    return s_runtime.call_native(function, args, arg_count);
}

static esp_err_t yui_execute_emit(const yui_action_t *action, const yui_action_eval_ctx_t *ctx)
{
    if (!s_runtime.emit_event) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!action->arg0) {
        ESP_LOGW(TAG, "emit action missing event name");
        return ESP_ERR_INVALID_ARG;
    }
    char event_buffer[YUI_ACTION_EVAL_BUFFER];
    const char *event_name = yui_eval_arg(action->arg0, ctx, event_buffer, sizeof(event_buffer));
    if (!event_name || event_name[0] == '\0') {
        ESP_LOGW(TAG, "emit action resolved to empty event name");
        return ESP_ERR_INVALID_ARG;
    }

    const char *args[YUI_ACTION_MAX_ARGS - 1] = {0};
    char arg_buffers[YUI_ACTION_MAX_ARGS - 1][YUI_ACTION_EVAL_BUFFER];
    size_t arg_count = 0;
    if (action->arg1) {
        args[arg_count] = yui_eval_arg(action->arg1, ctx, arg_buffers[arg_count], sizeof(arg_buffers[arg_count]));
        arg_count++;
    }
    if (action->arg2) {
        args[arg_count] = yui_eval_arg(action->arg2, ctx, arg_buffers[arg_count], sizeof(arg_buffers[arg_count]));
        arg_count++;
    }

    return s_runtime.emit_event(event_name, args, arg_count);
}

static esp_err_t yui_execute_unimplemented(const yui_action_t *action, const char *label)
{
    (void)action;
    ESP_LOGW(TAG, "%s action not implemented yet", label);
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t yui_execute_action(const yui_action_t *action, const yui_action_eval_ctx_t *ctx)
{
    switch (action->type) {
        case YUI_ACTION_SET:
            return yui_execute_set(action, ctx);
        case YUI_ACTION_GOTO:
            return yui_execute_goto(action, ctx);
        case YUI_ACTION_PUSH:
            return yui_execute_push(action, ctx);
        case YUI_ACTION_POP:
            return yui_execute_pop();
        case YUI_ACTION_MODAL:
            return yui_execute_modal(action, ctx);
        case YUI_ACTION_CLOSE_MODAL:
            return yui_execute_close_modal();
        case YUI_ACTION_CALL:
            return yui_execute_call(action, ctx);
        case YUI_ACTION_EMIT:
            return yui_execute_emit(action, ctx);
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t yui_action_list_from_node(const yml_node_t *node, yui_action_list_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (!node) {
        return ESP_OK;
    }

    yml_node_type_t type = yml_node_get_type(node);
    if (type == YML_NODE_SCALAR) {
        const char *scalar = yml_node_get_scalar(node);
        if (!scalar) {
            return ESP_ERR_INVALID_ARG;
        }
        out->items = (yui_action_t *)calloc(1, sizeof(yui_action_t));
        if (!out->items) {
            return ESP_ERR_NO_MEM;
        }
        esp_err_t err = yui_action_parse_text(scalar, &out->items[0]);
        if (err != ESP_OK) {
            yui_action_list_free(out);
            return err;
        }
        out->count = 1;
        return ESP_OK;
    }

    if (type == YML_NODE_SEQUENCE) {
        size_t count = yml_node_child_count(node);
        if (count == 0U) {
            return ESP_OK;
        }
        out->items = (yui_action_t *)calloc(count, sizeof(yui_action_t));
        if (!out->items) {
            return ESP_ERR_NO_MEM;
        }
        size_t idx = 0;
        for (const yml_node_t *child = yml_node_child_at(node, 0); child; child = yml_node_next(child)) {
            if (yml_node_get_type(child) != YML_NODE_SCALAR) {
                ESP_LOGW(TAG, "Action entries must be scalars");
                yui_action_list_free(out);
                return ESP_ERR_INVALID_ARG;
            }
            const char *scalar = yml_node_get_scalar(child);
            if (!scalar) {
                yui_action_list_free(out);
                return ESP_ERR_INVALID_ARG;
            }
            esp_err_t err = yui_action_parse_text(scalar, &out->items[idx]);
            if (err != ESP_OK) {
                yui_action_list_free(out);
                return err;
            }
            idx++;
        }
        out->count = idx;
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Unsupported event node type %d", (int)type);
    return ESP_ERR_INVALID_ARG;
}

void yui_action_list_free(yui_action_list_t *list)
{
    if (!list || !list->items) {
        return;
    }
    for (size_t i = 0; i < list->count; ++i) {
        free(list->items[i].arg0);
        free(list->items[i].arg1);
        free(list->items[i].arg2);
        list->items[i].arg0 = NULL;
        list->items[i].arg1 = NULL;
        list->items[i].arg2 = NULL;
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
}

esp_err_t yui_action_list_execute(const yui_action_list_t *list, const yui_action_eval_ctx_t *ctx)
{
    if (!list || list->count == 0U) {
        return ESP_OK;
    }
    esp_err_t first_err = ESP_OK;
    for (size_t i = 0; i < list->count; ++i) {
        esp_err_t err = yui_execute_action(&list->items[i], ctx);
        if (err != ESP_OK && first_err == ESP_OK) {
            first_err = err;
        }
    }
    return first_err;
}
