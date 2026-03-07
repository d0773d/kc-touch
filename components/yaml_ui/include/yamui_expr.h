#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    YUI_EXPR_VALUE_NULL = 0,
    YUI_EXPR_VALUE_BOOL,
    YUI_EXPR_VALUE_NUMBER,
    YUI_EXPR_VALUE_STRING,
} yui_expr_value_type_t;

typedef struct {
    yui_expr_value_type_t type;
    double number;
    bool boolean;
    const char *string;
    char *owned_string;
} yui_expr_value_t;

typedef bool (*yui_expr_symbol_resolver_t)(const char *identifier, void *ctx, yui_expr_value_t *out);
typedef void (*yui_expr_identifier_cb_t)(const char *identifier, void *ctx);

void yui_expr_value_reset(yui_expr_value_t *value);
void yui_expr_value_set_string_copy(yui_expr_value_t *value, const char *text);
void yui_expr_value_set_string_ref(yui_expr_value_t *value, const char *text);
void yui_expr_value_set_number(yui_expr_value_t *value, double number);
void yui_expr_value_set_bool(yui_expr_value_t *value, bool flag);

esp_err_t yui_expr_eval(const char *expression, yui_expr_symbol_resolver_t resolver, void *ctx, yui_expr_value_t *out_value);
esp_err_t yui_expr_eval_to_string(const char *expression, yui_expr_symbol_resolver_t resolver, void *ctx, char *out, size_t out_len);
esp_err_t yui_expr_collect_identifiers(const char *expression, yui_expr_identifier_cb_t cb, void *ctx);

#ifdef __cplusplus
}
#endif
