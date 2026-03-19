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
typedef enum {
    YUI_VALUE_BIND_NONE = 0,
    YUI_VALUE_BIND_TEXTAREA,
    YUI_VALUE_BIND_SWITCH,
    YUI_VALUE_BIND_SLIDER,
    YUI_VALUE_BIND_BAR,
    YUI_VALUE_BIND_ARC,
    YUI_VALUE_BIND_DROPDOWN,
} yui_value_bind_kind_t;

typedef struct {
    char *name;
    yml_node_t *root;
    yui_schema_t schema;
} yui_schema_runtime_t;

typedef struct {
    yui_schema_runtime_t *schema;
    char *screen_name;
} yui_screen_frame_t;

typedef struct {
    char *id;
    lv_obj_t *obj;
} yui_widget_ref_t;

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
    lv_obj_t *value_target;
    char *value_template;
    lv_obj_t *condition_target;
    char *visible_expr;
    char *enabled_expr;
    yui_value_bind_kind_t value_kind;
    char **bindings;
    size_t binding_count;
    yui_state_watch_handle_t *watch_handles;
    size_t watch_count;
    yui_component_scope_t *scope;
    yui_widget_events_t events;
    bool disposed;
    bool condition_refresh_pending;
    bool condition_refreshing;
    bool has_visible_state;
    bool last_visible;
    bool has_enabled_state;
    bool last_enabled;
};

static void yui_widget_refresh_text(yui_widget_runtime_t *runtime);
static void yui_widget_refresh_value(yui_widget_runtime_t *runtime);
static void yui_widget_refresh_conditions(yui_widget_runtime_t *runtime);
static void yui_widget_schedule_condition_refresh(yui_widget_runtime_t *runtime);
static bool yui_expr_value_is_truthy(const yui_expr_value_t *value);
static void yui_format_text(const char *tmpl, yui_component_scope_t *scope, char *out, size_t out_len);
static char *yui_strdup_local(const char *value);
static esp_err_t yui_collect_bindings_from_text(const char *text, char ***out_tokens, size_t *out_count);
static esp_err_t yui_collect_bindings_from_expr(const char *expr, char ***out_tokens, size_t *out_count);
static void yui_apply_layout(lv_obj_t *obj, const yml_node_t *layout_node, const char *default_type);
static lv_flex_align_t yui_flex_align_from_string(const char *value, lv_flex_align_t def);
static esp_err_t yui_render_widget_list(const yml_node_t *widgets_node, yui_schema_runtime_t *schema, lv_obj_t *parent, yui_component_scope_t *scope);
static bool yui_dropdown_select_value(lv_obj_t *dropdown, const char *value);
static esp_err_t yui_widget_bind_conditions(yui_widget_runtime_t *runtime, const yml_node_t *node, lv_obj_t *target);

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
static yui_state_watch_handle_t s_display_brightness_watch;
typedef struct {
    lv_obj_t *overlay;
} yui_modal_frame_t;
static yui_modal_frame_t *s_modal_stack;
static size_t s_modal_count;
static size_t s_modal_capacity;
static yui_widget_ref_t *s_widget_refs;
static size_t s_widget_ref_count;
static size_t s_widget_ref_capacity;

static void yui_widget_refs_clear(void)
{
    if (s_widget_refs) {
        for (size_t i = 0; i < s_widget_ref_count; ++i) {
            free(s_widget_refs[i].id);
            s_widget_refs[i].id = NULL;
            s_widget_refs[i].obj = NULL;
        }
        free(s_widget_refs);
    }
    s_widget_refs = NULL;
    s_widget_ref_count = 0U;
    s_widget_ref_capacity = 0U;
}

static esp_err_t yui_widget_refs_ensure_capacity(size_t desired)
{
    if (desired <= s_widget_ref_capacity) {
        return ESP_OK;
    }
    size_t new_capacity = s_widget_ref_capacity == 0U ? 8U : s_widget_ref_capacity * 2U;
    while (new_capacity < desired) {
        new_capacity *= 2U;
    }
    yui_widget_ref_t *next = (yui_widget_ref_t *)realloc(s_widget_refs, new_capacity * sizeof(yui_widget_ref_t));
    if (!next) {
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = s_widget_ref_capacity; i < new_capacity; ++i) {
        next[i].id = NULL;
        next[i].obj = NULL;
    }
    s_widget_refs = next;
    s_widget_ref_capacity = new_capacity;
    return ESP_OK;
}

static esp_err_t yui_widget_ref_register(const char *id, lv_obj_t *obj)
{
    if (!id || id[0] == '\0' || !obj) {
        return ESP_OK;
    }
    for (size_t i = 0; i < s_widget_ref_count; ++i) {
        if (s_widget_refs[i].id && strcmp(s_widget_refs[i].id, id) == 0) {
            s_widget_refs[i].obj = obj;
            return ESP_OK;
        }
    }
    esp_err_t err = yui_widget_refs_ensure_capacity(s_widget_ref_count + 1U);
    if (err != ESP_OK) {
        return err;
    }
    s_widget_refs[s_widget_ref_count].id = yui_strdup_local(id);
    if (!s_widget_refs[s_widget_ref_count].id) {
        return ESP_ERR_NO_MEM;
    }
    s_widget_refs[s_widget_ref_count].obj = obj;
    s_widget_ref_count++;
    return ESP_OK;
}

