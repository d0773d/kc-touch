#include "lvgl_yaml_gui.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "kc_touch_display.h"
#include "lvgl.h"
#include "sensor_manager.h"
#include "ui_schemas.h"
#include "yaml_core.h"
#include "yaml_ui.h"
#include "yamui_events.h"
#include "yamui_expr.h"
#include "yamui_runtime.h"
#include "yamui_state.h"

static const char *TAG = "lvgl_yaml";

static const char *yui_resolve_token(const sensor_record_t *sensor, const char *token, char *scratch, size_t scratch_len);

static char *yui_strdup_local(const char *src)
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

typedef struct {
    const lv_event_t *lv_event;
    const sensor_record_t *sensor;
} yui_event_resolver_ctx_t;

typedef struct {
    const yui_widget_t *widget;
    const sensor_record_t *sensor;
    lv_obj_t *event_target;
    lv_obj_t *text_target;
    yui_state_watch_handle_t *watch_handles;
    size_t watch_count;
} yui_widget_instance_t;

typedef struct {
    char *name;
    yml_node_t *root;
    yui_schema_t schema;
} yui_screen_instance_t;

typedef struct {
    const yui_component_t *component;
    lv_obj_t *overlay;
    lv_obj_t *container;
} yui_modal_instance_t;

typedef struct {
    const sensor_record_t *sensor;
} yui_expression_ctx_t;

typedef enum {
    YUI_NAV_REQUEST_GOTO = 0,
    YUI_NAV_REQUEST_PUSH,
    YUI_NAV_REQUEST_POP,
} yui_nav_request_type_t;

typedef struct {
    yui_nav_request_type_t type;
    char *arg;
} yui_nav_request_t;

static yui_screen_instance_t *s_screen_stack;
static size_t s_screen_count;
static size_t s_screen_capacity;
static yui_modal_instance_t *s_modal_stack;
static size_t s_modal_count;
static size_t s_modal_capacity;
static bool s_navigation_rendering;
static yui_nav_request_t *s_nav_queue;
static size_t s_nav_queue_count;
static size_t s_nav_queue_capacity;

static esp_err_t yui_runtime_goto_screen(const char *screen);
static esp_err_t yui_runtime_push_screen(const char *screen);
static esp_err_t yui_runtime_pop_screen(void);
static esp_err_t yui_runtime_show_modal(const char *component);
static esp_err_t yui_runtime_close_modal(void);
static esp_err_t yui_runtime_call_native(const char *function, const char **args, size_t arg_count);
static esp_err_t yui_runtime_emit_event(const char *event, const char **args, size_t arg_count);
static esp_err_t yui_navigation_replace_top(const char *screen);
static esp_err_t yui_navigation_push_screen(const char *screen);
static esp_err_t yui_navigation_pop_screen_internal(void);

static const yui_action_runtime_t s_runtime_vtable = {
    .goto_screen = yui_runtime_goto_screen,
    .push_screen = yui_runtime_push_screen,
    .pop_screen = yui_runtime_pop_screen,
    .show_modal = yui_runtime_show_modal,
    .close_modal = yui_runtime_close_modal,
    .call_native = yui_runtime_call_native,
    .emit_event = yui_runtime_emit_event,
};

static char *yui_trimmed_copy(const char *start, size_t len)
{
    if (!start) {
        return NULL;
    }
    while (len > 0U && isspace((unsigned char)*start)) {
        ++start;
        --len;
    }
    while (len > 0U && isspace((unsigned char)start[len - 1U])) {
        --len;
    }
    char *copy = (char *)malloc(len + 1U);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, start, len);
    copy[len] = '\0';
    return copy;
}

static bool yui_expression_symbol_resolver(const char *identifier, void *ctx, yui_expr_value_t *out)
{
    if (!out) {
        return false;
    }
    yui_expr_value_reset(out);
    if (!identifier || identifier[0] == '\0') {
        return true;
    }
    yui_expression_ctx_t *runtime = (yui_expression_ctx_t *)ctx;
    if (strncmp(identifier, "sensor.", 7) == 0) {
        const char *sub = identifier + 7;
        char scratch[32];
        const sensor_record_t *sensor = runtime ? runtime->sensor : NULL;
        const char *resolved = yui_resolve_token(sensor, sub, scratch, sizeof(scratch));
        yui_expr_value_set_string_copy(out, resolved);
        return true;
    }
    const char *state_value = yui_state_get(identifier, NULL);
    if (state_value) {
        yui_expr_value_set_string_ref(out, state_value);
    } else {
        yui_expr_value_set_string_ref(out, "");
    }
    return true;
}

static bool yui_widget_has_events(const yui_widget_t *widget)
{
    if (!widget) {
        return false;
    }
    for (size_t i = 0; i < YUI_WIDGET_EVENT_COUNT; ++i) {
        if (widget->events.lists[i].count > 0U) {
            return true;
        }
    }
    return false;
}

static yui_widget_event_type_t yui_widget_event_from_lv(lv_event_code_t code)
{
    switch (code) {
        case LV_EVENT_CLICKED:
            return YUI_WIDGET_EVENT_CLICK;
        case LV_EVENT_PRESSED:
            return YUI_WIDGET_EVENT_PRESS;
        case LV_EVENT_RELEASED:
            return YUI_WIDGET_EVENT_RELEASE;
        case LV_EVENT_VALUE_CHANGED:
            return YUI_WIDGET_EVENT_CHANGE;
        case LV_EVENT_FOCUSED:
            return YUI_WIDGET_EVENT_FOCUS;
        case LV_EVENT_DEFOCUSED:
            return YUI_WIDGET_EVENT_BLUR;
        default:
            return YUI_WIDGET_EVENT_INVALID;
    }
}

