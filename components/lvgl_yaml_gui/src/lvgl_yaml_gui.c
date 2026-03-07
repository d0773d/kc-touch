#include "lvgl_yaml_gui.h"

#include "esp_timer.h"
#include "kc_touch_display.h"
#include "lvgl.h"
#include "ui_schemas.h"
#include "yaml_core.h"
#include "yaml_ui.h"
#include "yamui_events.h"
#include "yamui_expr.h"
#include "yamui_logging.h"
#include "yamui_runtime.h"
#include "yamui_state.h"
#include "yui_navigation_queue.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define YUI_TEXT_BUFFER_MAX 256

#ifndef LV_FLEX_ALIGN_STRETCH
#define LV_FLEX_ALIGN_STRETCH LV_FLEX_ALIGN_START
#endif

typedef struct yui_component_scope yui_component_scope_t;
typedef struct yui_component_prop yui_component_prop_t;
typedef struct yui_widget_runtime yui_widget_runtime_t;

typedef struct {
    char *name;
    yml_node_t *root;
    yui_schema_t schema;
} yui_schema_runtime_t;

typedef struct {
    yui_schema_runtime_t *schema;
    char *screen_name;
} yui_screen_frame_t;

struct yui_component_prop {
    char *name;
    char *template_value;
    char *resolved_value;
    char **dependencies;
    size_t dependency_count;
};

struct yui_component_scope {
    yui_component_scope_t *parent;
    yui_component_prop_t *props;
    size_t prop_count;
    size_t ref_count;
};

struct yui_widget_runtime {
    lv_obj_t *event_target;
    lv_obj_t *text_target;
    char *text_template;
    char **bindings;
    size_t binding_count;
    yui_state_watch_handle_t *watch_handles;
    size_t watch_count;
    yui_component_scope_t *scope;
    yui_widget_events_t events;
};

static void yui_widget_refresh_text(yui_widget_runtime_t *runtime);
static void yui_format_text(const char *tmpl, yui_component_scope_t *scope, char *out, size_t out_len);
static char *yui_strdup_local(const char *value);
static esp_err_t yui_collect_bindings_from_text(const char *text, char ***out_tokens, size_t *out_count);
static void yui_apply_layout(lv_obj_t *obj, const yml_node_t *layout_node, const char *default_type);
static lv_flex_align_t yui_flex_align_from_string(const char *value, lv_flex_align_t def);
static esp_err_t yui_render_widget_list(const yml_node_t *widgets_node, yui_schema_runtime_t *schema, lv_obj_t *parent, yui_component_scope_t *scope);

typedef struct {
    const char *yaml_key;
    yui_widget_event_type_t event_type;
    lv_event_code_t lv_event;
} yui_widget_event_field_t;

typedef struct {
    yui_component_scope_t *scope;
} yui_expression_ctx_t;

typedef struct {
    const lv_event_t *lv_event;
    yui_component_scope_t *scope;
} yui_event_resolver_ctx_t;

static const yui_widget_event_field_t s_widget_events[] = {
    {"on_click", YUI_WIDGET_EVENT_CLICK, LV_EVENT_CLICKED},
    {"on_press", YUI_WIDGET_EVENT_PRESS, LV_EVENT_PRESSED},
    {"on_release", YUI_WIDGET_EVENT_RELEASE, LV_EVENT_RELEASED},
    {"on_change", YUI_WIDGET_EVENT_CHANGE, LV_EVENT_VALUE_CHANGED},
    {"on_focus", YUI_WIDGET_EVENT_FOCUS, LV_EVENT_FOCUSED},
    {"on_blur", YUI_WIDGET_EVENT_BLUR, LV_EVENT_DEFOCUSED},
};
static const size_t s_widget_event_count = sizeof(s_widget_events) / sizeof(s_widget_events[0]);

typedef struct {
    const char *name;
    const char *glyph;
} yui_symbol_entry_t;

static const yui_symbol_entry_t s_symbol_entries[] = {
    {"wifi", LV_SYMBOL_WIFI},
    {"ok", LV_SYMBOL_OK},
    {"warning", LV_SYMBOL_WARNING},
    {"left", LV_SYMBOL_LEFT},
    {"right", LV_SYMBOL_RIGHT},
};

static const char *yui_symbol_lookup(const char *name)
{
    if (!name) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(s_symbol_entries) / sizeof(s_symbol_entries[0]); ++i) {
        if (strcasecmp(name, s_symbol_entries[i].name) == 0) {
            return s_symbol_entries[i].glyph;
        }
    }
    return NULL;
}

static void yui_scope_acquire(yui_component_scope_t *scope)
{
    if (scope) {
        scope->ref_count++;
    }
}

static void yui_scope_destroy(yui_component_scope_t *scope);

static void yui_scope_release(yui_component_scope_t *scope)
{
    if (!scope || scope->ref_count == 0U) {
        return;
    }
    scope->ref_count--;
    if (scope->ref_count == 0U) {
        yui_scope_destroy(scope);
    }
}

static void yui_prop_free_dependencies(yui_component_prop_t *prop)
{
    if (!prop || !prop->dependencies) {
        return;
    }
    for (size_t i = 0; i < prop->dependency_count; ++i) {
        free(prop->dependencies[i]);
    }
    free(prop->dependencies);
    prop->dependencies = NULL;
    prop->dependency_count = 0U;
}

static void yui_scope_destroy(yui_component_scope_t *scope)
{
    if (!scope) {
        return;
    }
    if (scope->props) {
        for (size_t i = 0; i < scope->prop_count; ++i) {
            yui_component_prop_t *prop = &scope->props[i];
            free(prop->name);
            free(prop->template_value);
            free(prop->resolved_value);
            yui_prop_free_dependencies(prop);
        }
        free(scope->props);
    }
    scope->props = NULL;
    scope->prop_count = 0U;
    if (scope->parent) {
        yui_scope_release(scope->parent);
    }
    free(scope);
}

static yui_component_prop_t *yui_scope_find_prop(yui_component_scope_t *scope, const char *name)
{
    if (!scope || !name) {
        return NULL;
    }
    for (yui_component_scope_t *cursor = scope; cursor; cursor = cursor->parent) {
        for (size_t i = 0; i < cursor->prop_count; ++i) {
            yui_component_prop_t *prop = &cursor->props[i];
            if (prop->name && strcmp(prop->name, name) == 0) {
                return prop;
            }
        }
    }
    return NULL;
}