static lv_obj_t *yui_widget_ref_find(const char *id)
{
    if (!id || id[0] == '\0') {
        return NULL;
    }
    for (size_t i = 0; i < s_widget_ref_count; ++i) {
        if (s_widget_refs[i].id && strcmp(s_widget_refs[i].id, id) == 0) {
            return s_widget_refs[i].obj;
        }
    }
    return NULL;
}

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
            lv_obj_delete_async(frame->overlay);
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
        lv_obj_delete_async(frame->overlay);
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
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *panel = lv_obj_create(overlay);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x25293C), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(panel, 18, 0);
    lv_obj_set_style_radius(panel, 16, 0);
    
    /* Keep modal sizing simple to avoid layout recursion in the LVGL task. */
    lv_obj_set_width(panel, 420);
    lv_obj_set_height(panel, LV_SIZE_CONTENT);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    
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

static void yui_apply_display_brightness_value(const char *value)
{
    int percent = 100;
    if (value && value[0] != '\0') {
        percent = (int)strtol(value, NULL, 10);
    }
    if (percent < 0) {
        percent = 0;
    } else if (percent > 100) {
        percent = 100;
    }
    (void)kc_touch_display_brightness_set(percent);
}

static void yui_display_brightness_watch_cb(const char *key, const char *value, void *user_ctx)
{
    (void)key;
    (void)user_ctx;
    yui_apply_display_brightness_value(value);
}

static esp_err_t yui_register_display_watchers(void)
{
    if (s_display_brightness_watch != 0U) {
        return ESP_OK;
    }
    return yui_state_watch("display.brightness", yui_display_brightness_watch_cb, NULL, &s_display_brightness_watch);
}

static void yui_sync_display_brightness_from_state(void)
{
    yui_apply_display_brightness_value(yui_state_get("display.brightness", "100"));
}

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

static char *yui_node_join_sequence_scalar(const yml_node_t *node, const char *key)
{
    if (!node || !key) {
        return NULL;
    }
    const yml_node_t *child = yml_node_get_child(node, key);
    if (!child || yml_node_get_type(child) != YML_NODE_SEQUENCE) {
        return NULL;
    }
    size_t item_count = yml_node_child_count(child);
    if (item_count == 0U) {
        return yui_strdup_local("");
    }
    size_t total_len = 1U;
    for (size_t i = 0; i < item_count; ++i) {
        const yml_node_t *item = yml_node_child_at(child, i);
        const char *scalar = item ? yml_node_get_scalar(item) : NULL;
        if (scalar) {
            total_len += strlen(scalar);
        }
        if (i + 1U < item_count) {
            total_len += 1U;
        }
    }
    char *joined = (char *)malloc(total_len);
    if (!joined) {
        return NULL;
    }
    size_t offset = 0U;
    for (size_t i = 0; i < item_count; ++i) {
        const yml_node_t *item = yml_node_child_at(child, i);
        const char *scalar = item ? yml_node_get_scalar(item) : NULL;
        if (scalar) {
            size_t len = strlen(scalar);
            memcpy(joined + offset, scalar, len);
            offset += len;
        }
        if (i + 1U < item_count) {
            joined[offset++] = '\n';
        }
    }
    joined[offset] = '\0';
    return joined;
}

static int32_t yui_node_i32(const yml_node_t *node, const char *key, int32_t def)
{
    const char *scalar = yui_node_scalar(node, key);
    return scalar ? atoi(scalar) : def;
}

static bool yui_parse_bool(const char *value, bool def)
{
    if (!value) {
        return def;
    }
    if (strcasecmp(value, "true") == 0 || strcmp(value, "1") == 0 || strcasecmp(value, "yes") == 0 || strcasecmp(value, "on") == 0) {
        return true;
    }
    if (strcasecmp(value, "false") == 0 || strcmp(value, "0") == 0 || strcasecmp(value, "no") == 0 || strcasecmp(value, "off") == 0) {
        return false;
    }
    return def;
}

static const char *yui_node_resolved_scalar(const yml_node_t *node, const char *key, yui_component_scope_t *scope, char *buffer, size_t buffer_len)
{
    const char *raw = yui_node_scalar(node, key);
    if (!raw) {
        return NULL;
    }
    if (!buffer || buffer_len == 0U) {
        return raw;
    }
    yui_format_text(raw, scope, buffer, buffer_len);
    return buffer;
}

static int32_t yui_node_resolved_i32(const yml_node_t *node, const char *key, yui_component_scope_t *scope, int32_t def)
{
    char buffer[32];
    const char *value = yui_node_resolved_scalar(node, key, scope, buffer, sizeof(buffer));
    if (!value || value[0] == '\0') {
        return def;
    }
    return atoi(value);
}

static bool yui_node_resolved_bool(const yml_node_t *node, const char *key, yui_component_scope_t *scope, bool def)
{
    char buffer[32];
    const char *value = yui_node_resolved_scalar(node, key, scope, buffer, sizeof(buffer));
    return yui_parse_bool(value, def);
}