static void yui_copy_string(char *dest, size_t dest_len, const char *src)
{
    if (!dest || dest_len == 0U) {
        return;
    }
    if (!src) {
        dest[0] = '\0';
        return;
    }
    size_t len = strlen(src);
    if (len >= dest_len) {
        len = dest_len - 1U;
    }
    memcpy(dest, src, len);
    dest[len] = '\0';
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
    lv_obj_t *target = lv_event_get_target(ctx->lv_event);
    if (!target) {
        return buffer;
    }
#if LV_USE_TEXTAREA
    if (lv_obj_check_type(target, &lv_textarea_class)) {
        const char *txt = lv_textarea_get_text(target);
        yui_copy_string(buffer, buffer_len, txt);
        return buffer;
    }
#endif
#if LV_USE_DROPDOWN
    if (lv_obj_check_type(target, &lv_dropdown_class)) {
        lv_dropdown_get_selected_str(target, buffer, buffer_len);
        return buffer;
    }
#endif
#if LV_USE_ROLLER
    if (lv_obj_check_type(target, &lv_roller_class)) {
        lv_roller_get_selected_str(target, buffer, buffer_len);
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
#if LV_USE_SPINBOX
    if (lv_obj_check_type(target, &lv_spinbox_class)) {
        int32_t value = lv_spinbox_get_value(target);
        snprintf(buffer, buffer_len, "%ld", (long)value);
        return buffer;
    }
#endif
    const void *param = lv_event_get_param(ctx->lv_event);
    if (param) {
        yui_copy_string(buffer, buffer_len, (const char *)param);
        return buffer;
    }
    return buffer;
}

static const char *yui_event_resolve_checked(const yui_event_resolver_ctx_t *ctx, char *buffer, size_t buffer_len)
{
    if (!buffer || buffer_len == 0U) {
        return "";
    }
    if (!ctx || !ctx->lv_event) {
        buffer[0] = '\0';
        return buffer;
    }
    lv_obj_t *target = lv_event_get_target(ctx->lv_event);
    bool checked = target && lv_obj_has_state(target, LV_STATE_CHECKED);
    yui_copy_string(buffer, buffer_len, checked ? "true" : "false");
    return buffer;
}

static const char *yui_event_symbol_resolver(const char *symbol, void *ctx, char *buffer, size_t buffer_len)
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
    if (resolver_ctx && resolver_ctx->sensor && strncmp(symbol, "sensor.", 7) == 0) {
        const char *sub = symbol + 7;
        return yui_resolve_token(resolver_ctx->sensor, sub, buffer, buffer_len);
    }

    const char *state_value = yui_state_get(symbol, NULL);
    if (state_value) {
        return state_value;
    }

    if (buffer && buffer_len > 0U) {
        buffer[0] = '\0';
    }
    return buffer;
}

static void yui_fire_widget_load_event(const yui_widget_instance_t *instance)
{
    if (!instance || !instance->widget) {
        return;
    }
    const yui_action_list_t *list = &instance->widget->events.lists[YUI_WIDGET_EVENT_LOAD];
    if (!list || list->count == 0U) {
        return;
    }
    yui_event_resolver_ctx_t ctx = {
        .lv_event = NULL,
        .sensor = instance->sensor,
    };
    yui_action_eval_ctx_t eval_ctx = {
        .resolver = yui_event_symbol_resolver,
        .resolver_ctx = &ctx,
    };
    esp_err_t err = yui_action_list_execute(list, &eval_ctx);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "on_load actions failed (%s)", esp_err_to_name(err));
    }
}

static void yui_widget_instance_refresh_text(yui_widget_instance_t *instance)
{
    if (!instance || !instance->widget || !instance->text_target || !instance->widget->text) {
        return;
    }
    if (!lv_obj_is_valid(instance->text_target)) {
        return;
    }
    char buffer[128];
    yui_format_text(instance->widget->text, instance->sensor, buffer, sizeof(buffer));
    lv_label_set_text(instance->text_target, buffer);
}

static void yui_widget_state_cb(const char *key, const char *value, void *user_ctx)
{
    (void)key;
    (void)value;
    yui_widget_instance_t *instance = (yui_widget_instance_t *)user_ctx;
    if (!instance || !instance->text_target || !lv_obj_is_valid(instance->text_target)) {
        return;
    }
    yui_widget_instance_refresh_text(instance);
}

static void yui_widget_instance_destroy(yui_widget_instance_t *instance)
{
    if (!instance) {
        return;
    }
    if (instance->watch_handles) {
        for (size_t i = 0; i < instance->watch_count; ++i) {
            if (instance->watch_handles[i] != 0U) {
                yui_state_unwatch(instance->watch_handles[i]);
            }
        }
        free(instance->watch_handles);
        instance->watch_handles = NULL;
    }
    free(instance);
}