static yui_component_scope_t *yui_scope_create(yui_component_scope_t *parent, const yui_component_def_t *component, const yml_node_t *instance_node)
{
    yui_component_scope_t *scope = (yui_component_scope_t *)calloc(1, sizeof(yui_component_scope_t));
    if (!scope) {
        return NULL;
    }
    scope->parent = parent;
    scope->ref_count = 1U;
    if (parent) {
        yui_scope_acquire(parent);
    }
    if (!component || component->prop_count == 0U) {
        return scope;
    }
    scope->prop_count = component->prop_count;
    scope->props = (yui_component_prop_t *)calloc(scope->prop_count, sizeof(yui_component_prop_t));
    if (!scope->props) {
        yui_scope_release(scope);
        return NULL;
    }
    for (size_t i = 0; i < component->prop_count; ++i) {
        yui_component_prop_t *prop = &scope->props[i];
        const char *prop_name = component->props[i];
        prop->name = prop_name ? yui_strdup_local(prop_name) : NULL;
        const yml_node_t *value_node = (instance_node && prop_name) ? yml_node_get_child(instance_node, prop_name) : NULL;
        const char *scalar = value_node ? yml_node_get_scalar(value_node) : NULL;
        prop->template_value = scalar ? yui_strdup_local(scalar) : yui_strdup_local("");
        if (!prop->template_value) {
            yui_scope_release(scope);
            return NULL;
        }
        esp_err_t dep_err = yui_collect_bindings_from_text(prop->template_value, &prop->dependencies, &prop->dependency_count);
        if (dep_err != ESP_OK) {
            yui_scope_release(scope);
            return NULL;
        }
    }
    return scope;
}

static const char *yui_scope_resolve_prop(yui_component_scope_t *scope, const char *name)
{
    yui_component_prop_t *prop = yui_scope_find_prop(scope, name);
    if (!prop) {
        return NULL;
    }
    if (!prop->template_value) {
        return prop->resolved_value ? prop->resolved_value : "";
    }
    yui_component_scope_t *resolver_scope = scope ? scope->parent : NULL;
    char buffer[YUI_TEXT_BUFFER_MAX];
    buffer[0] = '\0';
    yui_format_text(prop->template_value, resolver_scope, buffer, sizeof(buffer));
    if (!prop->resolved_value || strcmp(prop->resolved_value, buffer) != 0) {
        free(prop->resolved_value);
        prop->resolved_value = yui_strdup_local(buffer);
    }
    return prop->resolved_value ? prop->resolved_value : "";
}

static void yui_scope_delete_cb(lv_event_t *event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_DELETE) {
        return;
    }
    yui_component_scope_t *scope = (yui_component_scope_t *)lv_event_get_user_data(event);
    yui_scope_release(scope);
}

static yui_schema_runtime_t *s_loaded_schema;
static yui_screen_frame_t *s_nav_stack;
static size_t s_nav_count;
static size_t s_nav_capacity;
typedef struct {
    lv_obj_t *overlay;
} yui_modal_frame_t;
static yui_modal_frame_t *s_modal_stack;
static size_t s_modal_count;
static size_t s_modal_capacity;

static esp_err_t yui_modal_ensure_capacity(size_t desired)
{
    if (desired <= s_modal_capacity) {
        return ESP_OK;
    }
    size_t new_capacity = s_modal_capacity == 0 ? 2U : s_modal_capacity * 2U;
    while (new_capacity < desired) {
        new_capacity *= 2U;
    }
    yui_modal_frame_t *next = (yui_modal_frame_t *)realloc(s_modal_stack, new_capacity * sizeof(yui_modal_frame_t));
    if (!next) {
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = s_modal_capacity; i < new_capacity; ++i) {
        next[i].overlay = NULL;
    }
    s_modal_stack = next;
    s_modal_capacity = new_capacity;
    return ESP_OK;
}

static void yui_modal_close_all(void)
{
    while (s_modal_count > 0U) {
        yui_modal_frame_t *frame = &s_modal_stack[--s_modal_count];
        if (frame->overlay) {
            lv_obj_del(frame->overlay);
            frame->overlay = NULL;
        }
    }
}

static esp_err_t yui_modal_close_top(void)
{
    if (s_modal_count == 0U) {
        return ESP_ERR_INVALID_STATE;
    }
    yui_modal_frame_t *frame = &s_modal_stack[s_modal_count - 1U];
    if (frame->overlay) {
        lv_obj_del(frame->overlay);
        frame->overlay = NULL;
    }
    s_modal_count--;
    return ESP_OK;
}