static void yui_widget_runtime_dispose(yui_widget_runtime_t *runtime)
{
    if (!runtime) {
        return;
    }
    if (runtime->disposed) {
        return;
    }
    runtime->disposed = true;
    runtime->event_target = NULL;
    runtime->text_target = NULL;
    runtime->value_target = NULL;
    runtime->value_kind = YUI_VALUE_BIND_NONE;
    if (runtime->watch_handles) {
        for (size_t i = 0; i < runtime->watch_count; ++i) {
            if (runtime->watch_handles[i] != 0U) {
                yui_state_unwatch(runtime->watch_handles[i]);
            }
        }
        free(runtime->watch_handles);
        runtime->watch_handles = NULL;
        runtime->watch_count = 0U;
    }
    if (runtime->bindings) {
        for (size_t i = 0; i < runtime->binding_count; ++i) {
            free(runtime->bindings[i]);
        }
        free(runtime->bindings);
        runtime->bindings = NULL;
        runtime->binding_count = 0U;
    }
    for (size_t i = 0; i < YUI_WIDGET_EVENT_COUNT; ++i) {
        yui_action_list_free(&runtime->events.lists[i]);
    }
    if (runtime->scope) {
        yui_scope_release(runtime->scope);
        runtime->scope = NULL;
    }
    free(runtime->text_template);
    runtime->text_template = NULL;
    free(runtime->value_template);
    runtime->value_template = NULL;
    free(runtime->visible_expr);
    runtime->visible_expr = NULL;
    free(runtime->enabled_expr);
    runtime->enabled_expr = NULL;
}