static yui_widget_instance_t *yui_widget_instance_create(const yui_widget_t *widget, const sensor_record_t *sensor, lv_obj_t *event_target, lv_obj_t *text_target)
{
    if (!widget || !event_target) {
        return NULL;
    }
    yui_widget_instance_t *instance = (yui_widget_instance_t *)calloc(1, sizeof(yui_widget_instance_t));
    if (!instance) {
        return NULL;
    }
    instance->widget = widget;
    instance->sensor = sensor;
    instance->event_target = event_target;
    instance->text_target = text_target ? text_target : event_target;

    if (widget->state_binding_count > 0U) {
        instance->watch_handles = (yui_state_watch_handle_t *)calloc(widget->state_binding_count, sizeof(yui_state_watch_handle_t));
        if (!instance->watch_handles) {
            ESP_LOGW(TAG, "Failed to allocate watch handle buffer");
        } else {
            for (size_t i = 0; i < widget->state_binding_count; ++i) {
                const char *binding = widget->state_bindings[i];
                if (!binding) {
                    continue;
                }
                yui_state_watch_handle_t handle = 0;
                esp_err_t err = yui_state_watch(binding, yui_widget_state_cb, instance, &handle);
                if (err == ESP_OK) {
                    instance->watch_handles[instance->watch_count++] = handle;
                } else {
                    ESP_LOGW(TAG, "Failed to watch state key '%s' (%s)", binding, esp_err_to_name(err));
                }
            }
        }
    }

    return instance;
}

static void yui_bind_widget_runtime(lv_obj_t *event_target, lv_obj_t *text_target, const yui_widget_t *widget, const sensor_record_t *sensor)
{
    if (!event_target || !widget) {
        return;
    }
    bool needs_events = yui_widget_has_events(widget);
    bool needs_watchers = widget->state_binding_count > 0U;
    if (!needs_events && !needs_watchers) {
        return;
    }
    yui_widget_instance_t *instance = yui_widget_instance_create(widget, sensor, event_target, text_target);
    if (!instance) {
        ESP_LOGW(TAG, "Failed to initialize widget runtime context");
        return;
    }
    lv_obj_add_event_cb(event_target, yui_widget_event_cb, LV_EVENT_ALL, instance);
    if (needs_events) {
        yui_fire_widget_load_event(instance);
    }
}

static void yui_widget_event_cb(lv_event_t *event)
{
    yui_widget_instance_t *instance = (yui_widget_instance_t *)lv_event_get_user_data(event);
    if (!instance) {
        return;
    }
    if (lv_event_get_code(event) == LV_EVENT_DELETE) {
        yui_widget_instance_destroy(instance);
        return;
    }
    if (!instance->widget || !yui_widget_has_events(instance->widget)) {
        return;
    }
    yui_widget_event_type_t mapped = yui_widget_event_from_lv(lv_event_get_code(event));
    if (mapped == YUI_WIDGET_EVENT_INVALID) {
        return;
    }
    const yui_action_list_t *list = &instance->widget->events.lists[mapped];
    if (!list || list->count == 0U) {
        return;
    }
    yui_event_resolver_ctx_t ctx = {
        .lv_event = event,
        .sensor = instance->sensor,
    };
    yui_action_eval_ctx_t eval_ctx = {
        .resolver = yui_event_symbol_resolver,
        .resolver_ctx = &ctx,
    };
    esp_err_t err = yui_action_list_execute(list, &eval_ctx);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Action execution failed (%s)", esp_err_to_name(err));
    }
}

static void yui_navigation_queue_reset_request(yui_nav_request_t *request)
{
    if (!request) {
        return;
    }
    free(request->arg);
    request->arg = NULL;
}

static void yui_navigation_queue_clear(void)
{
    if (!s_nav_queue) {
        s_nav_queue_count = 0;
        s_nav_queue_capacity = 0;
        return;
    }
    for (size_t i = 0; i < s_nav_queue_count; ++i) {
        yui_navigation_queue_reset_request(&s_nav_queue[i]);
    }
    s_nav_queue_count = 0;
}

static esp_err_t yui_navigation_queue_push(yui_nav_request_type_t type, const char *arg)
{
    if (s_nav_queue_count == s_nav_queue_capacity) {
        size_t new_capacity = s_nav_queue_capacity == 0 ? 4U : s_nav_queue_capacity * 2U;
        yui_nav_request_t *resized = (yui_nav_request_t *)realloc(s_nav_queue, new_capacity * sizeof(yui_nav_request_t));
        if (!resized) {
            return ESP_ERR_NO_MEM;
        }
        memset(resized + s_nav_queue_capacity, 0, (new_capacity - s_nav_queue_capacity) * sizeof(yui_nav_request_t));
        s_nav_queue = resized;
        s_nav_queue_capacity = new_capacity;
    }
    char *copy = NULL;
    if (arg) {
        copy = yui_strdup_local(arg);
        if (!copy) {
            return ESP_ERR_NO_MEM;
        }
    }
    s_nav_queue[s_nav_queue_count].type = type;
    s_nav_queue[s_nav_queue_count].arg = copy;
    s_nav_queue_count++;
    return ESP_OK;
}

static bool yui_navigation_queue_pop(yui_nav_request_t *out)
{
    if (s_nav_queue_count == 0U) {
        return false;
    }
    if (out) {
        *out = s_nav_queue[0];
    } else {
        yui_navigation_queue_reset_request(&s_nav_queue[0]);
    }
    if (s_nav_queue_count > 1U) {
        memmove(s_nav_queue, s_nav_queue + 1, (s_nav_queue_count - 1U) * sizeof(yui_nav_request_t));
    }
    s_nav_queue_count--;
    return true;
}