static esp_err_t yui_modal_show_component(const char *component_name)
{
    if (!s_loaded_schema) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!component_name || component_name[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    const yui_component_def_t *component = yui_schema_get_component(&s_loaded_schema->schema, component_name);
    if (!component) {
        return ESP_ERR_NOT_FOUND;
    }
    esp_err_t err = yui_modal_ensure_capacity(s_modal_count + 1U);
    if (err != ESP_OK) {
        return err;
    }
    lv_obj_t *root = lv_scr_act();
    if (!root) {
        return ESP_FAIL;
    }
    lv_obj_t *overlay = lv_obj_create(root);
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_60, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *panel = lv_obj_create(overlay);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x25293C), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(panel, 18, 0);
    lv_obj_set_style_radius(panel, 16, 0);
    
    /* Panel Sizing: Fixed width, dynamic height up to 90% of screen */
    lv_obj_set_width(panel, 420);
    lv_obj_set_height(panel, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(panel, lv_pct(90), 0);
    
    /* Allow scrolling if content is too tall */
    lv_obj_add_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_set_style_pad_row(panel, 12, 0);
    lv_obj_set_style_pad_column(panel, 12, 0);
    lv_obj_center(panel);
    yui_apply_layout(panel, component->layout_node, "column");

    yui_component_scope_t *scope = yui_scope_create(NULL, component, NULL);
    if (!scope) {
        lv_obj_del(overlay);
        return ESP_ERR_NO_MEM;
    }
    lv_obj_add_event_cb(panel, yui_scope_delete_cb, LV_EVENT_DELETE, scope);
    err = yui_render_widget_list(component->widgets_node, s_loaded_schema, panel, scope);
    if (err != ESP_OK) {
        lv_obj_del(overlay);
        return err;
    }
    s_modal_stack[s_modal_count++] = (yui_modal_frame_t){
        .overlay = overlay,
    };
    return ESP_OK;
}

static esp_err_t yui_runtime_goto_screen(const char *screen);
static esp_err_t yui_runtime_push_screen(const char *screen);
static esp_err_t yui_runtime_pop_screen(void);
static esp_err_t yui_runtime_show_modal(const char *component);
static esp_err_t yui_runtime_close_modal(void);
static esp_err_t yui_runtime_call_native(const char *function, const char **args, size_t arg_count);
static esp_err_t yui_runtime_emit_event(const char *event, const char **args, size_t arg_count);

static const yui_action_runtime_t s_runtime_vtable = {
    .goto_screen = yui_runtime_goto_screen,
    .push_screen = yui_runtime_push_screen,
    .pop_screen = yui_runtime_pop_screen,
    .show_modal = yui_runtime_show_modal,
    .close_modal = yui_runtime_close_modal,
    .call_native = yui_runtime_call_native,
    .emit_event = yui_runtime_emit_event,
};

static bool yui_expression_symbol_resolver(const char *identifier, void *ctx, yui_expr_value_t *out)
{
    if (!out) {
        return false;
    }
    yui_expr_value_reset(out);
    if (!identifier || identifier[0] == '\0') {
        return true;
    }
    yui_expression_ctx_t *expr_ctx = (yui_expression_ctx_t *)ctx;
    const char *value = NULL;
    if (expr_ctx && expr_ctx->scope) {
        value = yui_scope_resolve_prop(expr_ctx->scope, identifier);
    }
    if (!value) {
        value = yui_state_get(identifier, "");
    }
    if (!value) {
        value = "";
    }
    yui_expr_value_set_string_ref(out, value);
    return true;
}

static const char *yui_event_resolve_value(const yui_event_resolver_ctx_t *ctx, char *buffer, size_t buffer_len)
{
    if (!buffer || buffer_len == 0U) {
        return "";
    }
    buffer[0] = '\0';
    if (!ctx || !ctx->lv_event) {
        return buffer;
    }
    lv_event_t *evt = (lv_event_t *)ctx->lv_event;
    lv_obj_t *target = lv_event_get_target(evt);
    if (!target) {
        return buffer;
    }
#if LV_USE_TEXTAREA
    if (lv_obj_check_type(target, &lv_textarea_class)) {
        const char *text = lv_textarea_get_text(target);
        if (text) {
            strncpy(buffer, text, buffer_len - 1U);
            buffer[buffer_len - 1U] = '\0';
        }
        return buffer;
    }
#endif
#if LV_USE_DROPDOWN
    if (lv_obj_check_type(target, &lv_dropdown_class)) {
        lv_dropdown_get_selected_str(target, buffer, buffer_len);
        return buffer;
    }
#endif
#if LV_USE_SLIDER
    if (lv_obj_check_type(target, &lv_slider_class)) {
        int32_t value = lv_slider_get_value(target);
        snprintf(buffer, buffer_len, "%ld", (long)value);
        return buffer;
    }
#endif
    const void *param = lv_event_get_param(evt);
    if (param) {
        strncpy(buffer, (const char *)param, buffer_len - 1U);
        buffer[buffer_len - 1U] = '\0';
    }
    return buffer;
}

static const char *yui_event_resolve_checked(const yui_event_resolver_ctx_t *ctx, char *buffer, size_t buffer_len)
{
    if (!buffer || buffer_len == 0U) {
        return "";
    }
    bool checked = false;
    if (ctx && ctx->lv_event) {
        lv_event_t *evt = (lv_event_t *)ctx->lv_event;
        lv_obj_t *target = lv_event_get_target(evt);
        checked = target && lv_obj_has_state(target, LV_STATE_CHECKED);
    }
    strncpy(buffer, checked ? "true" : "false", buffer_len - 1U);
    buffer[buffer_len - 1U] = '\0';
    return buffer;
}

const char *yui_event_symbol_resolver(const char *symbol, void *ctx, char *buffer, size_t buffer_len)
{
    if (!symbol) {
        if (buffer && buffer_len > 0U) {
            buffer[0] = '\0';
        }
        return buffer;
    }
    yui_event_resolver_ctx_t *resolver_ctx = (yui_event_resolver_ctx_t *)ctx;
    if (strcmp(symbol, "value") == 0) {
        return yui_event_resolve_value(resolver_ctx, buffer, buffer_len);
    }
    if (strcmp(symbol, "checked") == 0) {
        return yui_event_resolve_checked(resolver_ctx, buffer, buffer_len);
    }
    const char *state_value = NULL;
    if (resolver_ctx && resolver_ctx->scope) {
        state_value = yui_scope_resolve_prop(resolver_ctx->scope, symbol);
    }
    if (!state_value) {
        state_value = yui_state_get(symbol, "");
    }
    if (state_value) {
        return state_value;
    }
    if (buffer && buffer_len > 0U) {
        buffer[0] = '\0';
    }
    return buffer;
}

static char *yui_strdup_local(const char *value)
{
    if (!value) {
        return NULL;
    }
    size_t len = strlen(value) + 1U;
    char *copy = (char *)malloc(len);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, value, len);
    return copy;
}

static const char *yui_node_scalar(const yml_node_t *node, const char *key)
{
    if (!node || !key) {
        return NULL;
    }
    const yml_node_t *child = yml_node_get_child(node, key);
    return child ? yml_node_get_scalar(child) : NULL;
}

static int32_t yui_node_i32(const yml_node_t *node, const char *key, int32_t def)
{
    const char *scalar = yui_node_scalar(node, key);
    return scalar ? atoi(scalar) : def;
}