static void yui_widget_event_cb(lv_event_t *event)
{
    yui_widget_runtime_t *runtime = (yui_widget_runtime_t *)lv_event_get_user_data(event);
    if (!runtime) {
        return;
    }
    if (lv_event_get_code(event) == LV_EVENT_DELETE) {
        yui_widget_runtime_dispose(runtime);
        return;
    }
    if (runtime->disposed) {
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
    if (!runtime || runtime->disposed || !runtime->text_target || !runtime->text_template) {
        return;
    }
    char buffer[YUI_TEXT_BUFFER_MAX];
    yui_format_text(runtime->text_template, runtime->scope, buffer, sizeof(buffer));
    lv_label_set_text(runtime->text_target, buffer);
}

static bool yui_dropdown_select_value(lv_obj_t *dropdown, const char *value)
{
#if LV_USE_DROPDOWN
    if (!dropdown || !value || value[0] == '\0') {
        return false;
    }
    int32_t by_name = lv_dropdown_get_option_index(dropdown, value);
    if (by_name >= 0) {
        lv_dropdown_set_selected(dropdown, (uint32_t)by_name);
        return true;
    }
    uint32_t option_count = lv_dropdown_get_option_count(dropdown);
    int idx = atoi(value);
    if (idx >= 0 && idx < (int)option_count) {
        lv_dropdown_set_selected(dropdown, (uint32_t)idx);
        return true;
    }
#else
    (void)dropdown;
    (void)value;
#endif
    return false;
}

static void yui_widget_refresh_value(yui_widget_runtime_t *runtime)
{
    if (!runtime || runtime->disposed || !runtime->value_target || !runtime->value_template || runtime->value_kind == YUI_VALUE_BIND_NONE) {
        return;
    }
    char buffer[YUI_TEXT_BUFFER_MAX];
    yui_format_text(runtime->value_template, runtime->scope, buffer, sizeof(buffer));

    switch (runtime->value_kind) {
        case YUI_VALUE_BIND_TEXTAREA:
#if LV_USE_TEXTAREA
            lv_textarea_set_text(runtime->value_target, buffer);
#endif
            break;
        case YUI_VALUE_BIND_SWITCH: {
            bool checked = yui_parse_bool(buffer, false);
            if (checked) {
                lv_obj_add_state(runtime->value_target, LV_STATE_CHECKED);
            } else {
                lv_obj_remove_state(runtime->value_target, LV_STATE_CHECKED);
            }
            break;
        }
        case YUI_VALUE_BIND_SLIDER:
#if LV_USE_SLIDER
            lv_slider_set_value(runtime->value_target, atoi(buffer), LV_ANIM_OFF);
#endif
            break;
        case YUI_VALUE_BIND_BAR:
#if LV_USE_BAR
            lv_bar_set_value(runtime->value_target, atoi(buffer), LV_ANIM_OFF);
#endif
            break;
        case YUI_VALUE_BIND_ARC:
#if LV_USE_ARC
            lv_arc_set_value(runtime->value_target, atoi(buffer));
#endif
            break;
        case YUI_VALUE_BIND_DROPDOWN:
#if LV_USE_DROPDOWN
            (void)yui_dropdown_select_value(runtime->value_target, buffer);
#endif
            break;
        case YUI_VALUE_BIND_NONE:
        default:
            break;
    }
}

static bool yui_expr_value_is_truthy(const yui_expr_value_t *value)
{
    if (!value) {
        return false;
    }
    switch (value->type) {
        case YUI_EXPR_VALUE_BOOL:
            return value->boolean;
        case YUI_EXPR_VALUE_NUMBER:
            return value->number != 0.0;
        case YUI_EXPR_VALUE_STRING:
            if (!value->string || value->string[0] == '\0') {
                return false;
            }
            if (strcasecmp(value->string, "false") == 0 || strcmp(value->string, "0") == 0 || strcasecmp(value->string, "off") == 0 || strcasecmp(value->string, "no") == 0) {
                return false;
            }
            return true;
        case YUI_EXPR_VALUE_NULL:
        default:
            return false;
    }
}

static void yui_widget_refresh_conditions_async_cb(void *user_data)
{
    yui_widget_runtime_t *runtime = (yui_widget_runtime_t *)user_data;
    if (!runtime || runtime->disposed) {
        return;
    }
    runtime->condition_refresh_pending = false;
    yui_widget_refresh_conditions(runtime);
}

static void yui_widget_schedule_condition_refresh(yui_widget_runtime_t *runtime)
{
    if (!runtime || runtime->disposed || !runtime->condition_target) {
        return;
    }
    if (runtime->condition_refresh_pending) {
        return;
    }
    runtime->condition_refresh_pending = true;
    if (lv_async_call(yui_widget_refresh_conditions_async_cb, runtime) != LV_RESULT_OK) {
        runtime->condition_refresh_pending = false;
        yui_widget_refresh_conditions(runtime);
    }
}

static void yui_widget_refresh_conditions(yui_widget_runtime_t *runtime)
{
    if (!runtime || runtime->disposed || !runtime->condition_target || runtime->condition_refreshing) {
        return;
    }
    runtime->condition_refreshing = true;
    yui_expression_ctx_t ctx = {
        .scope = runtime->scope,
    };
    if (runtime->visible_expr) {
        yui_expr_value_t value = {0};
        if (yui_expr_eval(runtime->visible_expr, yui_expression_symbol_resolver, &ctx, &value) == ESP_OK) {
            bool visible = yui_expr_value_is_truthy(&value);
            if (!runtime->has_visible_state || runtime->last_visible != visible) {
                runtime->last_visible = visible;
                runtime->has_visible_state = true;
                if (visible) {
                    lv_obj_clear_flag(runtime->condition_target, LV_OBJ_FLAG_HIDDEN);
                } else {
                    lv_obj_add_flag(runtime->condition_target, LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
        yui_expr_value_reset(&value);
    }
    if (runtime->enabled_expr) {
        yui_expr_value_t value = {0};
        if (yui_expr_eval(runtime->enabled_expr, yui_expression_symbol_resolver, &ctx, &value) == ESP_OK) {
            bool enabled = yui_expr_value_is_truthy(&value);
            if (!runtime->has_enabled_state || runtime->last_enabled != enabled) {
                runtime->last_enabled = enabled;
                runtime->has_enabled_state = true;
                if (enabled) {
                    lv_obj_remove_state(runtime->condition_target, LV_STATE_DISABLED);
                } else {
                    lv_obj_add_state(runtime->condition_target, LV_STATE_DISABLED);
                }
            }
        }
        yui_expr_value_reset(&value);
    }
    runtime->condition_refreshing = false;
}

static void yui_widget_state_cb(const char *key, const char *value, void *user_ctx)
{
    (void)key;
    (void)value;
    yui_widget_runtime_t *runtime = (yui_widget_runtime_t *)user_ctx;
    if (!runtime || runtime->disposed) {
        return;
    }
    yui_widget_refresh_text(runtime);
    yui_widget_refresh_value(runtime);
    yui_widget_schedule_condition_refresh(runtime);
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

typedef struct {
    char ***out_tokens;
    size_t *out_count;
} yui_expr_binding_ctx_t;

static void yui_collect_expr_identifier_cb(const char *identifier, void *ctx)
{
    if (!identifier || !ctx) {
        return;
    }
    yui_expr_binding_ctx_t *binding_ctx = (yui_expr_binding_ctx_t *)ctx;
    for (size_t i = 0; i < *binding_ctx->out_count; ++i) {
        if (strcmp((*binding_ctx->out_tokens)[i], identifier) == 0) {
            return;
        }
    }
    char *copy = yui_strdup_local(identifier);
    if (!copy) {
        return;
    }
    char **next = (char **)realloc(*binding_ctx->out_tokens, (*binding_ctx->out_count + 1U) * sizeof(char *));
    if (!next) {
        free(copy);
        return;
    }
    *binding_ctx->out_tokens = next;
    (*binding_ctx->out_tokens)[(*binding_ctx->out_count)++] = copy;
}

static esp_err_t yui_collect_bindings_from_expr(const char *expr, char ***out_tokens, size_t *out_count)
{
    if (!out_tokens || !out_count) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_tokens = NULL;
    *out_count = 0U;
    if (!expr || expr[0] == '\0') {
        return ESP_OK;
    }
    yui_expr_binding_ctx_t ctx = {
        .out_tokens = out_tokens,
        .out_count = out_count,
    };
    return yui_expr_collect_identifiers(expr, yui_collect_expr_identifier_cb, &ctx);
}

static esp_err_t yui_widget_bind_value(yui_widget_runtime_t *runtime, const char *value_tmpl, lv_obj_t *target, yui_value_bind_kind_t kind)
{
    if (!runtime || !value_tmpl || !target || kind == YUI_VALUE_BIND_NONE) {
        return ESP_OK;
    }
    runtime->value_target = target;
    runtime->value_kind = kind;
    runtime->value_template = yui_strdup_local(value_tmpl);
    if (!runtime->value_template) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = yui_collect_bindings_from_text(value_tmpl, &runtime->bindings, &runtime->binding_count);
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
    yui_widget_refresh_value(runtime);
    return ESP_OK;
}

static esp_err_t yui_widget_bind_conditions(yui_widget_runtime_t *runtime, const yml_node_t *node, lv_obj_t *target)
{
    if (!runtime || !node || !target) {
        return ESP_OK;
    }
    const char *visible_expr = yui_node_scalar(node, "visible_if");
    const char *enabled_expr = yui_node_scalar(node, "enabled_if");
    if ((!visible_expr || visible_expr[0] == '\0') && (!enabled_expr || enabled_expr[0] == '\0')) {
        return ESP_OK;
    }
    runtime->condition_target = target;
    if (visible_expr && visible_expr[0] != '\0') {
        runtime->visible_expr = yui_strdup_local(visible_expr);
        if (!runtime->visible_expr) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (enabled_expr && enabled_expr[0] != '\0') {
        runtime->enabled_expr = yui_strdup_local(enabled_expr);
        if (!runtime->enabled_expr) {
            return ESP_ERR_NO_MEM;
        }
    }
    char **tokens = NULL;
    size_t token_count = 0U;
    esp_err_t err = ESP_OK;
    if (runtime->visible_expr) {
        err = yui_collect_bindings_from_expr(runtime->visible_expr, &tokens, &token_count);
        if (err != ESP_OK) {
            return err;
        }
    }
    if (runtime->enabled_expr) {
        char **enabled_tokens = NULL;
        size_t enabled_count = 0U;
        err = yui_collect_bindings_from_expr(runtime->enabled_expr, &enabled_tokens, &enabled_count);
        if (err != ESP_OK) {
            if (tokens) {
                for (size_t i = 0; i < token_count; ++i) free(tokens[i]);
                free(tokens);
            }
            return err;
        }
        for (size_t i = 0; i < enabled_count; ++i) {
            bool exists = false;
            for (size_t j = 0; j < token_count; ++j) {
                if (strcmp(tokens[j], enabled_tokens[i]) == 0) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                char **next = (char **)realloc(tokens, (token_count + 1U) * sizeof(char *));
                if (!next) {
                    err = ESP_ERR_NO_MEM;
                    break;
                }
                tokens = next;
                tokens[token_count++] = enabled_tokens[i];
                enabled_tokens[i] = NULL;
            }
        }
        for (size_t i = 0; i < enabled_count; ++i) {
            free(enabled_tokens[i]);
        }
        free(enabled_tokens);
        if (err != ESP_OK) {
            for (size_t i = 0; i < token_count; ++i) free(tokens[i]);
            free(tokens);
            return err;
        }
    }
    for (size_t i = 0; i < token_count; ++i) {
        err = yui_widget_watch_state(runtime, tokens[i]);
        free(tokens[i]);
        if (err != ESP_OK) {
            free(tokens);
            return err;
        }
    }
    free(tokens);
    yui_widget_schedule_condition_refresh(runtime);
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

static bool yui_node_has_child(const yml_node_t *node, const char *key)
{
    if (!node || !key) {
        return false;
    }
    return yml_node_get_child(node, key) != NULL;
}

static void yui_register_widget_id(const yml_node_t *node, lv_obj_t *obj)
{
    if (!node || !obj) {
        return;
    }
    const char *id = yui_node_scalar(node, "id");
    if (!id || id[0] == '\0') {
        return;
    }
    esp_err_t err = yui_widget_ref_register(id, obj);
    if (err != ESP_OK) {
        yamui_log(YAMUI_LOG_LEVEL_WARN, YAMUI_LOG_CAT_LVGL, "Failed to register widget id '%s' (%s)", id, esp_err_to_name(err));
    }
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
        yui_register_widget_id(node, label);
        yui_apply_common_widget_attrs(label, node, schema);
        const char *text = yui_node_scalar(node, "text");
        yui_widget_runtime_t *runtime = yui_widget_runtime_create(label, scope);
        if (runtime && text) {
            (void)yui_widget_bind_text(runtime, text, label);
        } else if (text) {
            lv_label_set_text(label, text);
        }
        if (runtime) {
            (void)yui_widget_bind_conditions(runtime, node, label);
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
            yui_widget_runtime_t *runtime = yui_widget_runtime_create(symbol, scope);
            if (runtime) {
                (void)yui_widget_bind_conditions(runtime, node, symbol);
            }
            return ESP_OK;
        }
        lv_obj_t *img = lv_img_create(parent);
        yui_register_widget_id(node, img);
        yui_apply_common_widget_attrs(img, node, schema);
        lv_img_set_src(img, src);
        yui_widget_runtime_t *runtime = yui_widget_runtime_create(img, scope);
        if (runtime) {
            (void)yui_widget_bind_conditions(runtime, node, img);
        }
        return ESP_OK;
    }
    if (strcmp(type, "button") == 0) {
        const char *text = yui_node_scalar(node, "text");
        lv_obj_t *btn = lv_button_create(parent);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        yui_register_widget_id(node, btn);
        bool text_is_dynamic = text && strstr(text, "{{") && strstr(text, "}}");
        if (!yui_node_has_child(node, "width")) {
            lv_flex_flow_t parent_flow = lv_obj_get_style_flex_flow(parent, LV_PART_MAIN);
            if (parent_flow == LV_FLEX_FLOW_COLUMN || parent_flow == LV_FLEX_FLOW_COLUMN_WRAP) {
                lv_obj_set_width(btn, LV_PCT(100));
            } else {
                lv_obj_set_width(btn, LV_SIZE_CONTENT);
            }
        }
        yui_apply_common_widget_attrs(btn, node, schema);
        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        lv_obj_center(label);
        if (text && !text_is_dynamic) {
            lv_label_set_text(label, text);
        }
        yui_widget_runtime_t *runtime = yui_widget_runtime_create(btn, scope);
        if (runtime) {
            runtime->text_target = label;
            if (text && text_is_dynamic) {
                (void)yui_widget_bind_text(runtime, text, label);
            }
            (void)yui_widget_bind_conditions(runtime, node, btn);
            (void)yui_widget_parse_events(node, runtime);
        } else if (text) {
            lv_label_set_text(label, text);
        }
        return ESP_OK;
    }
    if (strcmp(type, "spacer") == 0) {
        lv_obj_t *spacer = lv_obj_create(parent);
        yui_register_widget_id(node, spacer);
        lv_obj_remove_style_all(spacer);
        lv_obj_set_height(spacer, yui_node_i32(node, "size", 12));
        lv_obj_set_width(spacer, LV_PCT(100));
        lv_obj_clear_flag(spacer, LV_OBJ_FLAG_CLICKABLE);
        yui_apply_common_widget_attrs(spacer, node, schema);
        yui_widget_runtime_t *runtime = yui_widget_runtime_create(spacer, scope);
        if (runtime) {
            (void)yui_widget_bind_conditions(runtime, node, spacer);
        }
        return ESP_OK;
    }
    if (strcmp(type, "textarea") == 0) {
#if LV_USE_TEXTAREA
        lv_obj_t *ta = lv_textarea_create(parent);
        yui_register_widget_id(node, ta);
        yui_apply_common_widget_attrs(ta, node, schema);
        const char *placeholder = yui_node_scalar(node, "placeholder");
        if (placeholder) {
            lv_textarea_set_placeholder_text(ta, placeholder);
        }
        lv_textarea_set_password_mode(ta, yui_node_resolved_bool(node, "password_mode", scope, false));
        char text_buf[YUI_TEXT_BUFFER_MAX];
        const char *initial_text = yui_node_resolved_scalar(node, "text", scope, text_buf, sizeof(text_buf));
        if (!initial_text) {
            initial_text = yui_node_resolved_scalar(node, "value", scope, text_buf, sizeof(text_buf));
        }
        if (initial_text) {
            lv_textarea_set_text(ta, initial_text);
        }
        yui_widget_runtime_t *runtime = yui_widget_runtime_create(ta, scope);
        if (runtime) {
            const char *value_tmpl = yui_node_scalar(node, "text");
            if (!value_tmpl) {
                value_tmpl = yui_node_scalar(node, "value");
            }
            if (value_tmpl) {
                (void)yui_widget_bind_value(runtime, value_tmpl, ta, YUI_VALUE_BIND_TEXTAREA);
            }
            (void)yui_widget_bind_conditions(runtime, node, ta);
            (void)yui_widget_parse_events(node, runtime);
        }
        return ESP_OK;
#else
        yamui_log(YAMUI_LOG_LEVEL_WARN, YAMUI_LOG_CAT_LVGL, "Widget type 'textarea' unavailable: LV_USE_TEXTAREA=0");
        return ESP_OK;
#endif
    }
    if (strcmp(type, "switch") == 0) {
#if LV_USE_SWITCH
        lv_obj_t *sw = lv_switch_create(parent);
        yui_register_widget_id(node, sw);
        yui_apply_common_widget_attrs(sw, node, schema);
        if (yui_node_resolved_bool(node, "value", scope, false)) {
            lv_obj_add_state(sw, LV_STATE_CHECKED);
        }
        yui_widget_runtime_t *runtime = yui_widget_runtime_create(sw, scope);
        if (runtime) {
            const char *value_tmpl = yui_node_scalar(node, "value");
            if (value_tmpl) {
                (void)yui_widget_bind_value(runtime, value_tmpl, sw, YUI_VALUE_BIND_SWITCH);
            }
            (void)yui_widget_bind_conditions(runtime, node, sw);
            (void)yui_widget_parse_events(node, runtime);
        }
        return ESP_OK;
#else
        yamui_log(YAMUI_LOG_LEVEL_WARN, YAMUI_LOG_CAT_LVGL, "Widget type 'switch' unavailable: LV_USE_SWITCH=0");
        return ESP_OK;
#endif
    }
    if (strcmp(type, "slider") == 0) {
#if LV_USE_SLIDER
        lv_obj_t *slider = lv_slider_create(parent);
        yui_register_widget_id(node, slider);
        yui_apply_common_widget_attrs(slider, node, schema);
        int32_t min = yui_node_resolved_i32(node, "min", scope, 0);
        int32_t max = yui_node_resolved_i32(node, "max", scope, 100);
        if (max < min) {
            int32_t tmp = min;
            min = max;
            max = tmp;
        }
        lv_slider_set_range(slider, min, max);
        int32_t value = yui_node_resolved_i32(node, "value", scope, min);
        if (value < min) {
            value = min;
        } else if (value > max) {
            value = max;
        }
        lv_slider_set_value(slider, value, LV_ANIM_OFF);
        yui_widget_runtime_t *runtime = yui_widget_runtime_create(slider, scope);
        if (runtime) {
            const char *value_tmpl = yui_node_scalar(node, "value");
            if (value_tmpl) {
                (void)yui_widget_bind_value(runtime, value_tmpl, slider, YUI_VALUE_BIND_SLIDER);
            }
            (void)yui_widget_bind_conditions(runtime, node, slider);
            (void)yui_widget_parse_events(node, runtime);
        }
        return ESP_OK;
#else
        yamui_log(YAMUI_LOG_LEVEL_WARN, YAMUI_LOG_CAT_LVGL, "Widget type 'slider' unavailable: LV_USE_SLIDER=0");
        return ESP_OK;
#endif
    }
    if (strcmp(type, "bar") == 0) {
#if LV_USE_BAR
        lv_obj_t *bar = lv_bar_create(parent);
        yui_register_widget_id(node, bar);
        yui_apply_common_widget_attrs(bar, node, schema);
        int32_t min = yui_node_resolved_i32(node, "min", scope, 0);
        int32_t max = yui_node_resolved_i32(node, "max", scope, 100);
        if (max < min) {
            int32_t tmp = min;
            min = max;
            max = tmp;
        }
        lv_bar_set_range(bar, min, max);
        int32_t value = yui_node_resolved_i32(node, "value", scope, min);
        if (value < min) {
            value = min;
        } else if (value > max) {
            value = max;
        }
        lv_bar_set_value(bar, value, LV_ANIM_OFF);
        yui_widget_runtime_t *runtime = yui_widget_runtime_create(bar, scope);
        if (runtime) {
            const char *value_tmpl = yui_node_scalar(node, "value");
            if (value_tmpl) {
                (void)yui_widget_bind_value(runtime, value_tmpl, bar, YUI_VALUE_BIND_BAR);
            }
            (void)yui_widget_bind_conditions(runtime, node, bar);
            (void)yui_widget_parse_events(node, runtime);
        }
        return ESP_OK;
#else
        yamui_log(YAMUI_LOG_LEVEL_WARN, YAMUI_LOG_CAT_LVGL, "Widget type 'bar' unavailable: LV_USE_BAR=0");
        return ESP_OK;
#endif
    }
    if (strcmp(type, "arc") == 0) {
#if LV_USE_ARC
        lv_obj_t *arc = lv_arc_create(parent);
        yui_register_widget_id(node, arc);
        yui_apply_common_widget_attrs(arc, node, schema);
        int32_t min = yui_node_resolved_i32(node, "min", scope, 0);
        int32_t max = yui_node_resolved_i32(node, "max", scope, 100);
        if (max < min) {
            int32_t tmp = min;
            min = max;
            max = tmp;
        }
        lv_arc_set_range(arc, min, max);
        int32_t value = yui_node_resolved_i32(node, "value", scope, min);
        if (value < min) {
            value = min;
        } else if (value > max) {
            value = max;
        }
        lv_arc_set_value(arc, value);
        yui_widget_runtime_t *runtime = yui_widget_runtime_create(arc, scope);
        if (runtime) {
            const char *value_tmpl = yui_node_scalar(node, "value");
            if (value_tmpl) {
                (void)yui_widget_bind_value(runtime, value_tmpl, arc, YUI_VALUE_BIND_ARC);
            }
            (void)yui_widget_bind_conditions(runtime, node, arc);
            (void)yui_widget_parse_events(node, runtime);
        }
        return ESP_OK;
#else
        yamui_log(YAMUI_LOG_LEVEL_WARN, YAMUI_LOG_CAT_LVGL, "Widget type 'arc' unavailable: LV_USE_ARC=0");
        return ESP_OK;
#endif
    }
    if (strcmp(type, "dropdown") == 0) {
#if LV_USE_DROPDOWN
        lv_obj_t *dd = lv_dropdown_create(parent);
        yui_register_widget_id(node, dd);
        yui_apply_common_widget_attrs(dd, node, schema);
        char *options_joined = yui_node_join_sequence_scalar(node, "options");
        const char *options = options_joined ? options_joined : yui_node_scalar(node, "options");
        if (options && options[0] != '\0') {
            lv_dropdown_set_options(dd, options);
        }
        char value_buf[YUI_TEXT_BUFFER_MAX];
        const char *value = yui_node_resolved_scalar(node, "value", scope, value_buf, sizeof(value_buf));
        if (value && value[0] != '\0') {
            (void)yui_dropdown_select_value(dd, value);
        }
        yui_widget_runtime_t *runtime = yui_widget_runtime_create(dd, scope);
        if (runtime) {
            const char *value_tmpl = yui_node_scalar(node, "value");
            if (value_tmpl) {
                (void)yui_widget_bind_value(runtime, value_tmpl, dd, YUI_VALUE_BIND_DROPDOWN);
            }
            (void)yui_widget_bind_conditions(runtime, node, dd);
            (void)yui_widget_parse_events(node, runtime);
        }
        free(options_joined);
        return ESP_OK;
#else
        yamui_log(YAMUI_LOG_LEVEL_WARN, YAMUI_LOG_CAT_LVGL, "Widget type 'dropdown' unavailable: LV_USE_DROPDOWN=0");
        return ESP_OK;
#endif
    }
    if (strcmp(type, "keyboard") == 0) {
#if LV_USE_KEYBOARD
        lv_obj_t *kb = lv_keyboard_create(parent);
        yui_register_widget_id(node, kb);
        yui_apply_common_widget_attrs(kb, node, schema);
        const char *target_id = yui_node_scalar(node, "target");
        if (target_id && target_id[0] != '\0') {
            lv_obj_t *target_obj = yui_widget_ref_find(target_id);
            if (target_obj) {
                lv_keyboard_set_textarea(kb, target_obj);
            } else {
                yamui_log(YAMUI_LOG_LEVEL_WARN, YAMUI_LOG_CAT_LVGL, "Keyboard target '%s' not found", target_id);
            }
        }
        const char *mode = yui_node_scalar(node, "mode");
        if (mode && mode[0] != '\0') {
            if (strcasecmp(mode, "number") == 0) {
                lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
            } else if (strcasecmp(mode, "text_lower") == 0) {
                lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
            } else if (strcasecmp(mode, "text_upper") == 0) {
                lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_UPPER);
            } else if (strcasecmp(mode, "special") == 0) {
                lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_SPECIAL);
            }
        }
        yui_widget_runtime_t *runtime = yui_widget_runtime_create(kb, scope);
        if (runtime) {
            (void)yui_widget_bind_conditions(runtime, node, kb);
            (void)yui_widget_parse_events(node, runtime);
        }
        return ESP_OK;
#else
        yamui_log(YAMUI_LOG_LEVEL_WARN, YAMUI_LOG_CAT_LVGL, "Widget type 'keyboard' unavailable: LV_USE_KEYBOARD=0");
        return ESP_OK;
#endif
    }
    if (strcmp(type, "row") == 0 || strcmp(type, "column") == 0) {
        lv_obj_t *container = lv_obj_create(parent);
        yui_register_widget_id(node, container);
        lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
        /* Size to fit content by default */
        lv_obj_set_size(container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        yui_apply_layout(container, yml_node_get_child(node, "layout"), type);
        yui_apply_common_widget_attrs(container, node, schema);
        yui_widget_runtime_t *runtime = yui_widget_runtime_create(container, scope);
        if (runtime) {
            (void)yui_widget_bind_conditions(runtime, node, container);
        }
        return yui_render_widget_list(yml_node_get_child(node, "widgets"), schema, container, scope);
    }
    if (strcmp(type, "panel") == 0) {
        lv_obj_t *panel = lv_obj_create(parent);
        yui_register_widget_id(node, panel);
        lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
        yui_apply_common_widget_attrs(panel, node, schema);
        yui_widget_runtime_t *runtime = yui_widget_runtime_create(panel, scope);
        if (runtime) {
            (void)yui_widget_bind_conditions(runtime, node, panel);
        }
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
    yui_widget_refs_clear();
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

static void yui_navigation_reset_stack(void)
{
    yui_modal_close_all();
    yui_widget_refs_clear();
    for (size_t i = 0; i < s_nav_count; ++i) {
        yui_screen_frame_destroy(&s_nav_stack[i]);
    }
    s_nav_count = 0U;
    yui_nav_queue_reset();
}

static yui_schema_runtime_t *yui_schema_runtime_attach(const char *name, yml_node_t *root)
{
    if (!root) {
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

    yui_navigation_reset_stack();
    if (s_loaded_schema) {
        yui_schema_runtime_destroy(s_loaded_schema);
    }
    s_loaded_schema = runtime;
    return runtime;
}

static yui_schema_runtime_t *yui_schema_runtime_load_named(const char *name)
{
    if (!name || name[0] == '\0') {
        return NULL;
    }
    if (s_loaded_schema && s_loaded_schema->name && strcasecmp(s_loaded_schema->name, name) == 0) {
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
    return yui_schema_runtime_attach(name, root);
}

static yui_schema_runtime_t *yui_schema_runtime_load_file(const char *path)
{
    if (!path || path[0] == '\0') {
        return NULL;
    }
    yml_node_t *root = NULL;
    if (yaml_core_parse_file(path, &root) != ESP_OK) {
        return NULL;
    }
    return yui_schema_runtime_attach(path, root);
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

static void yui_async_show_modal_cb(void *user_data)
{
    char *component = (char *)user_data;
    if (component) {
        (void)yui_modal_show_component(component);
        free(component);
    }
}

static esp_err_t yui_runtime_show_modal(const char *component)
{
    if (!component || component[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    char *copy = yui_strdup_local(component);
    if (!copy) {
        return ESP_ERR_NO_MEM;
    }
    if (lv_async_call(yui_async_show_modal_cb, copy) != LV_RESULT_OK) {
        free(copy);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void yui_async_close_modal_cb(void *user_data)
{
    (void)user_data;
    (void)yui_modal_close_top();
}

static esp_err_t yui_runtime_close_modal(void)
{
    if (lv_async_call(yui_async_close_modal_cb, NULL) != LV_RESULT_OK) {
        return ESP_FAIL;
    }
    return ESP_OK;
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

static esp_err_t yui_runtime_prepare(void)
{
    esp_err_t runtime_err = yamui_runtime_init();
    if (runtime_err != ESP_OK) {
        return runtime_err;
    }
    yui_register_builtin_natives();
    yui_nav_queue_init(yui_navigation_execute_request, NULL);
    yui_events_set_runtime(&s_runtime_vtable);
    return yui_register_display_watchers();
}

static esp_err_t yui_boot_loaded_schema(yui_schema_runtime_t *schema)
{
    if (!schema) {
        return ESP_ERR_NOT_FOUND;
    }
    yui_sync_display_brightness_from_state();
    const char *initial_screen = yui_schema_default_screen(&schema->schema);
    if (!initial_screen || initial_screen[0] == '\0') {
        return ESP_ERR_NOT_FOUND;
    }
    esp_err_t err = yui_navigation_push(initial_screen);
    if (err != ESP_OK) {
        yamui_log(YAMUI_LOG_LEVEL_ERROR, YAMUI_LOG_CAT_NAV, "Failed to load initial screen (%s)", esp_err_to_name(err));
    }
    return err;
}

esp_err_t lvgl_yaml_gui_load_named(const char *schema_name)
{
    esp_err_t err = yui_runtime_prepare();
    if (err != ESP_OK) {
        return err;
    }
    yui_schema_runtime_t *schema = yui_schema_runtime_load_named(schema_name);
    return yui_boot_loaded_schema(schema);
}

esp_err_t lvgl_yaml_gui_load_from_file(const char *path)
{
    esp_err_t err = yui_runtime_prepare();
    if (err != ESP_OK) {
        return err;
    }
    yui_schema_runtime_t *schema = yui_schema_runtime_load_file(path);
    return yui_boot_loaded_schema(schema);
}

esp_err_t lvgl_yaml_gui_load_default(void)
{
    const char *default_schema = ui_schemas_get_default_name();
    return lvgl_yaml_gui_load_named(default_schema);
}