static esp_err_t yui_navigation_execute_request(yui_nav_request_type_t type, const char *arg)
{
    switch (type) {
        case YUI_NAV_REQUEST_GOTO:
            if (!arg || arg[0] == '\0') {
                return ESP_ERR_INVALID_ARG;
            }
            return yui_navigation_replace_top(arg);
        case YUI_NAV_REQUEST_PUSH:
            if (!arg || arg[0] == '\0') {
                return ESP_ERR_INVALID_ARG;
            }
            return yui_navigation_push_screen(arg);
        case YUI_NAV_REQUEST_POP:
            return yui_navigation_pop_screen_internal();
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

static void yui_navigation_process_queue(void)
{
    if (s_navigation_rendering) {
        return;
    }
    while (s_nav_queue_count > 0U) {
        yui_nav_request_t request = {0};
        if (!yui_navigation_queue_pop(&request)) {
            break;
        }
        esp_err_t err = yui_navigation_execute_request(request.type, request.arg);
        yui_navigation_queue_reset_request(&request);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Queued navigation request failed (%s)", esp_err_to_name(err));
            break;
        }
        if (s_navigation_rendering) {
            break;
        }
    }
}

static esp_err_t yui_navigation_submit_request(yui_nav_request_type_t type, const char *arg)
{
    if (s_navigation_rendering || s_nav_queue_count > 0U) {
        return yui_navigation_queue_push(type, arg);
    }
    return yui_navigation_execute_request(type, arg);
}

static void yui_screen_instance_destroy(yui_screen_instance_t *instance)
{
    if (!instance) {
        return;
    }
    free(instance->name);
    instance->name = NULL;
    if (instance->root) {
        yml_node_free(instance->root);
        instance->root = NULL;
    }
    yui_schema_free(&instance->schema);
    memset(&instance->schema, 0, sizeof(instance->schema));
}

static void yui_navigation_clear_stack(void)
{
    yui_navigation_queue_clear();
    yui_modal_clear_stack();
    if (!s_screen_stack) {
        s_screen_count = 0;
        return;
    }
    for (size_t i = 0; i < s_screen_count; ++i) {
        yui_screen_instance_destroy(&s_screen_stack[i]);
    }
    s_screen_count = 0;
}

static esp_err_t yui_navigation_ensure_capacity(size_t desired)
{
    if (desired <= s_screen_capacity) {
        return ESP_OK;
    }
    size_t new_capacity = s_screen_capacity == 0 ? 2 : s_screen_capacity * 2;
    while (new_capacity < desired) {
        new_capacity *= 2;
    }
    yui_screen_instance_t *resized = (yui_screen_instance_t *)realloc(s_screen_stack, new_capacity * sizeof(yui_screen_instance_t));
    if (!resized) {
        ESP_LOGE(TAG, "Failed to grow navigation stack");
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = s_screen_capacity; i < new_capacity; ++i) {
        memset(&resized[i], 0, sizeof(yui_screen_instance_t));
    }
    s_screen_stack = resized;
    s_screen_capacity = new_capacity;
    return ESP_OK;
}

static const char *yui_navigation_resolve_name(const char *name)
{
    if (name && name[0] != '\0') {
        return name;
    }
    return ui_schemas_get_default_name();
}

static esp_err_t yui_screen_instance_load(const char *name, yui_screen_instance_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    const char *resolved = yui_navigation_resolve_name(name);
    if (!resolved) {
        ESP_LOGE(TAG, "No schema name resolved");
        return ESP_ERR_INVALID_ARG;
    }

    size_t blob_size = 0;
    const uint8_t *blob = ui_schemas_get_named(resolved, &blob_size);
    if (!blob || blob_size == 0U) {
        ESP_LOGE(TAG, "Schema '%s' not found", resolved);
        return ESP_ERR_NOT_FOUND;

        static lv_flex_align_t yui_align_to_lv(yui_component_align_t align)
        {
            switch (align) {
                case YUI_COMPONENT_ALIGN_CENTER:
                    return LV_FLEX_ALIGN_CENTER;
                case YUI_COMPONENT_ALIGN_END:
                    return LV_FLEX_ALIGN_END;
                case YUI_COMPONENT_ALIGN_STRETCH:
                    return LV_FLEX_ALIGN_STRETCH;
                case YUI_COMPONENT_ALIGN_START:
                default:
                    return LV_FLEX_ALIGN_START;
            }
        }

        static lv_flex_flow_t yui_flow_to_lv(yui_component_flow_t flow)
        {
            return flow == YUI_COMPONENT_FLOW_ROW ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN;
        }

        static void yui_component_apply_layout(lv_obj_t *obj, const yui_component_layout_t *layout)
        {
            if (!obj) {
                return;
            }
            uint8_t padding = layout ? layout->padding : 16;
            uint8_t gap = layout ? layout->gap : 12;
            lv_obj_set_style_pad_all(obj, padding, 0);
            lv_obj_set_style_pad_row(obj, gap, 0);
            lv_obj_set_style_pad_column(obj, gap, 0);
            lv_obj_set_style_radius(obj, 14, 0);
            lv_obj_set_style_border_width(obj, 0, 0);
            lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
            const char *bg = layout ? layout->background_color : NULL;
            lv_color_t color = yui_color_from_string(bg, lv_color_hex(0x1F1F2B));
            lv_obj_set_style_bg_color(obj, color, 0);
            lv_obj_set_flex_flow(obj, yui_flow_to_lv(layout ? layout->flow : YUI_COMPONENT_FLOW_COLUMN));
            lv_flex_align_t main_align = yui_align_to_lv(layout ? layout->main_align : YUI_COMPONENT_ALIGN_START);
            lv_flex_align_t cross_align = yui_align_to_lv(layout ? layout->cross_align : YUI_COMPONENT_ALIGN_START);
            lv_obj_set_flex_align(obj, main_align, cross_align, cross_align);
        }

        static const yui_component_t *yui_find_component(const char *name)
        {
            if (!name || name[0] == '\0') {
                return NULL;
            }
            for (size_t i = s_screen_count; i > 0; --i) {
                const yui_component_t *component = yui_schema_get_component(&s_screen_stack[i - 1U].schema, name);
                if (component) {
                    return component;
                }
            }
            return NULL;
        }

        static esp_err_t yui_render_component(const yui_component_t *component, lv_obj_t *parent)
        {
            if (!component || !parent) {
                return ESP_ERR_INVALID_ARG;
            }
            for (size_t i = 0; i < component->widget_count; ++i) {
                yui_render_widget(&component->widgets[i], NULL, NULL, parent);
            }
            return ESP_OK;
        }

        static lv_obj_t *yui_modal_create_overlay(void)
        {
            lv_obj_t *screen = lv_scr_act();
            if (!screen) {
                return NULL;
            }
            lv_obj_t *overlay = lv_obj_create(screen);
            lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
            lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
            lv_obj_set_style_bg_opa(overlay, LV_OPA_50, 0);
            lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
            return overlay;
        }

        static void yui_modal_emit_event(const char *event, const char *component_name)
        {
            if (!event) {
                return;
            }
            const char *args[1] = {component_name ? component_name : ""};
            size_t arg_count = (component_name && component_name[0] != '\0') ? 1U : 0U;
            (void)yamui_runtime_emit_event(event, arg_count > 0U ? args : NULL, arg_count);
        }

        static void yui_modal_instance_destroy(yui_modal_instance_t *instance, bool emit_event)
        {
            if (!instance) {
                return;
            }
            const char *component_name = (instance->component && instance->component->name) ? instance->component->name : NULL;
            if (instance->overlay && lv_obj_is_valid(instance->overlay)) {
                lv_obj_del(instance->overlay);
            }
            instance->overlay = NULL;
            instance->container = NULL;
            instance->component = NULL;
            if (emit_event) {
                yui_modal_emit_event("modal.closed", component_name);
            }
        }

        static esp_err_t yui_modal_ensure_capacity(size_t desired)
        {
            if (desired <= s_modal_capacity) {
                return ESP_OK;
            }
            size_t new_capacity = s_modal_capacity == 0 ? 2 : s_modal_capacity * 2;
            while (new_capacity < desired) {
                new_capacity *= 2;
            }
            yui_modal_instance_t *resized = (yui_modal_instance_t *)realloc(s_modal_stack, new_capacity * sizeof(yui_modal_instance_t));
            if (!resized) {
                ESP_LOGE(TAG, "Failed to grow modal stack");
                return ESP_ERR_NO_MEM;
            }
            for (size_t i = s_modal_capacity; i < new_capacity; ++i) {
                memset(&resized[i], 0, sizeof(yui_modal_instance_t));
            }
            s_modal_stack = resized;
            s_modal_capacity = new_capacity;
            return ESP_OK;
        }

        static void yui_modal_clear_stack(void)
        {
            if (!s_modal_stack) {
                s_modal_count = 0;
                return;
            }
            for (size_t i = 0; i < s_modal_count; ++i) {
                yui_modal_instance_destroy(&s_modal_stack[i], true);
            }
            s_modal_count = 0;
        }

        static esp_err_t yui_modal_render_component(yui_modal_instance_t *instance, const yui_component_t *component)
        {
            if (!instance || !component) {
                return ESP_ERR_INVALID_ARG;
            }
            lv_obj_t *overlay = yui_modal_create_overlay();
            if (!overlay) {
                return ESP_ERR_NO_MEM;
            }
            lv_obj_t *container = lv_obj_create(overlay);
            yui_component_apply_layout(container, &component->layout);
            lv_obj_set_width(container, LV_PCT(80));
            lv_obj_center(container);
            esp_err_t err = yui_render_component(component, container);
            if (err != ESP_OK) {
                lv_obj_del(overlay);
                return err;
            }
            instance->overlay = overlay;
            instance->container = container;
            instance->component = component;
            return ESP_OK;
        }

        static esp_err_t yui_modal_push_component(const yui_component_t *component)
        {
            if (!component) {
                return ESP_ERR_INVALID_ARG;
            }
            esp_err_t err = yui_modal_ensure_capacity(s_modal_count + 1U);
            if (err != ESP_OK) {
                return err;
            }
            yui_modal_instance_t instance = {0};
            err = yui_modal_render_component(&instance, component);
            if (err != ESP_OK) {
                return err;
            }
            s_modal_stack[s_modal_count++] = instance;
            yui_modal_emit_event("modal.opened", component->name);
            return ESP_OK;
        }
    }

    yml_node_t *root = NULL;
    esp_err_t err = yaml_core_parse_buffer((const char *)blob, blob_size, &root);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse schema '%s' (%s)", resolved, esp_err_to_name(err));
        return err;
    }

    yui_schema_t schema = {0};
    err = yui_schema_from_tree(root, &schema);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Schema '%s' invalid (%s)", resolved, esp_err_to_name(err));
        yml_node_free(root);
        return err;
    }

    out->name = yui_strdup_local(resolved);
    if (!out->name) {
        ESP_LOGE(TAG, "Failed to track schema name");
        yui_schema_free(&schema);
        yml_node_free(root);
        return ESP_ERR_NO_MEM;
    }
    out->root = root;
    out->schema = schema;
    return ESP_OK;
}