static void yui_widget_runtime_destroy(void *ptr)
{
    yui_widget_runtime_t *runtime = (yui_widget_runtime_t *)ptr;
    if (!runtime) {
        return;
    }
    if (runtime->watch_handles) {
        for (size_t i = 0; i < runtime->watch_count; ++i) {
            if (runtime->watch_handles[i] != 0U) {
                yui_state_unwatch(runtime->watch_handles[i]);
            }
        }
        free(runtime->watch_handles);
    }
    if (runtime->bindings) {
        for (size_t i = 0; i < runtime->binding_count; ++i) {
            free(runtime->bindings[i]);
        }
        free(runtime->bindings);
    }
    for (size_t i = 0; i < YUI_WIDGET_EVENT_COUNT; ++i) {
        yui_action_list_free(&runtime->events.lists[i]);
    }
    if (runtime->scope) {
        yui_scope_release(runtime->scope);
        runtime->scope = NULL;
    }
    free(runtime->text_template);
    free(runtime);
}

static void yui_widget_event_cb(lv_event_t *event)
{
    yui_widget_runtime_t *runtime = (yui_widget_runtime_t *)lv_event_get_user_data(event);
    if (!runtime) {
        return;
    }
    if (lv_event_get_code(event) == LV_EVENT_DELETE) {
        yui_widget_runtime_destroy(runtime);
        return;
    }
    lv_event_code_t code = lv_event_get_code(event);
    yui_widget_event_type_t type = YUI_WIDGET_EVENT_INVALID;
    for (size_t i = 0; i < s_widget_event_count; ++i) {
        if (s_widget_events[i].lv_event == code) {
            type = s_widget_events[i].event_type;
            break;
        }
    }
    if (type == YUI_WIDGET_EVENT_INVALID) {
        return;
    }
    const yui_action_list_t *list = &runtime->events.lists[type];
    if (!list || list->count == 0U) {
        return;
    }
    yui_event_resolver_ctx_t resolver_ctx = {
        .lv_event = event,
        .scope = runtime->scope,
    };
    yui_action_eval_ctx_t eval_ctx = {
        .resolver = yui_event_symbol_resolver,
        .resolver_ctx = &resolver_ctx,
    };
    esp_err_t err = yui_action_list_execute(list, &eval_ctx);
    if (err != ESP_OK) {
        yamui_log(YAMUI_LOG_LEVEL_WARN, YAMUI_LOG_CAT_ACTION, "Widget action failed (%s)", esp_err_to_name(err));
    }
}

static yui_widget_runtime_t *yui_widget_runtime_create(lv_obj_t *event_target, yui_component_scope_t *scope)
{
    if (!event_target) {
        return NULL;
    }
    yui_widget_runtime_t *runtime = (yui_widget_runtime_t *)calloc(1, sizeof(yui_widget_runtime_t));
    if (!runtime) {
        return NULL;
    }
    runtime->event_target = event_target;
    runtime->text_target = event_target;
    runtime->scope = scope;
    if (scope) {
        yui_scope_acquire(scope);
    }
    lv_obj_add_event_cb(event_target, yui_widget_event_cb, LV_EVENT_ALL, runtime);
    return runtime;
}

static void yui_format_text(const char *tmpl, yui_component_scope_t *scope, char *out, size_t out_len)
{
    if (!tmpl || !out || out_len == 0U) {
        return;
    }
    yui_expression_ctx_t ctx = {
        .scope = scope,
    };
    size_t pos = 0;
    out[0] = '\0';
    while (*tmpl && pos + 1U < out_len) {
        if (tmpl[0] == '{' && tmpl[1] == '{') {
            const char *end = strstr(tmpl + 2, "}}");
            if (!end) {
                break;
            }
            size_t expr_len = (size_t)(end - (tmpl + 2));
            char *expr = (char *)malloc(expr_len + 1U);
            if (!expr) {
                break;
            }
            memcpy(expr, tmpl + 2, expr_len);
            expr[expr_len] = '\0';
            size_t remaining = out_len - pos;
            esp_err_t err = yui_expr_eval_to_string(expr, yui_expression_symbol_resolver, &ctx, out + pos, remaining);
            free(expr);
            if (err == ESP_OK) {
                pos += strlen(out + pos);
            }
            tmpl = end + 2;
            continue;
        }
        out[pos++] = *tmpl++;
    }
    out[pos] = '\0';
}

static void yui_widget_refresh_text(yui_widget_runtime_t *runtime)
{
    if (!runtime || !runtime->text_target || !runtime->text_template) {
        return;
    }
    char buffer[YUI_TEXT_BUFFER_MAX];
    yui_format_text(runtime->text_template, runtime->scope, buffer, sizeof(buffer));
    lv_label_set_text(runtime->text_target, buffer);
}

static void yui_widget_state_cb(const char *key, const char *value, void *user_ctx)
{
    (void)key;
    (void)value;
    yui_widget_runtime_t *runtime = (yui_widget_runtime_t *)user_ctx;
    if (!runtime) {
        return;
    }
    yui_widget_refresh_text(runtime);
}

static bool yui_is_valid_token(const char *token)
{
    if (!token || token[0] == '\0') {
        return false;
    }
    for (const char *cursor = token; *cursor; ++cursor) {
        char ch = *cursor;
        if (!(isalnum((unsigned char)ch) || ch == '_' || ch == '-' || ch == '.')) {
            return false;
        }
    }
    return true;
}

static esp_err_t yui_collect_bindings_from_text(const char *text, char ***out_tokens, size_t *out_count)
{
    if (!out_tokens || !out_count) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_tokens = NULL;
    *out_count = 0;
    if (!text) {
        return ESP_OK;
    }
    const char *cursor = text;
    while (true) {
        const char *open = strstr(cursor, "{{");
        if (!open) {
            break;
        }
        const char *close = strstr(open + 2, "}}");
        if (!close) {
            break;
        }
        const char *token_start = open + 2;
        size_t len = (size_t)(close - token_start);
        while (len > 0U && isspace((unsigned char)*token_start)) {
            ++token_start;
            --len;
        }
        while (len > 0U && isspace((unsigned char)token_start[len - 1U])) {
            --len;
        }
        if (len > 0U) {
            char *token = (char *)malloc(len + 1U);
            if (!token) {
                return ESP_ERR_NO_MEM;
            }
            memcpy(token, token_start, len);
            token[len] = '\0';
            if (yui_is_valid_token(token)) {
                char **next = (char **)realloc(*out_tokens, (*out_count + 1U) * sizeof(char *));
                if (!next) {
                    free(token);
                    return ESP_ERR_NO_MEM;
                }
                *out_tokens = next;
                (*out_tokens)[(*out_count)++] = token;
            } else {
                free(token);
            }
        }
        cursor = close + 2;
    }
    return ESP_OK;
}