static esp_err_t yui_navigation_render_current(void)
{
    if (s_screen_count == 0U) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_navigation_rendering) {
        return ESP_ERR_INVALID_STATE;
    }
    s_navigation_rendering = true;
    yui_modal_clear_stack();
    esp_err_t err = yui_render_schema(&s_screen_stack[s_screen_count - 1U].schema);
    s_navigation_rendering = false;
    if (err == ESP_OK) {
        yui_navigation_process_queue();
    }
    return err;
}

static esp_err_t yui_navigation_push_instance(yui_screen_instance_t *instance)
{
    if (!instance) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = yui_navigation_ensure_capacity(s_screen_count + 1U);
    if (err != ESP_OK) {
        yui_screen_instance_destroy(instance);
        return err;
    }
    s_screen_stack[s_screen_count++] = *instance;
    memset(instance, 0, sizeof(*instance));
    err = yui_navigation_render_current();
    if (err != ESP_OK) {
        yui_screen_instance_destroy(&s_screen_stack[--s_screen_count]);
    }
    return err;
}

static esp_err_t yui_navigation_replace_top(const char *screen)
{
    yui_screen_instance_t new_instance = {0};
    esp_err_t err = yui_screen_instance_load(screen, &new_instance);
    if (err != ESP_OK) {
        return err;
    }
    if (s_screen_count == 0U) {
        return yui_navigation_push_instance(&new_instance);
    }

    size_t top_index = s_screen_count - 1U;
    yui_screen_instance_t old_instance = s_screen_stack[top_index];
    s_screen_stack[top_index] = new_instance;
    err = yui_navigation_render_current();
    if (err != ESP_OK) {
        yui_screen_instance_destroy(&s_screen_stack[top_index]);
        s_screen_stack[top_index] = old_instance;
        (void)yui_navigation_render_current();
        return err;
    }
    yui_screen_instance_destroy(&old_instance);
    return ESP_OK;
}

static esp_err_t yui_navigation_push_screen(const char *screen)
{
    yui_screen_instance_t instance = {0};
    esp_err_t err = yui_screen_instance_load(screen, &instance);
    if (err != ESP_OK) {
        return err;
    }
    return yui_navigation_push_instance(&instance);
}

static esp_err_t yui_navigation_pop_screen_internal(void)
{
    if (s_screen_count <= 1U) {
        ESP_LOGW(TAG, "Cannot pop root screen");
        return ESP_ERR_INVALID_STATE;
    }
    yui_screen_instance_t old_instance = s_screen_stack[--s_screen_count];
    esp_err_t err = yui_navigation_render_current();
    if (err != ESP_OK) {
        s_screen_stack[s_screen_count++] = old_instance;
        (void)yui_navigation_render_current();
        return err;
    }
    yui_screen_instance_destroy(&old_instance);
    return ESP_OK;
}

static esp_err_t yui_navigation_reset(const char *screen)
{
    yui_navigation_clear_stack();
    return yui_navigation_push_screen(screen);
}

static esp_err_t yui_runtime_goto_screen(const char *screen)
{
    return yui_navigation_submit_request(YUI_NAV_REQUEST_GOTO, screen);
}

static esp_err_t yui_runtime_push_screen(const char *screen)
{
    return yui_navigation_submit_request(YUI_NAV_REQUEST_PUSH, screen);
}

static esp_err_t yui_runtime_pop_screen(void)
{
    return yui_navigation_submit_request(YUI_NAV_REQUEST_POP, NULL);
}

static esp_err_t yui_runtime_show_modal(const char *component)
{
    const yui_component_t *definition = yui_find_component(component);
    if (!definition) {
        ESP_LOGW(TAG, "Modal component '%s' not found", component ? component : "<null>");
        return ESP_ERR_NOT_FOUND;
    }
    return yui_modal_push_component(definition);
}

static esp_err_t yui_runtime_close_modal(void)
{
    if (s_modal_count == 0U) {
        ESP_LOGW(TAG, "No modal to close");
        return ESP_ERR_INVALID_STATE;
    }
    yui_modal_instance_destroy(&s_modal_stack[--s_modal_count], true);
    return ESP_OK;
}