static esp_err_t yui_widget_append_watch(yui_widget_runtime_t *runtime, yui_state_watch_handle_t handle)
{
    if (!runtime || handle == 0U) {
        return ESP_OK;
    }
    yui_state_watch_handle_t *next = (yui_state_watch_handle_t *)realloc(runtime->watch_handles, (runtime->watch_count + 1U) * sizeof(yui_state_watch_handle_t));
    if (!next) {
        return ESP_ERR_NO_MEM;
    }
    runtime->watch_handles = next;
    runtime->watch_handles[runtime->watch_count++] = handle;
    return ESP_OK;
}

static esp_err_t yui_widget_watch_state(yui_widget_runtime_t *runtime, const char *key)
{
    if (!runtime || !key || key[0] == '\0') {
        return ESP_OK;
    }
    yui_state_watch_handle_t handle = 0;
    esp_err_t err = yui_state_watch(key, yui_widget_state_cb, runtime, &handle);
    if (err != ESP_OK || handle == 0U) {
        return err;
    }
    esp_err_t append_err = yui_widget_append_watch(runtime, handle);
    if (append_err != ESP_OK) {
        yui_state_unwatch(handle);
        return append_err;
    }
    return ESP_OK;
}

static esp_err_t yui_widget_bind_text(yui_widget_runtime_t *runtime, const char *text, lv_obj_t *target)
{
    if (!runtime || !text || !target) {
        return ESP_OK;
    }
    runtime->text_target = target;
    runtime->text_template = yui_strdup_local(text);
    if (!runtime->text_template) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = yui_collect_bindings_from_text(text, &runtime->bindings, &runtime->binding_count);
    if (err != ESP_OK) {
        return err;
    }
    for (size_t i = 0; i < runtime->binding_count; ++i) {
        const char *token = runtime->bindings[i];
        if (!token) {
            continue;
        }
        bool handled = false;
        if (runtime->scope) {
            yui_component_prop_t *prop = yui_scope_find_prop(runtime->scope, token);
            if (prop) {
                handled = true;
                for (size_t dep = 0; dep < prop->dependency_count; ++dep) {
                    err = yui_widget_watch_state(runtime, prop->dependencies[dep]);
                    if (err != ESP_OK) {
                        return err;
                    }
                }
            }
        }
        if (!handled) {
            err = yui_widget_watch_state(runtime, token);
            if (err != ESP_OK) {
                return err;
            }
        }
    }
    yui_widget_refresh_text(runtime);
    return ESP_OK;
}