static esp_err_t yui_runtime_call_native(const char *function, const char **args, size_t arg_count)
{
    if (!function || function[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = yamui_runtime_call_function(function, args, arg_count);
    if (err == ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "Native function '%s' not registered", function);
    }
    return err;
}

static esp_err_t yui_runtime_emit_event(const char *event, const char **args, size_t arg_count)
{
    if (!event || event[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    return yamui_runtime_emit_event(event, args, arg_count);
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
    if (len == 7U) {
        return lv_color_hex(value & 0xFFFFFFU);
    }
    return lv_color_hex(value >> 8); // ignore alpha for now
}

static void yui_style_card(lv_obj_t *obj, const yui_style_t *style)
{
    if (!obj) {
        return;
    }
    if (style && style->background_color) {
        lv_color_t bg = yui_color_from_string(style->background_color, lv_color_hex(0x151523));
        lv_obj_set_style_bg_color(obj, bg, 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    } else {
        lv_obj_set_style_bg_color(obj, lv_color_hex(0x1E1E2E), 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    }
    int32_t radius = style ? style->radius : 16;
    int32_t padding = style ? style->padding : 16;
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_pad_all(obj, padding, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
}

static void yui_apply_label_variant(lv_obj_t *label, const yui_widget_t *widget, const yui_style_t *style)
{
    if (!label || !widget) {
        return;
    }
    const lv_font_t *font = &lv_font_montserrat_20;
    lv_color_t color = style && style->text_color ? yui_color_from_string(style->text_color, lv_color_hex(0xF4F4F8)) : lv_color_hex(0xF4F4F8);
    if (widget->variant) {
        if (strcmp(widget->variant, "value") == 0) {
            font = &lv_font_montserrat_20;
            if (style && style->accent_color) {
                color = yui_color_from_string(style->accent_color, color);
            }
        } else if (strcmp(widget->variant, "status") == 0) {
            font = LV_FONT_DEFAULT;
        }
    }
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
}

static void yui_assign_card_width(lv_obj_t *card, uint8_t columns)
{
    if (columns <= 1U) {
        lv_obj_set_width(card, LV_PCT(100));
    } else if (columns == 2U) {
        lv_obj_set_width(card, LV_PCT(48));
    } else if (columns == 3U) {
        lv_obj_set_width(card, LV_PCT(30));
    } else {
        lv_obj_set_width(card, LV_PCT(22));
    }
}

static const char *yui_resolve_token(const sensor_record_t *sensor, const char *token, char *scratch, size_t scratch_len)
{
    if (!token || !sensor) {
        const char *state_value = yui_state_get(token, "");
        return state_value ? state_value : "";
    }
    if (strcmp(token, "name") == 0) {
        return sensor->name;
    }
    if (strcmp(token, "value") == 0) {
        snprintf(scratch, scratch_len, "%.1f", sensor->value);
        return scratch;
    }
    if (strcmp(token, "unit") == 0) {
        return sensor->unit;
    }
    if (strcmp(token, "min") == 0) {
        snprintf(scratch, scratch_len, "%.1f", sensor->min);
        return scratch;
    }
    if (strcmp(token, "max") == 0) {
        snprintf(scratch, scratch_len, "%.1f", sensor->max);
        return scratch;
    }
    if (strcmp(token, "id") == 0) {
        return sensor->id;
    }
    if (strcmp(token, "type") == 0) {
        return sensor->type;
    }
    if (strcmp(token, "firmware") == 0) {
        return sensor->firmware;
    }
    if (strcmp(token, "address") == 0) {
        snprintf(scratch, scratch_len, "0x%02X", sensor->address);
        return scratch;
    }
    const char *state_value = yui_state_get(token, "");
    if (state_value) {
        return state_value;
    }
    return "";
}

static void yui_format_text(const char *tmpl, const sensor_record_t *sensor, char *out, size_t out_len)
{
    if (!tmpl || !out || out_len == 0U) {
        return;
    }
    yui_expression_ctx_t ctx = {
        .sensor = sensor,
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
            char *expr = yui_trimmed_copy(tmpl + 2, expr_len);
            if (expr) {
                size_t remaining = out_len - pos;
                esp_err_t err = yui_expr_eval_to_string(expr, yui_expression_symbol_resolver, &ctx, out + pos, remaining);
                if (err == ESP_OK) {
                    pos += strlen(out + pos);
                }
                free(expr);
            }
            tmpl = end + 2;
            continue;
        }
        out[pos++] = *tmpl++;
    }
    out[pos] = '\0';
}

static void yui_render_widget(const yui_widget_t *widget, const sensor_record_t *sensor, const yui_style_t *style, lv_obj_t *parent)
{
    if (!widget || !parent) {
        return;
    }
    char buffer[128] = "";
    if (widget->text) {
        yui_format_text(widget->text, sensor, buffer, sizeof(buffer));
    }
    switch (widget->type) {
        case YUI_WIDGET_LABEL: {
            lv_obj_t *label = lv_label_create(parent);
            yui_apply_label_variant(label, widget, style);
            lv_label_set_text(label, widget->text ? buffer : "");
            lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(label, LV_PCT(100));
            yui_bind_widget_runtime(label, label, widget, sensor);
            break;
        }
        case YUI_WIDGET_BUTTON: {
            lv_obj_t *btn = lv_btn_create(parent);
            lv_obj_set_width(btn, LV_PCT(100));
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x36364A), 0);
            lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
            lv_obj_set_style_pad_all(btn, 12, 0);
            lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_t *label = lv_label_create(btn);
            lv_label_set_text(label, widget->text ? buffer : "");
            lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
            lv_obj_center(label);
            yui_bind_widget_runtime(btn, label, widget, sensor);
            break;
        }
        case YUI_WIDGET_SPACER: {
            lv_obj_t *spacer = lv_obj_create(parent);
            lv_obj_remove_style_all(spacer);
            lv_obj_set_height(spacer, widget->size > 0 ? widget->size : 8);
            lv_obj_set_width(spacer, LV_PCT(100));
            lv_obj_clear_flag(spacer, LV_OBJ_FLAG_CLICKABLE);
            break;
        }
        default:
            break;
    }
}

static esp_err_t yui_render_card(const yui_template_t *tpl, const yui_schema_t *schema, const sensor_record_t *sensor, lv_obj_t *parent)
{
    if (!tpl || !schema || !sensor || !parent) {
        return ESP_ERR_INVALID_ARG;
    }
    const yui_style_t *style = tpl->style ? yui_schema_get_style(schema, tpl->style) : NULL;
    lv_obj_t *card = lv_obj_create(parent);
    yui_assign_card_width(card, schema->layout.columns);
    yui_style_card(card, style);

    if (tpl->title) {
        char buffer[128];
        yui_format_text(tpl->title, sensor, buffer, sizeof(buffer));
        lv_obj_t *title = lv_label_create(card);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
        lv_label_set_text(title, buffer);
        lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(title, LV_PCT(100));
    }

    if (tpl->subtitle) {
        char buffer[128];
        yui_format_text(tpl->subtitle, sensor, buffer, sizeof(buffer));
        lv_obj_t *subtitle = lv_label_create(card);
        lv_obj_set_style_text_font(subtitle, LV_FONT_DEFAULT, 0);
        lv_color_t color = style && style->accent_color ? yui_color_from_string(style->accent_color, lv_color_hex(0x8E9AC0)) : lv_color_hex(0x8E9AC0);
        lv_obj_set_style_text_color(subtitle, color, 0);
        lv_label_set_text(subtitle, buffer);
        lv_label_set_long_mode(subtitle, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(subtitle, LV_PCT(100));
    }

    for (size_t i = 0; i < tpl->widget_count; ++i) {
        yui_render_widget(&tpl->widgets[i], sensor, style, card);
    }

    return ESP_OK;
}

static esp_err_t yui_render_schema(const yui_schema_t *schema)
{
    size_t sensor_count = 0;
    const sensor_record_t *sensors = sensor_manager_get_snapshot(&sensor_count);
    if (!sensors || sensor_count == 0U) {
        ESP_LOGW(TAG, "No sensors available for YAML UI");
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t sensor_err = sensor_manager_update();
    if (sensor_err != ESP_OK) {
        ESP_LOGW(TAG, "Sensor update failed (%s)", esp_err_to_name(sensor_err));
    }

    lv_obj_t *screen = lv_scr_act();
    if (!screen) {
        return ESP_FAIL;
    }

    kc_touch_display_reset_ui_state();
    lv_obj_clean(screen);

    if (schema->layout.background_color) {
        lv_color_t bg = yui_color_from_string(schema->layout.background_color, lv_color_hex(0x090912));
        lv_obj_set_style_bg_color(screen, bg, 0);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    }

    lv_obj_t *grid = lv_obj_create(screen);
    lv_obj_set_size(grid, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_all(grid, schema->layout.padding, 0);
    lv_obj_set_style_pad_row(grid, schema->layout.v_spacing, 0);
    lv_obj_set_style_pad_column(grid, schema->layout.h_spacing, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    for (size_t i = 0; i < sensor_count; ++i) {
        const char *type_name = sensors[i].type[0] != '\0' ? sensors[i].type : "unknown";
        const yui_template_t *tpl = yui_schema_get_template(schema, type_name);
        if (!tpl) {
            ESP_LOGW(TAG, "No template defined for sensor type '%s'", type_name);
            continue;
        }
        yui_render_card(tpl, schema, &sensors[i], grid);
    }

    return ESP_OK;
}

static void yui_native_fn_goto(int argc, const char **argv)
{
    if (argc < 1 || !argv || !argv[0]) {
        return;
    }
    esp_err_t err = yui_runtime_goto_screen(argv[0]);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ui_goto failed (%s)", esp_err_to_name(err));
    }
}

static void yui_native_fn_push(int argc, const char **argv)
{
    if (argc < 1 || !argv || !argv[0]) {
        return;
    }
    esp_err_t err = yui_runtime_push_screen(argv[0]);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ui_push failed (%s)", esp_err_to_name(err));
    }
}

static void yui_native_fn_pop(int argc, const char **argv)
{
    (void)argc;
    (void)argv;
    esp_err_t err = yui_runtime_pop_screen();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ui_pop failed (%s)", esp_err_to_name(err));
    }
}

static void yui_native_fn_show_modal(int argc, const char **argv)
{
    if (argc < 1 || !argv || !argv[0]) {
        return;
    }
    esp_err_t err = yui_runtime_show_modal(argv[0]);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ui_show_modal failed (%s)", esp_err_to_name(err));
    }
}

static void yui_native_fn_close_modal(int argc, const char **argv)
{
    (void)argc;
    (void)argv;
    esp_err_t err = yui_runtime_close_modal();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ui_close_modal failed (%s)", esp_err_to_name(err));
    }
}

static void yui_native_fn_refresh(int argc, const char **argv)
{
    (void)argc;
    (void)argv;
    esp_err_t err = sensor_manager_update();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Sensor refresh failed (%s)", esp_err_to_name(err));
    }
    err = yui_navigation_render_current();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Unable to rerender after refresh (%s)", esp_err_to_name(err));
    }
}

static void yui_native_fn_reset_display(int argc, const char **argv)
{
    (void)argc;
    (void)argv;
    kc_touch_display_reset_ui_state();
    esp_err_t err = yui_navigation_render_current();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Display reset render failed (%s)", esp_err_to_name(err));
    }
}

static void yui_register_builtin_natives(void)
{
    const struct {
        const char *name;
        yamui_native_fn_t fn;
    } entries[] = {
        {"ui_goto", yui_native_fn_goto},
        {"ui_push", yui_native_fn_push},
        {"ui_pop", yui_native_fn_pop},
        {"ui_show_modal", yui_native_fn_show_modal},
        {"ui_close_modal", yui_native_fn_close_modal},
        {"ui_refresh", yui_native_fn_refresh},
        {"ui_reset_display", yui_native_fn_reset_display},
    };
    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); ++i) {
        esp_err_t err = yamui_runtime_register_function(entries[i].name, entries[i].fn);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to register native '%s' (%s)", entries[i].name, esp_err_to_name(err));
        }
    }
}

esp_err_t lvgl_yaml_gui_load_default(void)
{
    esp_err_t init_err = yamui_runtime_init();
    if (init_err != ESP_OK) {
        ESP_LOGW(TAG, "Runtime init failed (%s)", esp_err_to_name(init_err));
    }
    yui_register_builtin_natives();
    yui_events_set_runtime(&s_runtime_vtable);
    const char *default_screen = ui_schemas_get_default_name();
    if (!default_screen) {
        ESP_LOGE(TAG, "No default schema configured");
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = yui_navigation_reset(default_screen);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load default schema '%s' (%s)", default_screen, esp_err_to_name(err));
    }
    return err;
}