static esp_err_t yui_widget_parse_events(const yml_node_t *node, yui_widget_runtime_t *runtime)
{
    if (!node || !runtime) {
        return ESP_OK;
    }
    for (size_t i = 0; i < s_widget_event_count; ++i) {
        const yml_node_t *event_node = yml_node_get_child(node, s_widget_events[i].yaml_key);
        if (!event_node) {
            continue;
        }
        esp_err_t err = yui_action_list_from_node(event_node, &runtime->events.lists[s_widget_events[i].event_type]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

static lv_color_t yui_color_from_string(const char *hex, lv_color_t fallback)
{
    if (!hex || hex[0] != '#') {
        return fallback;
    }
    size_t len = strlen(hex);
    if (len != 7U && len != 9U) {
        return fallback;
    }
    uint32_t value = (uint32_t)strtoul(hex + 1, NULL, 16);
    if (len == 9U) {
        value >>= 8;
    }
    return lv_color_hex(value & 0xFFFFFFU);
}

static void yui_apply_style(lv_obj_t *obj, const yui_style_t *style)
{
    if (!obj || !style) {
        return;
    }
    if (style->background_color) {
        lv_obj_set_style_bg_color(obj, yui_color_from_string(style->background_color, lv_color_hex(0x101018)), 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    }
    if (style->padding > 0) {
        lv_obj_set_style_pad_all(obj, style->padding, 0);
    }
    if (style->padding_x >= 0) {
        lv_obj_set_style_pad_left(obj, style->padding_x, 0);
        lv_obj_set_style_pad_right(obj, style->padding_x, 0);
    }
    if (style->padding_y >= 0) {
        lv_obj_set_style_pad_top(obj, style->padding_y, 0);
        lv_obj_set_style_pad_bottom(obj, style->padding_y, 0);
    }
    if (style->radius > 0) {
        lv_obj_set_style_radius(obj, style->radius, 0);
    }
}

static void yui_apply_layout(lv_obj_t *obj, const yml_node_t *layout_node, const char *default_type)
{
    const char *type = layout_node ? yui_node_scalar(layout_node, "type") : NULL;
    const char *mode = type ? type : default_type;
    if (!mode || strcmp(mode, "column") == 0) {
        lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
    } else if (strcmp(mode, "row") == 0) {
        lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
    } else {
        lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
    }
    int32_t gap = yui_node_i32(layout_node, "gap", 12);
    lv_obj_set_style_pad_row(obj, gap, 0);
    lv_obj_set_style_pad_column(obj, gap, 0);
    const char *align = layout_node ? yui_node_scalar(layout_node, "align") : NULL;
    const char *justify = layout_node ? yui_node_scalar(layout_node, "justify") : NULL;
    lv_obj_set_flex_align(
        obj,
        yui_flex_align_from_string(justify, LV_FLEX_ALIGN_START),
        yui_flex_align_from_string(align, LV_FLEX_ALIGN_START),
        LV_FLEX_ALIGN_STRETCH);
}

static bool yui_node_parse_size(const yml_node_t *node, const char *key, lv_coord_t *out_value)
{
    if (!node || !key || !out_value) {
        return false;
    }
    const yml_node_t *child = yml_node_get_child(node, key);
    if (!child) {
        return false;
    }
    const char *scalar = yml_node_get_scalar(child);
    if (!scalar) {
        return false;
    }
    size_t len = strlen(scalar);
    if (len > 1U && scalar[len - 1U] == '%') {
        *out_value = LV_PCT(atoi(scalar));
        return true;
    }
    *out_value = (lv_coord_t)atoi(scalar);
    return true;
}

static lv_align_t yui_align_from_string(const char *value, lv_align_t def)
{
    if (!value) {
        return def;
    }
    if (strcasecmp(value, "center") == 0) {
        return LV_ALIGN_CENTER;
    }
    if (strcasecmp(value, "top") == 0) {
        return LV_ALIGN_TOP_MID;
    }
    if (strcasecmp(value, "bottom") == 0) {
        return LV_ALIGN_BOTTOM_MID;
    }
    if (strcasecmp(value, "left") == 0) {
        return LV_ALIGN_LEFT_MID;
    }
    if (strcasecmp(value, "right") == 0) {
        return LV_ALIGN_RIGHT_MID;
    }
    return def;
}

static lv_flex_align_t yui_flex_align_from_string(const char *value, lv_flex_align_t def)
{
    if (!value) {
        return def;
    }
    if (strcasecmp(value, "start") == 0) {
        return LV_FLEX_ALIGN_START;
    }
    if (strcasecmp(value, "center") == 0) {
        return LV_FLEX_ALIGN_CENTER;
    }
    if (strcasecmp(value, "end") == 0) {
        return LV_FLEX_ALIGN_END;
    }
    if (strcasecmp(value, "space_between") == 0) {
        return LV_FLEX_ALIGN_SPACE_BETWEEN;
    }
    if (strcasecmp(value, "space_around") == 0) {
        return LV_FLEX_ALIGN_SPACE_AROUND;
    }
    if (strcasecmp(value, "space_evenly") == 0) {
        return LV_FLEX_ALIGN_SPACE_EVENLY;
    }
    if (strcasecmp(value, "stretch") == 0) {
        return LV_FLEX_ALIGN_STRETCH;
    }
    return def;
}

static void yui_apply_common_widget_attrs(lv_obj_t *obj, const yml_node_t *node, yui_schema_runtime_t *schema)
{
    if (!obj || !node || !schema) {
        return;
    }
    const char *style_name = yui_node_scalar(node, "style");
    if (style_name) {
        const yui_style_t *style = yui_schema_get_style(&schema->schema, style_name);
        yui_apply_style(obj, style);
    }
    lv_coord_t size_value = 0;
    if (yui_node_parse_size(node, "width", &size_value)) {
        lv_obj_set_width(obj, size_value);
    }
    if (yui_node_parse_size(node, "height", &size_value)) {
        lv_obj_set_height(obj, size_value);
    }
    const char *align = yui_node_scalar(node, "align");
    if (align) {
        lv_obj_align(obj, yui_align_from_string(align, LV_ALIGN_CENTER), 0, 0);
    }
    int32_t grow = yui_node_i32(node, "grow", -1);
    if (grow >= 0) {
        lv_obj_set_flex_grow(obj, (uint8_t)grow);
    }
}

static esp_err_t yui_render_widget(const yml_node_t *node, yui_schema_runtime_t *schema, lv_obj_t *parent, yui_component_scope_t *scope);

static esp_err_t yui_render_widget_list(const yml_node_t *widgets_node, yui_schema_runtime_t *schema, lv_obj_t *parent, yui_component_scope_t *scope)
{
    if (!widgets_node || yml_node_get_type(widgets_node) != YML_NODE_SEQUENCE) {
        return ESP_OK;
    }
    for (const yml_node_t *child = yml_node_child_at(widgets_node, 0); child; child = yml_node_next(child)) {
        esp_err_t err = yui_render_widget(child, schema, parent, scope);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

static esp_err_t yui_render_component_instance(const yui_component_def_t *component, const yml_node_t *instance_node, yui_schema_runtime_t *schema, lv_obj_t *parent, yui_component_scope_t *parent_scope)
{
    if (!component || !schema || !parent) {
        return ESP_ERR_INVALID_ARG;
    }
    yui_component_scope_t *scope = yui_scope_create(parent_scope, component, instance_node);
    if (!scope) {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_t *container = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    /* Size to fit content by default */
    lv_obj_set_size(container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    yui_apply_layout(container, component->layout_node, "column");
    lv_obj_add_event_cb(container, yui_scope_delete_cb, LV_EVENT_DELETE, scope);
    esp_err_t err = yui_render_widget_list(component->widgets_node, schema, container, scope);
    if (err != ESP_OK) {
        lv_obj_del(container);
    }
    return err;
}

static esp_err_t yui_render_widget(const yml_node_t *node, yui_schema_runtime_t *schema, lv_obj_t *parent, yui_component_scope_t *scope)
{
    if (!node || !schema || !parent || yml_node_get_type(node) != YML_NODE_MAPPING) {
        return ESP_OK;
    }
    const char *type = yui_node_scalar(node, "type");
    if (!type) {
        return ESP_OK;
    }
    const yui_component_def_t *component = yui_schema_get_component(&schema->schema, type);
    if (component) {
        return yui_render_component_instance(component, node, schema, parent, scope);
    }
    if (strcmp(type, "label") == 0) {
        lv_obj_t *label = lv_label_create(parent);
        yui_apply_common_widget_attrs(label, node, schema);
        const char *text = yui_node_scalar(node, "text");
        yui_widget_runtime_t *runtime = yui_widget_runtime_create(label, scope);
        if (runtime && text) {
            (void)yui_widget_bind_text(runtime, text, label);
        } else if (text) {
            lv_label_set_text(label, text);
        }
        if (runtime) {
            (void)yui_widget_parse_events(node, runtime);
        }
        return ESP_OK;
    }
    if (strcmp(type, "img") == 0) {
        const char *src = yui_node_scalar(node, "src");
        if (!src) {
            yamui_log(YAMUI_LOG_LEVEL_WARN, YAMUI_LOG_CAT_LVGL, "Image widget missing src");
            return ESP_OK;
        }
        if (strncmp(src, "symbol:", 7) == 0) {
            const char *glyph = yui_symbol_lookup(src + 7);
            lv_obj_t *symbol = lv_label_create(parent);
            yui_apply_common_widget_attrs(symbol, node, schema);
            lv_label_set_text(symbol, glyph ? glyph : "");
            return ESP_OK;
        }
        lv_obj_t *img = lv_img_create(parent);
        yui_apply_common_widget_attrs(img, node, schema);
        lv_img_set_src(img, src);
        return ESP_OK;
    }
    if (strcmp(type, "button") == 0) {
        lv_obj_t *btn = lv_btn_create(parent);
        lv_obj_set_width(btn, LV_PCT(100));
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        yui_apply_common_widget_attrs(btn, node, schema);
        lv_obj_t *label = lv_label_create(btn);
        lv_obj_center(label);
        const char *text = yui_node_scalar(node, "text");
        yui_widget_runtime_t *runtime = yui_widget_runtime_create(btn, scope);
        if (runtime) {
            runtime->text_target = label;
            if (text) {
                (void)yui_widget_bind_text(runtime, text, label);
            }
            (void)yui_widget_parse_events(node, runtime);
        } else if (text) {
            lv_label_set_text(label, text);
        }
        return ESP_OK;
    }
    if (strcmp(type, "spacer") == 0) {
        lv_obj_t *spacer = lv_obj_create(parent);
        lv_obj_remove_style_all(spacer);
        lv_obj_set_height(spacer, yui_node_i32(node, "size", 12));
        lv_obj_set_width(spacer, LV_PCT(100));
        lv_obj_clear_flag(spacer, LV_OBJ_FLAG_CLICKABLE);
        yui_apply_common_widget_attrs(spacer, node, schema);
        return ESP_OK;
    }
    if (strcmp(type, "row") == 0 || strcmp(type, "column") == 0) {
        lv_obj_t *container = lv_obj_create(parent);
        lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
        /* Size to fit content by default */
        lv_obj_set_size(container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        yui_apply_layout(container, yml_node_get_child(node, "layout"), type);
        yui_apply_common_widget_attrs(container, node, schema);
        return yui_render_widget_list(yml_node_get_child(node, "widgets"), schema, container, scope);
    }
    if (strcmp(type, "panel") == 0) {
        lv_obj_t *panel = lv_obj_create(parent);
        lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
        yui_apply_common_widget_attrs(panel, node, schema);
        const yml_node_t *layout = yml_node_get_child(node, "layout");
        if (layout) {
            yui_apply_layout(panel, layout, "column");
        }
        return yui_render_widget_list(yml_node_get_child(node, "widgets"), schema, panel, scope);
    }
    yamui_log(YAMUI_LOG_LEVEL_WARN, YAMUI_LOG_CAT_LVGL, "Unsupported widget type '%s'", type);
    return ESP_OK;
}

static esp_err_t yui_render_screen(const yml_node_t *screen_node, yui_schema_runtime_t *schema)
{
    if (!screen_node || !schema) {
        return ESP_ERR_INVALID_ARG;
    }
    lv_obj_t *root = lv_scr_act();
    if (!root) {
        return ESP_FAIL;
    }
    kc_touch_display_reset_ui_state();
    yui_modal_close_all();
    lv_obj_clean(root);

    lv_obj_t *container = lv_obj_create(root);
    lv_obj_set_size(container, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    yui_apply_layout(container, yml_node_get_child(screen_node, "layout"), "column");

    const yml_node_t *widgets = yml_node_get_child(screen_node, "widgets");
    esp_err_t err = yui_render_widget_list(widgets, schema, container, NULL);
    if (err != ESP_OK) {
        return err;
    }

    const yml_node_t *on_load = yml_node_get_child(screen_node, "on_load");
    if (on_load) {
        yui_action_list_t list = {0};
        if (yui_action_list_from_node(on_load, &list) == ESP_OK && list.count > 0U) {
            yui_action_eval_ctx_t eval_ctx = {
                .resolver = yui_event_symbol_resolver,
                .resolver_ctx = NULL,
            };
            (void)yui_action_list_execute(&list, &eval_ctx);
        }
        yui_action_list_free(&list);
    }

    return ESP_OK;
}

static void yui_screen_frame_destroy(yui_screen_frame_t *frame)
{
    if (!frame) {
        return;
    }
    free(frame->screen_name);
    frame->screen_name = NULL;
    frame->schema = NULL;
}

static void yui_schema_runtime_destroy(yui_schema_runtime_t *schema)
{
    if (!schema) {
        return;
    }
    free(schema->name);
    if (schema->root) {
        yml_node_free(schema->root);
    }
    yui_schema_free(&schema->schema);
    free(schema);
}

static yui_schema_runtime_t *yui_schema_runtime_load(const char *name)
{
    if (s_loaded_schema && s_loaded_schema->name && name && strcasecmp(s_loaded_schema->name, name) == 0) {
        return s_loaded_schema;
    }
    size_t blob_size = 0;
    const uint8_t *blob = ui_schemas_get_named(name, &blob_size);
    if (!blob || blob_size == 0U) {
        return NULL;
    }
    yml_node_t *root = NULL;
    if (yaml_core_parse_buffer((const char *)blob, blob_size, &root) != ESP_OK) {
        return NULL;
    }
    yui_schema_t schema = {0};
    if (yui_schema_from_tree(root, &schema) != ESP_OK) {
        yml_node_free(root);
        return NULL;
    }
    yui_schema_runtime_t *runtime = (yui_schema_runtime_t *)calloc(1, sizeof(yui_schema_runtime_t));
    if (!runtime) {
        yui_schema_free(&schema);
        yml_node_free(root);
        return NULL;
    }
    runtime->name = yui_strdup_local(name);
    runtime->root = root;
    runtime->schema = schema;
    if (s_loaded_schema) {
        yui_schema_runtime_destroy(s_loaded_schema);
    }
    s_loaded_schema = runtime;
    return runtime;
}

static const yml_node_t *yui_schema_resolve_screen(yui_schema_runtime_t *schema, const char *screen)
{
    if (!schema) {
        return NULL;
    }
    const char *target = screen;
    if (!target || target[0] == '\0') {
        target = yui_schema_default_screen(&schema->schema);
    }
    return yui_schema_get_screen(&schema->schema, target);
}

static esp_err_t yui_navigation_ensure_capacity(size_t desired)
{
    if (desired <= s_nav_capacity) {
        return ESP_OK;
    }
    size_t new_capacity = s_nav_capacity == 0 ? 4 : s_nav_capacity * 2;
    while (new_capacity < desired) {
        new_capacity *= 2;
    }
    yui_screen_frame_t *next = (yui_screen_frame_t *)realloc(s_nav_stack, new_capacity * sizeof(yui_screen_frame_t));
    if (!next) {
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = s_nav_capacity; i < new_capacity; ++i) {
        memset(&next[i], 0, sizeof(yui_screen_frame_t));
    }
    s_nav_stack = next;
    s_nav_capacity = new_capacity;
    return ESP_OK;
}

static esp_err_t yui_navigation_render_current(void)
{
    if (s_nav_count == 0U) {
        return ESP_ERR_INVALID_STATE;
    }
    yui_screen_frame_t *frame = &s_nav_stack[s_nav_count - 1U];
    const yml_node_t *screen_node = yui_schema_resolve_screen(frame->schema, frame->screen_name);
    if (!screen_node) {
        return ESP_ERR_NOT_FOUND;
    }
    esp_err_t begin_err = yui_nav_queue_begin_render();
    if (begin_err != ESP_OK) {
        return begin_err;
    }
    esp_err_t err = yui_render_screen(screen_node, frame->schema);
    yui_nav_queue_end_render(err == ESP_OK);
    return err;
}

static esp_err_t yui_navigation_push(const char *screen)
{
    if (!s_loaded_schema) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = yui_navigation_ensure_capacity(s_nav_count + 1U);
    if (err != ESP_OK) {
        return err;
    }
    yui_screen_frame_t frame = {
        .schema = s_loaded_schema,
        .screen_name = yui_strdup_local(screen ? screen : yui_schema_default_screen(&s_loaded_schema->schema)),
    };
    s_nav_stack[s_nav_count++] = frame;
    return yui_navigation_render_current();
}

static esp_err_t yui_navigation_replace_top(const char *screen)
{
    if (s_nav_count == 0U) {
        return yui_navigation_push(screen);
    }
    yui_screen_frame_t *frame = &s_nav_stack[s_nav_count - 1U];
    free(frame->screen_name);
    frame->screen_name = yui_strdup_local(screen);
    return yui_navigation_render_current();
}

static esp_err_t yui_navigation_pop_internal(void)
{
    if (s_nav_count <= 1U) {
        return ESP_ERR_INVALID_STATE;
    }
    yui_screen_frame_destroy(&s_nav_stack[--s_nav_count]);
    return yui_navigation_render_current();
}

static esp_err_t yui_runtime_goto_screen(const char *screen)
{
    return yui_nav_queue_submit(YUI_NAV_REQUEST_GOTO, screen);
}

static esp_err_t yui_runtime_push_screen(const char *screen)
{
    return yui_nav_queue_submit(YUI_NAV_REQUEST_PUSH, screen);
}

static esp_err_t yui_runtime_pop_screen(void)
{
    return yui_nav_queue_submit(YUI_NAV_REQUEST_POP, NULL);
}

static esp_err_t yui_runtime_show_modal(const char *component)
{
    return yui_nav_queue_submit(YUI_NAV_REQUEST_SHOW_MODAL, component);
}

static esp_err_t yui_runtime_close_modal(void)
{
    return yui_nav_queue_submit(YUI_NAV_REQUEST_CLOSE_MODAL, NULL);
}

static esp_err_t yui_runtime_call_native(const char *function, const char **args, size_t arg_count)
{
    return yamui_runtime_call_function(function, args, arg_count);
}

static esp_err_t yui_runtime_emit_event(const char *event, const char **args, size_t arg_count)
{
    return yamui_runtime_emit_event(event, args, arg_count);
}

static esp_err_t yui_navigation_execute_request(yui_nav_request_type_t type, const char *arg, void *ctx)
{
    (void)ctx;
    switch (type) {
        case YUI_NAV_REQUEST_GOTO:
            return yui_navigation_replace_top(arg);
        case YUI_NAV_REQUEST_PUSH:
            return yui_navigation_push(arg);
        case YUI_NAV_REQUEST_POP:
            return yui_navigation_pop_internal();
        case YUI_NAV_REQUEST_SHOW_MODAL:
            return yui_modal_show_component(arg);
        case YUI_NAV_REQUEST_CLOSE_MODAL:
            return yui_modal_close_top();
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

static void yui_native_fn_goto(int argc, const char **argv)
{
    if (argc > 0 && argv) {
        (void)yui_runtime_goto_screen(argv[0]);
    }
}

static void yui_native_fn_push(int argc, const char **argv)
{
    if (argc > 0 && argv) {
        (void)yui_runtime_push_screen(argv[0]);
    }
}

static void yui_native_fn_pop(int argc, const char **argv)
{
    (void)argc;
    (void)argv;
    (void)yui_runtime_pop_screen();
}

static void yui_register_builtin_natives(void)
{
    yamui_runtime_register_function("ui_goto", yui_native_fn_goto);
    yamui_runtime_register_function("ui_push", yui_native_fn_push);
    yamui_runtime_register_function("ui_pop", yui_native_fn_pop);
}

esp_err_t lvgl_yaml_gui_load_default(void)
{
    esp_err_t runtime_err = yamui_runtime_init();
    if (runtime_err != ESP_OK) {
        return runtime_err;
    }
    yui_register_builtin_natives();
    yui_nav_queue_init(yui_navigation_execute_request, NULL);
    yui_events_set_runtime(&s_runtime_vtable);

    const char *default_schema = ui_schemas_get_default_name();
    yui_schema_runtime_t *schema = yui_schema_runtime_load(default_schema);
    if (!schema) {
        return ESP_ERR_NOT_FOUND;
    }
    const char *initial_screen = yui_schema_default_screen(&schema->schema);
    esp_err_t err = yui_navigation_push(initial_screen);
    if (err != ESP_OK) {
        yamui_log(YAMUI_LOG_LEVEL_ERROR, YAMUI_LOG_CAT_NAV, "Failed to load initial screen (%s)", esp_err_to_name(err));
    }
    return err;
}
