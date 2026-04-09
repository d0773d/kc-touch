#include "lvgl_yaml_gui.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "kc_touch_gui.h"
#include "kc_touch_display.h"
#include "lvgl.h"
#include "freertos/semphr.h"
#include "yui_fonts.h"
#include "ui_schemas.h"
#include "yaml_core.h"
#include "yaml_ui.h"
#include "yamui_events.h"
#include "yamui_expr.h"
#include "yamui_async.h"
#include "yamui_logging.h"
#include "yamui_runtime.h"
#include "yamui_state.h"
#include "yui_camera.h"
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
    YUI_VALUE_BIND_CHECKBOX,
    YUI_VALUE_BIND_SLIDER,
    YUI_VALUE_BIND_BAR,
    YUI_VALUE_BIND_ARC,
    YUI_VALUE_BIND_DROPDOWN,
    YUI_VALUE_BIND_ROLLER,
    YUI_VALUE_BIND_LED,
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

typedef struct {
    lv_obj_t *container;
    lv_obj_t *image;
    lv_obj_t *placeholder;
    lv_obj_t *scroll_host;
    lv_draw_buf_t *draw_buf;
    uint8_t *staging_buf;
    size_t frame_len;
    uint32_t frame_width;
    uint32_t frame_height;
    uint32_t source_width;
    uint32_t source_height;
    int video_fd;
    bool stream_started;
    SemaphoreHandle_t frame_lock;
    bool frame_ready;
    bool update_queued;
    bool active;
    bool suspended;
} yui_camera_preview_t;

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
    bool text_template_is_translation_key;
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
    bool binding_refresh_pending;
    bool condition_refresh_pending;
    bool condition_refreshing;
    bool has_visible_state;
    bool last_visible;
    bool has_enabled_state;
    bool last_enabled;
    bool watch_all_state;
};

static void yui_widget_refresh_text(yui_widget_runtime_t *runtime);
static void yui_widget_refresh_value(yui_widget_runtime_t *runtime);
static void yui_widget_refresh_conditions(yui_widget_runtime_t *runtime);
static void yui_widget_schedule_condition_refresh(yui_widget_runtime_t *runtime);
static esp_err_t yui_widget_watch_all_state(yui_widget_runtime_t *runtime);
static bool yui_expr_value_is_truthy(const yui_expr_value_t *value);
static void yui_format_text(const char *tmpl, yui_component_scope_t *scope, char *out, size_t out_len);
static bool yui_format_node_text(const yml_node_t *node, yui_component_scope_t *scope, char *out, size_t out_len);
static lv_chart_type_t yui_chart_type_from_string(const char *value);
static lv_chart_update_mode_t yui_chart_update_mode_from_string(const char *value);
static lv_chart_axis_t yui_chart_axis_from_string(const char *value);
static lv_dir_t yui_dir_from_string(const char *value, lv_dir_t def);
static lv_menu_mode_header_t yui_menu_header_mode_from_string(const char *value);
static lv_menu_mode_root_back_button_t yui_menu_root_back_button_mode_from_string(const char *value);
static char *yui_strdup_local(const char *value);
static esp_err_t yui_collect_bindings_from_text(const char *text, char ***out_tokens, size_t *out_count);
static esp_err_t yui_collect_bindings_from_expr(const char *expr, char ***out_tokens, size_t *out_count);
static void yui_apply_layout(lv_obj_t *obj, const yml_node_t *layout_node, const char *default_type);
static lv_flex_align_t yui_flex_align_from_string(const char *value, lv_flex_align_t def);
static esp_err_t yui_render_widget_list(const yml_node_t *widgets_node, yui_schema_runtime_t *schema, lv_obj_t *parent, yui_component_scope_t *scope);
static bool yui_dropdown_select_value(lv_obj_t *dropdown, const char *value);
static bool yui_roller_select_value(lv_obj_t *roller, const char *value);
static esp_err_t yui_widget_bind_conditions(yui_widget_runtime_t *runtime, const yml_node_t *node, lv_obj_t *target);
static bool yui_theme_is_dark(void);
static lv_color_t yui_theme_screen_bg_color(void);
static lv_color_t yui_theme_modal_overlay_color(void);
static lv_color_t yui_theme_modal_panel_color(void);
static const yui_style_t *yui_resolve_style(const yui_schema_t *schema, const char *style_name);
static const yml_node_t *yui_find_event_node(const yml_node_t *node, const char *yaml_key, const char *companion_key);
static const char *yui_translate_key(const char *key);
static const char *yui_canonicalize_state_key(const char *key);
static esp_err_t yui_camera_preview_start(lv_obj_t *container, lv_obj_t *image, lv_obj_t *placeholder);
static void yui_camera_preview_stop(void);
static void yui_expr_value_set_coerced_scalar(yui_expr_value_t *out, const char *value);
static lv_obj_t *yui_find_scroll_host(lv_obj_t *obj);
static bool yui_parse_calendar_date_string(const char *value, lv_calendar_date_t *out_date);
static void yui_disable_shadows_recursive(lv_obj_t *obj);

typedef struct {
    const char *yaml_key;
    const char *companion_key;
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
    {"on_click", "onClick", YUI_WIDGET_EVENT_CLICK, LV_EVENT_CLICKED},
    {"on_press", "onPress", YUI_WIDGET_EVENT_PRESS, LV_EVENT_PRESSED},
    {"on_release", "onRelease", YUI_WIDGET_EVENT_RELEASE, LV_EVENT_RELEASED},
    {"on_change", "onChange", YUI_WIDGET_EVENT_CHANGE, LV_EVENT_VALUE_CHANGED},
    {"on_focus", "onFocus", YUI_WIDGET_EVENT_FOCUS, LV_EVENT_FOCUSED},
    {"on_blur", "onBlur", YUI_WIDGET_EVENT_BLUR, LV_EVENT_DEFOCUSED},
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

static const char *yui_canonicalize_state_key(const char *key)
{
    if (!key) {
        return NULL;
    }
    if (strncmp(key, "state.", 6) == 0 && key[6] != '\0') {
        return key + 6;
    }
    return key;
}

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
        const yml_node_t *value_node = NULL;
        if (instance_node && prop_name) {
            value_node = yml_node_get_child(instance_node, prop_name);
            if (!value_node) {
                const yml_node_t *props_node = yml_node_get_child(instance_node, "props");
                if (props_node && yml_node_get_type(props_node) == YML_NODE_MAPPING) {
                    value_node = yml_node_get_child(props_node, prop_name);
                }
            }
        }
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
static yui_state_watch_handle_t s_theme_watch;
static yui_state_watch_handle_t s_locale_watch;
typedef struct {
    lv_obj_t *overlay;
} yui_modal_frame_t;
typedef struct {
    lv_calendar_date_t *highlighted_dates;
    size_t highlighted_count;
} yui_calendar_runtime_t;
static yui_modal_frame_t *s_modal_stack;
static size_t s_modal_count;
static size_t s_modal_capacity;
static yui_widget_ref_t *s_widget_refs;
static size_t s_widget_ref_count;
static size_t s_widget_ref_capacity;
static yui_camera_preview_t s_camera_preview;
static int64_t s_camera_preview_last_frame_us;

#define YUI_CAMERA_PREVIEW_MIN_FRAME_INTERVAL_US (250000)
#define YUI_CAMERA_PREVIEW_MAX_WIDTH 200U
#define YUI_CAMERA_PREVIEW_MAX_HEIGHT 160U

static void yui_camera_preview_apply_frame(void *ctx)
{
    (void)ctx;

    if (!s_camera_preview.active || !s_camera_preview.image || !s_camera_preview.draw_buf || !s_camera_preview.frame_lock) {
        s_camera_preview.update_queued = false;
        return;
    }
    if (s_camera_preview.suspended) {
        s_camera_preview.update_queued = false;
        return;
    }

    if (xSemaphoreTake(s_camera_preview.frame_lock, 0) == pdTRUE) {
        if (s_camera_preview.frame_ready && s_camera_preview.staging_buf && s_camera_preview.draw_buf->data) {
            memcpy(s_camera_preview.draw_buf->data, s_camera_preview.staging_buf, s_camera_preview.frame_len);
            s_camera_preview.frame_ready = false;
            lv_obj_remove_flag(s_camera_preview.image, LV_OBJ_FLAG_HIDDEN);
            if (s_camera_preview.placeholder) {
                lv_obj_add_flag(s_camera_preview.placeholder, LV_OBJ_FLAG_HIDDEN);
            }
            lv_obj_invalidate(s_camera_preview.image);
        }
        xSemaphoreGive(s_camera_preview.frame_lock);
    }

    s_camera_preview.update_queued = false;
}

static void yui_camera_preview_frame_cb(uint8_t *frame_buf,
                                        uint8_t frame_index,
                                        uint32_t width,
                                        uint32_t height,
                                        size_t frame_len)
{
    (void)frame_index;
    (void)width;
    (void)height;

    if (!s_camera_preview.active || s_camera_preview.suspended || !frame_buf || !s_camera_preview.staging_buf || !s_camera_preview.frame_lock) {
        return;
    }

    int64_t now_us = esp_timer_get_time();
    if ((now_us - s_camera_preview_last_frame_us) < YUI_CAMERA_PREVIEW_MIN_FRAME_INTERVAL_US) {
        return;
    }

    if (frame_len > s_camera_preview.frame_len) {
        frame_len = s_camera_preview.frame_len;
    }

    if (xSemaphoreTake(s_camera_preview.frame_lock, 0) == pdTRUE) {
        const uint32_t preview_w = s_camera_preview.frame_width;
        const uint32_t preview_h = s_camera_preview.frame_height;
        const uint32_t source_w = s_camera_preview.source_width;
        const uint32_t source_h = s_camera_preview.source_height;
        uint16_t *dst = (uint16_t *)s_camera_preview.staging_buf;
        const uint16_t *src = (const uint16_t *)frame_buf;

        for (uint32_t y = 0; y < preview_h; ++y) {
            uint32_t src_y = (y * source_h) / preview_h;
            const uint16_t *src_row = src + (src_y * source_w);
            uint16_t *dst_row = dst + (y * preview_w);
            for (uint32_t x = 0; x < preview_w; ++x) {
                uint32_t src_x = (x * source_w) / preview_w;
                dst_row[x] = src_row[src_x];
            }
        }
        s_camera_preview.frame_ready = true;
        s_camera_preview_last_frame_us = now_us;
        xSemaphoreGive(s_camera_preview.frame_lock);
    }

    if (!s_camera_preview.update_queued) {
        s_camera_preview.update_queued = true;
        if (kc_touch_gui_dispatch(yui_camera_preview_apply_frame, NULL, 0) != ESP_OK) {
            s_camera_preview.update_queued = false;
        }
    }
}

static void yui_camera_preview_stop(void)
{
    yui_camera_stream_register_frame_cb(NULL);

    if (s_camera_preview.video_fd >= 0) {
        if (s_camera_preview.stream_started) {
            (void)yui_camera_stream_stop(s_camera_preview.video_fd);
            (void)yui_camera_stream_wait_for_stop();
        }
        (void)yui_camera_stream_close(s_camera_preview.video_fd);
        s_camera_preview.video_fd = -1;
    }

    if (s_camera_preview.draw_buf) {
        if (s_camera_preview.draw_buf->unaligned_data) {
            heap_caps_free(s_camera_preview.draw_buf->unaligned_data);
        }
        free(s_camera_preview.draw_buf);
        s_camera_preview.draw_buf = NULL;
    }
    if (s_camera_preview.staging_buf) {
        heap_caps_free(s_camera_preview.staging_buf);
        s_camera_preview.staging_buf = NULL;
    }
    if (s_camera_preview.frame_lock) {
        vSemaphoreDelete(s_camera_preview.frame_lock);
        s_camera_preview.frame_lock = NULL;
    }

    s_camera_preview.container = NULL;
    s_camera_preview.image = NULL;
    s_camera_preview.placeholder = NULL;
    s_camera_preview.scroll_host = NULL;
    s_camera_preview.frame_len = 0U;
    s_camera_preview.frame_width = 0U;
    s_camera_preview.frame_height = 0U;
    s_camera_preview.source_width = 0U;
    s_camera_preview.source_height = 0U;
    s_camera_preview.frame_ready = false;
    s_camera_preview.update_queued = false;
    s_camera_preview.stream_started = false;
    s_camera_preview.active = false;
    s_camera_preview.suspended = false;
    s_camera_preview_last_frame_us = 0;
}

static void yui_camera_preview_scroll_cb(lv_event_t *event)
{
    if (!event || !s_camera_preview.active) {
        return;
    }

    lv_obj_t *target = lv_event_get_target(event);
    if (target != s_camera_preview.scroll_host) {
        return;
    }

    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_SCROLL_BEGIN) {
        s_camera_preview.suspended = true;
        s_camera_preview.update_queued = false;
        return;
    }

    if (code == LV_EVENT_SCROLL_END) {
        s_camera_preview.suspended = false;
        if (s_camera_preview.frame_ready && !s_camera_preview.update_queued) {
            s_camera_preview.update_queued = true;
            if (kc_touch_gui_dispatch(yui_camera_preview_apply_frame, NULL, 0) != ESP_OK) {
                s_camera_preview.update_queued = false;
            }
        }
    }
}

static lv_obj_t *yui_find_scroll_host(lv_obj_t *obj)
{
    lv_obj_t *cursor = obj;
    while (cursor) {
        if (lv_obj_has_flag(cursor, LV_OBJ_FLAG_SCROLLABLE)) {
            return cursor;
        }
        cursor = lv_obj_get_parent(cursor);
    }
    return NULL;
}

static void yui_camera_preview_delete_cb(lv_event_t *event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_DELETE) {
        return;
    }

    lv_obj_t *target = lv_event_get_target(event);
    if (target == s_camera_preview.container) {
        yui_camera_preview_stop();
    }
}

static void yui_calendar_delete_cb(lv_event_t *event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_DELETE) {
        return;
    }

    yui_calendar_runtime_t *runtime = (yui_calendar_runtime_t *)lv_event_get_user_data(event);
    if (!runtime) {
        return;
    }

    free(runtime->highlighted_dates);
    free(runtime);
}

static esp_err_t yui_camera_preview_start(lv_obj_t *container, lv_obj_t *image, lv_obj_t *placeholder)
{
    const char *preview_device = yui_camera_preview_device_path();

    if (!container || !image || !placeholder) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!kc_touch_gui_camera_ready() || !yui_camera_is_ready()) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (s_camera_preview.active) {
        return ESP_ERR_INVALID_STATE;
    }

    int video_fd = yui_camera_stream_open(preview_device, YUI_CAMERA_FMT_RGB565);
    if (video_fd < 0) {
        return ESP_FAIL;
    }

    esp_err_t err = yui_camera_stream_set_buffers(video_fd, 2, NULL);
    if (err != ESP_OK) {
        (void)yui_camera_stream_close(video_fd);
        return err;
    }

    uint32_t width = 0U;
    uint32_t height = 0U;
    size_t frame_len = 0U;
    err = yui_camera_stream_get_frame_info(&width, &height, &frame_len);
    if (err != ESP_OK) {
        (void)yui_camera_stream_close(video_fd);
        return err;
    }

    uint32_t preview_width = width;
    uint32_t preview_height = height;
    if (preview_width > YUI_CAMERA_PREVIEW_MAX_WIDTH) {
        preview_width = YUI_CAMERA_PREVIEW_MAX_WIDTH;
        preview_height = (height * preview_width) / width;
    }
    if (preview_height > YUI_CAMERA_PREVIEW_MAX_HEIGHT) {
        preview_height = YUI_CAMERA_PREVIEW_MAX_HEIGHT;
        preview_width = (width * preview_height) / height;
    }
    if (preview_width == 0U) {
        preview_width = 1U;
    }
    if (preview_height == 0U) {
        preview_height = 1U;
    }

    uint32_t draw_buf_size = LV_DRAW_BUF_SIZE(preview_width, preview_height, LV_COLOR_FORMAT_RGB565);
    lv_draw_buf_t *draw_buf = (lv_draw_buf_t *)calloc(1, sizeof(lv_draw_buf_t));
    if (!draw_buf) {
        (void)yui_camera_stream_close(video_fd);
        return ESP_ERR_NO_MEM;
    }

    void *draw_mem = heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN,
                                             draw_buf_size,
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!draw_mem) {
        draw_mem = heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, draw_buf_size, MALLOC_CAP_8BIT);
    }
    if (!draw_mem) {
        free(draw_buf);
        (void)yui_camera_stream_close(video_fd);
        return ESP_ERR_NO_MEM;
    }

    if (lv_draw_buf_init(draw_buf, preview_width, preview_height, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO, draw_mem, draw_buf_size) != LV_RESULT_OK) {
        heap_caps_free(draw_mem);
        free(draw_buf);
        (void)yui_camera_stream_close(video_fd);
        return ESP_FAIL;
    }
    lv_draw_buf_set_flag(draw_buf, LV_IMAGE_FLAGS_MODIFIABLE);

    uint8_t *staging = (uint8_t *)heap_caps_malloc(draw_buf->data_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!staging) {
        staging = (uint8_t *)heap_caps_malloc(draw_buf->data_size, MALLOC_CAP_8BIT);
    }
    if (!staging) {
        heap_caps_free(draw_mem);
        free(draw_buf);
        (void)yui_camera_stream_close(video_fd);
        return ESP_ERR_NO_MEM;
    }

    SemaphoreHandle_t frame_lock = xSemaphoreCreateMutex();
    if (!frame_lock) {
        heap_caps_free(staging);
        heap_caps_free(draw_mem);
        free(draw_buf);
        (void)yui_camera_stream_close(video_fd);
        return ESP_ERR_NO_MEM;
    }

    memset(draw_buf->data, 0, draw_buf->data_size);
    memset(staging, 0, draw_buf->data_size);

    s_camera_preview.container = container;
    s_camera_preview.image = image;
    s_camera_preview.placeholder = placeholder;
    s_camera_preview.scroll_host = yui_find_scroll_host(container);
    s_camera_preview.draw_buf = draw_buf;
    s_camera_preview.staging_buf = staging;
    s_camera_preview.frame_len = draw_buf->data_size;
    s_camera_preview.frame_width = preview_width;
    s_camera_preview.frame_height = preview_height;
    s_camera_preview.source_width = width;
    s_camera_preview.source_height = height;
    s_camera_preview.video_fd = video_fd;
    s_camera_preview.frame_lock = frame_lock;
    s_camera_preview.frame_ready = false;
    s_camera_preview.update_queued = false;
    s_camera_preview.stream_started = false;
    s_camera_preview.active = true;
    s_camera_preview.suspended = false;

    lv_image_set_src(image, draw_buf);
    lv_obj_set_size(image, (int32_t)preview_width, (int32_t)preview_height);
    lv_obj_center(image);
    lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);

    err = yui_camera_stream_register_frame_cb(yui_camera_preview_frame_cb);
    if (err == ESP_OK) {
        err = yui_camera_stream_start(video_fd, tskNO_AFFINITY);
    }
    if (err == ESP_OK) {
        s_camera_preview.stream_started = true;
    }
    if (err != ESP_OK) {
        yui_camera_preview_stop();
        return err;
    }

    if (s_camera_preview.scroll_host) {
        lv_obj_add_event_cb(s_camera_preview.scroll_host, yui_camera_preview_scroll_cb, LV_EVENT_SCROLL_BEGIN, NULL);
        lv_obj_add_event_cb(s_camera_preview.scroll_host, yui_camera_preview_scroll_cb, LV_EVENT_SCROLL_END, NULL);
    }

    return ESP_OK;
}

static void yui_widget_refs_clear(void)
{
    yui_camera_preview_stop();
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
    lv_obj_set_style_bg_color(overlay, yui_theme_modal_overlay_color(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_60, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *panel = lv_obj_create(overlay);
    lv_obj_set_style_bg_color(panel, yui_theme_modal_panel_color(), 0);
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

static bool yui_theme_is_dark(void)
{
    return yui_state_get_bool("ui.dark_mode", false);
}

static const char *yui_current_locale(void)
{
    if (!s_loaded_schema) {
        return yui_state_get("ui.locale", "");
    }
    return yui_state_get("ui.locale", yui_schema_locale(&s_loaded_schema->schema));
}

static const char *yui_translate_key(const char *key)
{
    if (!key || key[0] == '\0' || !s_loaded_schema) {
        return key;
    }
    const char *translated = yui_schema_translate(&s_loaded_schema->schema, yui_current_locale(), key);
    return translated ? translated : key;
}

static lv_color_t yui_theme_screen_bg_color(void)
{
    return yui_theme_is_dark() ? lv_color_hex(0x0F1117) : lv_color_hex(0xF5F7FB);
}

static lv_color_t yui_theme_modal_overlay_color(void)
{
    return yui_theme_is_dark() ? lv_color_hex(0x000000) : lv_color_hex(0x1B2233);
}

static lv_color_t yui_theme_modal_panel_color(void)
{
    return yui_theme_is_dark() ? lv_color_hex(0x25293C) : lv_color_hex(0xFFFFFF);
}

static const yui_style_t *yui_resolve_style(const yui_schema_t *schema, const char *style_name)
{
    if (!schema || !style_name || style_name[0] == '\0') {
        return NULL;
    }
    char themed_name[96];
    snprintf(themed_name, sizeof(themed_name), "%s.%s", yui_theme_is_dark() ? "dark" : "light", style_name);
    const yui_style_t *style = yui_schema_get_style(schema, themed_name);
    if (style) {
        return style;
    }
    return yui_schema_get_style(schema, style_name);
}

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

static void yui_theme_watch_cb(const char *key, const char *value, void *user_ctx)
{
    (void)key;
    (void)value;
    (void)user_ctx;
    (void)yui_nav_queue_submit(YUI_NAV_REQUEST_REFRESH, NULL);
}

static void yui_locale_watch_cb(const char *key, const char *value, void *user_ctx)
{
    (void)key;
    (void)value;
    (void)user_ctx;
    (void)yui_nav_queue_submit(YUI_NAV_REQUEST_REFRESH, NULL);
}

static esp_err_t yui_register_display_watchers(void)
{
    if (s_display_brightness_watch != 0U) {
        return ESP_OK;
    }
    return yui_state_watch("display.brightness", yui_display_brightness_watch_cb, NULL, &s_display_brightness_watch);
}

static esp_err_t yui_register_theme_watchers(void)
{
    if (s_theme_watch != 0U) {
        return ESP_OK;
    }
    return yui_state_watch("ui.dark_mode", yui_theme_watch_cb, NULL, &s_theme_watch);
}

static esp_err_t yui_register_locale_watchers(void)
{
    if (s_locale_watch != 0U) {
        return ESP_OK;
    }
    return yui_state_watch("ui.locale", yui_locale_watch_cb, NULL, &s_locale_watch);
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
    yui_expr_value_set_coerced_scalar(out, value);
    return true;
}

static void yui_expr_value_set_coerced_scalar(yui_expr_value_t *out, const char *value)
{
    if (!out) {
        return;
    }
    if (!value) {
        yui_expr_value_set_string_ref(out, "");
        return;
    }

    if (strcasecmp(value, "true") == 0) {
        yui_expr_value_set_bool(out, true);
        return;
    }
    if (strcasecmp(value, "false") == 0) {
        yui_expr_value_set_bool(out, false);
        return;
    }

    char *end = NULL;
    double number = strtod(value, &end);
    if (end && end != value) {
        while (*end != '\0' && isspace((unsigned char)*end)) {
            end++;
        }
        if (*end == '\0') {
            yui_expr_value_set_number(out, number);
            return;
        }
    }

    yui_expr_value_set_string_ref(out, value);
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
#if LV_USE_ARC
    if (lv_obj_check_type(target, &lv_arc_class)) {
        int32_t value = lv_arc_get_value(target);
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

static const char *yui_node_resolved_localized_scalar(const yml_node_t *node,
                                                      const char *value_key,
                                                      const char *translation_key_field,
                                                      yui_component_scope_t *scope,
                                                      char *buffer,
                                                      size_t buffer_len)
{
    const char *raw_translation_key = yui_node_scalar(node, translation_key_field);
    if (raw_translation_key && raw_translation_key[0] != '\0') {
        char key_buffer[YUI_TEXT_BUFFER_MAX];
        key_buffer[0] = '\0';
        yui_format_text(raw_translation_key, scope, key_buffer, sizeof(key_buffer));
        const char *translated = yui_translate_key(key_buffer);
        if (!buffer || buffer_len == 0U) {
            return translated;
        }
        snprintf(buffer, buffer_len, "%s", translated ? translated : "");
        return buffer;
    }
    return yui_node_resolved_scalar(node, value_key, scope, buffer, buffer_len);
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

static bool yui_format_node_text(const yml_node_t *node, yui_component_scope_t *scope, char *out, size_t out_len)
{
    if (!out || out_len == 0U) {
        return false;
    }
    out[0] = '\0';
    if (!node) {
        return false;
    }

    const char *raw_text = yml_node_get_scalar(node);
    if (raw_text) {
        yui_format_text(raw_text, scope, out, out_len);
        return true;
    }

    if (yml_node_get_type(node) == YML_NODE_MAPPING) {
        const yml_node_t *value_node = yml_node_get_child(node, "value");
        if (!value_node) {
            value_node = yml_node_get_child(node, "text");
        }
        if (value_node) {
            return yui_format_node_text(value_node, scope, out, out_len);
        }
    }

    if (yml_node_get_type(node) == YML_NODE_SEQUENCE && yml_node_child_count(node) == 1U) {
        const yml_node_t *child = yml_node_child_at(node, 0);
        if (child) {
            return yui_format_node_text(child, scope, out, out_len);
        }
    }

    return false;
}

static lv_chart_type_t yui_chart_type_from_string(const char *value)
{
    if (!value) {
        return LV_CHART_TYPE_LINE;
    }
    if (strcasecmp(value, "bar") == 0) {
        return LV_CHART_TYPE_BAR;
    }
    if (strcasecmp(value, "scatter") == 0) {
        return LV_CHART_TYPE_SCATTER;
    }
    if (strcasecmp(value, "curve") == 0) {
        return LV_CHART_TYPE_CURVE;
    }
    if (strcasecmp(value, "stacked") == 0) {
        return LV_CHART_TYPE_STACKED;
    }
    if (strcasecmp(value, "none") == 0) {
        return LV_CHART_TYPE_NONE;
    }
    return LV_CHART_TYPE_LINE;
}

static lv_chart_update_mode_t yui_chart_update_mode_from_string(const char *value)
{
    if (value && strcasecmp(value, "circular") == 0) {
        return LV_CHART_UPDATE_MODE_CIRCULAR;
    }
    return LV_CHART_UPDATE_MODE_SHIFT;
}

static lv_chart_axis_t yui_chart_axis_from_string(const char *value)
{
    if (!value) {
        return LV_CHART_AXIS_PRIMARY_Y;
    }
    if (strcasecmp(value, "secondary_y") == 0 || strcasecmp(value, "y2") == 0) {
        return LV_CHART_AXIS_SECONDARY_Y;
    }
    if (strcasecmp(value, "primary_x") == 0 || strcasecmp(value, "x1") == 0) {
        return LV_CHART_AXIS_PRIMARY_X;
    }
    if (strcasecmp(value, "secondary_x") == 0 || strcasecmp(value, "x2") == 0) {
        return LV_CHART_AXIS_SECONDARY_X;
    }
    return LV_CHART_AXIS_PRIMARY_Y;
}

static lv_dir_t yui_dir_from_string(const char *value, lv_dir_t def)
{
    if (!value) {
        return def;
    }
    if (strcasecmp(value, "top") == 0) {
        return LV_DIR_TOP;
    }
    if (strcasecmp(value, "bottom") == 0) {
        return LV_DIR_BOTTOM;
    }
    if (strcasecmp(value, "left") == 0) {
        return LV_DIR_LEFT;
    }
    if (strcasecmp(value, "right") == 0) {
        return LV_DIR_RIGHT;
    }
    return def;
}

static lv_menu_mode_header_t yui_menu_header_mode_from_string(const char *value)
{
    if (!value || value[0] == '\0') {
        return LV_MENU_HEADER_TOP_FIXED;
    }
    if (strcasecmp(value, "top_unfixed") == 0 || strcasecmp(value, "unfixed") == 0) {
        return LV_MENU_HEADER_TOP_UNFIXED;
    }
    if (strcasecmp(value, "bottom_fixed") == 0 || strcasecmp(value, "bottom") == 0) {
        return LV_MENU_HEADER_BOTTOM_FIXED;
    }
    return LV_MENU_HEADER_TOP_FIXED;
}

static lv_menu_mode_root_back_button_t yui_menu_root_back_button_mode_from_string(const char *value)
{
    if (!value || value[0] == '\0') {
        return LV_MENU_ROOT_BACK_BUTTON_DISABLED;
    }
    if (strcasecmp(value, "true") == 0 || strcmp(value, "1") == 0 ||
        strcasecmp(value, "enabled") == 0 || strcasecmp(value, "show") == 0) {
        return LV_MENU_ROOT_BACK_BUTTON_ENABLED;
    }
    return LV_MENU_ROOT_BACK_BUTTON_DISABLED;
}

static bool yui_parse_calendar_date_string(const char *value, lv_calendar_date_t *out_date)
{
    if (!value || !out_date) {
        return false;
    }

    unsigned int year = 0U;
    unsigned int month = 0U;
    unsigned int day = 0U;
    if (sscanf(value, "%u-%u-%u", &year, &month, &day) != 3) {
        return false;
    }
    if (month < 1U || month > 12U || day < 1U || day > 31U || year > 65535U) {
        return false;
    }

    out_date->year = (uint16_t)year;
    out_date->month = (uint8_t)month;
    out_date->day = (uint8_t)day;
    return true;
}

static void yui_disable_shadows_recursive(lv_obj_t *obj)
{
    if (!obj) {
        return;
    }
    lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(obj, 0, LV_PART_ITEMS);
    lv_obj_set_style_shadow_opa(obj, LV_OPA_TRANSP, LV_PART_ITEMS);

    uint32_t child_count = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < child_count; ++i) {
        lv_obj_t *child = lv_obj_get_child(obj, (int32_t)i);
        yui_disable_shadows_recursive(child);
    }
}

static uint32_t yui_table_row_column_count(const yml_node_t *row_node)
{
    if (!row_node) {
        return 0U;
    }

    const yml_node_t *cells_node = row_node;
    if (yml_node_get_type(row_node) == YML_NODE_MAPPING) {
        const yml_node_t *explicit_cells = yml_node_get_child(row_node, "cells");
        if (explicit_cells) {
            cells_node = explicit_cells;
        }
    }

    if (cells_node && yml_node_get_type(cells_node) == YML_NODE_SEQUENCE) {
        return (uint32_t)yml_node_child_count(cells_node);
    }

    if (yml_node_get_type(row_node) == YML_NODE_MAPPING) {
        uint32_t count = 0U;
        for (const yml_node_t *child = yml_node_child_at(row_node, 0); child; child = yml_node_next(child)) {
            const char *key = yml_node_get_key(child);
            if (key && strcmp(key, "cells") == 0) {
                continue;
            }
            count++;
        }
        return count;
    }

    return 0U;
}

static const yml_node_t *yui_table_row_cell_at(const yml_node_t *row_node, uint32_t col)
{
    if (!row_node) {
        return NULL;
    }

    const yml_node_t *cells_node = row_node;
    if (yml_node_get_type(row_node) == YML_NODE_MAPPING) {
        const yml_node_t *explicit_cells = yml_node_get_child(row_node, "cells");
        if (explicit_cells) {
            cells_node = explicit_cells;
        }
    }

    if (cells_node && yml_node_get_type(cells_node) == YML_NODE_SEQUENCE) {
        return yml_node_child_at(cells_node, col);
    }

    if (yml_node_get_type(row_node) == YML_NODE_MAPPING) {
        uint32_t idx = 0U;
        for (const yml_node_t *child = yml_node_child_at(row_node, 0); child; child = yml_node_next(child)) {
            const char *key = yml_node_get_key(child);
            if (key && strcmp(key, "cells") == 0) {
                continue;
            }
            if (idx == col) {
                return child;
            }
            idx++;
        }
    }

    return NULL;
}

static void yui_widget_refresh_text(yui_widget_runtime_t *runtime)
{
    if (!runtime || runtime->disposed || !runtime->text_target || !runtime->text_template) {
        return;
    }
    char buffer[YUI_TEXT_BUFFER_MAX];
    yui_format_text(runtime->text_template, runtime->scope, buffer, sizeof(buffer));
    const char *final_text = buffer;
    if (runtime->text_template_is_translation_key) {
        const char *translated = yui_translate_key(buffer);
        final_text = translated ? translated : "";
    }
    lv_label_set_text(runtime->text_target, final_text);
    lv_obj_mark_layout_as_dirty(runtime->text_target);
    lv_obj_invalidate(runtime->text_target);
    lv_obj_t *parent = lv_obj_get_parent(runtime->text_target);
    if (parent) {
        lv_obj_mark_layout_as_dirty(parent);
        lv_obj_invalidate(parent);
    }
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

static bool yui_roller_select_value(lv_obj_t *roller, const char *value)
{
#if LV_USE_ROLLER
    if (!roller || !value || value[0] == '\0') {
        return false;
    }
    if (lv_roller_set_selected_str(roller, value, LV_ANIM_OFF)) {
        return true;
    }
    uint32_t option_count = lv_roller_get_option_count(roller);
    int idx = atoi(value);
    if (idx >= 0 && idx < (int)option_count) {
        lv_roller_set_selected(roller, (uint32_t)idx, LV_ANIM_OFF);
        return true;
    }
#else
    (void)roller;
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
        case YUI_VALUE_BIND_SWITCH:
        case YUI_VALUE_BIND_CHECKBOX: {
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
        case YUI_VALUE_BIND_ROLLER:
#if LV_USE_ROLLER
            (void)yui_roller_select_value(runtime->value_target, buffer);
#endif
            break;
        case YUI_VALUE_BIND_LED:
#if LV_USE_LED
            if (yui_parse_bool(buffer, false)) {
                lv_led_on(runtime->value_target);
            } else {
                char *end = NULL;
                long bright = strtol(buffer, &end, 10);
                if (end && end != buffer) {
                    if (bright < 0) {
                        bright = 0;
                    }
                    if (bright > 255) {
                        bright = 255;
                    }
                    lv_led_set_brightness(runtime->value_target, (uint8_t)bright);
                    if (bright <= 0) {
                        lv_led_off(runtime->value_target);
                    }
                } else {
                    lv_led_off(runtime->value_target);
                }
            }
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

static void yui_widget_refresh_binding_async_cb(void *user_data)
{
    yui_widget_runtime_t *runtime = (yui_widget_runtime_t *)user_data;
    if (!runtime || runtime->disposed) {
        return;
    }
    runtime->binding_refresh_pending = false;
    yui_widget_refresh_text(runtime);
    yui_widget_refresh_value(runtime);
    yui_widget_schedule_condition_refresh(runtime);
}

static void yui_widget_state_cb(const char *key, const char *value, void *user_ctx)
{
    (void)value;
    (void)key;
    yui_widget_runtime_t *runtime = (yui_widget_runtime_t *)user_ctx;
    if (!runtime || runtime->disposed) {
        return;
    }
    if (runtime->binding_refresh_pending) {
        return;
    }
    runtime->binding_refresh_pending = true;
    if (lv_async_call(yui_widget_refresh_binding_async_cb, runtime) != LV_RESULT_OK) {
        runtime->binding_refresh_pending = false;
        yui_widget_refresh_text(runtime);
        yui_widget_refresh_value(runtime);
        yui_widget_schedule_condition_refresh(runtime);
    }
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
    const char *watch_key = yui_canonicalize_state_key(key);
    if (!watch_key || watch_key[0] == '\0') {
        return ESP_OK;
    }
    yui_state_watch_handle_t handle = 0;
    esp_err_t err = yui_state_watch(watch_key, yui_widget_state_cb, runtime, &handle);
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

static esp_err_t yui_widget_bind_text(yui_widget_runtime_t *runtime, const char *text, lv_obj_t *target, bool is_translation_key)
{
    if (!runtime || !text || !target) {
        return ESP_OK;
    }
    runtime->text_target = target;
    runtime->text_template_is_translation_key = is_translation_key;
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
    if (runtime->binding_count == 0U) {
        err = yui_widget_watch_all_state(runtime);
        if (err != ESP_OK) {
            return err;
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
    if (runtime->binding_count == 0U) {
        err = yui_widget_watch_all_state(runtime);
        if (err != ESP_OK) {
            return err;
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
    if (token_count == 0U) {
        err = yui_widget_watch_all_state(runtime);
        if (err != ESP_OK) {
            return err;
        }
    }
    yui_widget_schedule_condition_refresh(runtime);
    return ESP_OK;
}

static esp_err_t yui_widget_parse_events(const yml_node_t *node, yui_widget_runtime_t *runtime)
{
    if (!node || !runtime) {
        return ESP_OK;
    }
    for (size_t i = 0; i < s_widget_event_count; ++i) {
        const yml_node_t *event_node = yui_find_event_node(node, s_widget_events[i].yaml_key, s_widget_events[i].companion_key);
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

static esp_err_t yui_widget_watch_all_state(yui_widget_runtime_t *runtime)
{
    if (!runtime || runtime->watch_all_state) {
        return ESP_OK;
    }
    yui_state_watch_handle_t handle = 0U;
    esp_err_t err = yui_state_watch(NULL, yui_widget_state_cb, runtime, &handle);
    if (err != ESP_OK || handle == 0U) {
        return err;
    }
    esp_err_t append_err = yui_widget_append_watch(runtime, handle);
    if (append_err != ESP_OK) {
        yui_state_unwatch(handle);
        return append_err;
    }
    runtime->watch_all_state = true;
    return ESP_OK;
}

static const yml_node_t *yui_find_event_node(const yml_node_t *node, const char *yaml_key, const char *companion_key)
{
    if (!node || !yaml_key) {
        return NULL;
    }

    const yml_node_t *event_node = yml_node_get_child(node, yaml_key);
    if (event_node) {
        return event_node;
    }

    const yml_node_t *events_node = yml_node_get_child(node, "events");
    if (!events_node) {
        return NULL;
    }

    event_node = yml_node_get_child(events_node, yaml_key);
    if (event_node) {
        return event_node;
    }

    if (companion_key) {
        event_node = yml_node_get_child(events_node, companion_key);
        if (event_node) {
            return event_node;
        }
    }

    return NULL;
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
    const lv_font_t *font = yui_font_pick(style->font_size, style->font_weight, style->text_font);
    if (font) {
        lv_obj_set_style_text_font(obj, font, 0);
    }
    if (style->background_color) {
        lv_obj_set_style_bg_color(obj, yui_color_from_string(style->background_color, lv_color_hex(0x101018)), 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    }
    if (style->text_color) {
        lv_obj_set_style_text_color(obj, yui_color_from_string(style->text_color, lv_color_hex(0xFFFFFF)), 0);
    }
    if (style->letter_spacing != 0) {
        lv_obj_set_style_text_letter_space(obj, style->letter_spacing, 0);
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
    const char *widget_type = yui_node_scalar(node, "type");
    const char *theme_style_name = yui_schema_get_theme_default_style(&schema->schema, widget_type);
    if (theme_style_name) {
        const yui_style_t *theme_style = yui_resolve_style(&schema->schema, theme_style_name);
        yui_apply_style(obj, theme_style);
    }
    const char *style_name = yui_node_scalar(node, "style");
    if (style_name) {
        const yui_style_t *style = yui_resolve_style(&schema->schema, style_name);
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

static bool yui_parent_flows_column(lv_obj_t *parent)
{
    if (!parent) {
        return false;
    }
    lv_flex_flow_t parent_flow = lv_obj_get_style_flex_flow(parent, LV_PART_MAIN);
    return parent_flow == LV_FLEX_FLOW_COLUMN || parent_flow == LV_FLEX_FLOW_COLUMN_WRAP;
}

static void yui_prepare_layout_container(lv_obj_t *obj)
{
    if (!obj) {
        return;
    }
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_outline_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
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
    yui_register_widget_id(instance_node, container);
    yui_prepare_layout_container(container);
    /* Size to fit content by default */
    lv_obj_set_size(container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    if (instance_node && !yui_node_has_child(instance_node, "width") && yui_parent_flows_column(parent)) {
        lv_obj_set_width(container, LV_PCT(100));
    }
    if (instance_node) {
        yui_apply_common_widget_attrs(container, instance_node, schema);
    }
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
        const char *raw_text = yui_node_scalar(node, "text");
        const char *raw_text_key = yui_node_scalar(node, "text_key");
        char text_buf[YUI_TEXT_BUFFER_MAX];
        const char *text = yui_node_resolved_localized_scalar(node, "text", "text_key", scope, text_buf, sizeof(text_buf));
        yui_widget_runtime_t *runtime = yui_widget_runtime_create(label, scope);
        if (runtime && text) {
            if (raw_text && strstr(raw_text, "{{") && strstr(raw_text, "}}")) {
                (void)yui_widget_bind_text(runtime, raw_text, label, false);
            } else if (raw_text_key && strstr(raw_text_key, "{{") && strstr(raw_text_key, "}}")) {
                (void)yui_widget_bind_text(runtime, raw_text_key, label, true);
            } else {
                lv_label_set_text(label, text);
            }
        } else if (text) {
            lv_label_set_text(label, text);
        }
        if (runtime) {
            (void)yui_widget_bind_conditions(runtime, node, label);
            (void)yui_widget_parse_events(node, runtime);
        }
        return ESP_OK;
    }
    if (strcmp(type, "camera_preview") == 0) {
        lv_obj_t *container = lv_obj_create(parent);
        yui_register_widget_id(node, container);
        lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_clip_corner(container, true, 0);
        lv_obj_set_style_pad_all(container, 0, 0);
        lv_obj_set_style_border_width(container, 0, 0);
        if (!yui_node_has_child(node, "width") && yui_parent_flows_column(parent)) {
            lv_obj_set_width(container, LV_PCT(100));
        }
        if (!yui_node_has_child(node, "height")) {
            lv_obj_set_height(container, 240);
        }
        yui_apply_common_widget_attrs(container, node, schema);

        lv_obj_t *image = lv_image_create(container);
        lv_image_set_inner_align(image, LV_IMAGE_ALIGN_CENTER);
        lv_obj_center(image);

        lv_obj_t *placeholder = lv_label_create(container);
        lv_label_set_text(placeholder, kc_touch_gui_camera_ready() ? "Starting camera preview..." : "Camera unavailable");
        const yui_style_t *placeholder_style = yui_resolve_style(&schema->schema, "body");
        if (placeholder_style) {
            yui_apply_style(placeholder, placeholder_style);
        }
        lv_obj_center(placeholder);

        yui_widget_runtime_t *runtime = yui_widget_runtime_create(container, scope);
        if (runtime) {
            (void)yui_widget_bind_conditions(runtime, node, container);
        }
        lv_obj_add_event_cb(container, yui_camera_preview_delete_cb, LV_EVENT_DELETE, NULL);

        esp_err_t preview_err = yui_camera_preview_start(container, image, placeholder);
        if (preview_err != ESP_OK) {
            yamui_log(YAMUI_LOG_LEVEL_WARN,
                      YAMUI_LOG_CAT_LVGL,
                      "camera_preview unavailable (%s)",
                      esp_err_to_name(preview_err));
            lv_label_set_text(placeholder, "Camera preview unavailable");
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
        char text_buf[YUI_TEXT_BUFFER_MAX];
        const char *raw_text = yui_node_scalar(node, "text");
        const char *raw_text_key = yui_node_scalar(node, "text_key");
        const char *text = yui_node_resolved_localized_scalar(node, "text", "text_key", scope, text_buf, sizeof(text_buf));
        lv_obj_t *btn = lv_button_create(parent);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        yui_register_widget_id(node, btn);
        bool text_is_dynamic = (raw_text && strstr(raw_text, "{{") && strstr(raw_text, "}}"))
            || (raw_text_key && strstr(raw_text_key, "{{") && strstr(raw_text_key, "}}"));
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
                if (raw_text && strstr(raw_text, "{{") && strstr(raw_text, "}}")) {
                    (void)yui_widget_bind_text(runtime, raw_text, label, false);
                } else if (raw_text_key && strstr(raw_text_key, "{{") && strstr(raw_text_key, "}}")) {
                    (void)yui_widget_bind_text(runtime, raw_text_key, label, true);
                }
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
    if (strcmp(type, "spinner") == 0) {
#if LV_USE_SPINNER
        lv_obj_t *spinner = lv_spinner_create(parent);
        yui_register_widget_id(node, spinner);
        if (!yui_node_has_child(node, "width")) {
            lv_obj_set_width(spinner, 32);
        }
        if (!yui_node_has_child(node, "height")) {
            lv_obj_set_height(spinner, 32);
        }
        yui_apply_common_widget_attrs(spinner, node, schema);
        uint32_t duration = (uint32_t)yui_node_resolved_i32(node, "duration", scope, 1000);
        uint32_t arc_sweep = (uint32_t)yui_node_resolved_i32(node, "arc_sweep", scope, 240);
        lv_spinner_set_anim_params(spinner, duration, arc_sweep);
        yui_widget_runtime_t *runtime = yui_widget_runtime_create(spinner, scope);
        if (runtime) {
            (void)yui_widget_bind_conditions(runtime, node, spinner);
            (void)yui_widget_parse_events(node, runtime);
        }
        return ESP_OK;
#else
        yamui_log(YAMUI_LOG_LEVEL_WARN, YAMUI_LOG_CAT_LVGL, "Widget type 'spinner' unavailable: LV_USE_SPINNER=0");
        return ESP_OK;
#endif
    }
    if (strcmp(type, "textarea") == 0) {
#if LV_USE_TEXTAREA
        lv_obj_t *ta = lv_textarea_create(parent);
        yui_register_widget_id(node, ta);
        yui_apply_common_widget_attrs(ta, node, schema);
        char placeholder_buf[YUI_TEXT_BUFFER_MAX];
        const char *placeholder = yui_node_resolved_localized_scalar(node, "placeholder", "placeholder_key", scope, placeholder_buf, sizeof(placeholder_buf));
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
    if (strcmp(type, "checkbox") == 0) {
#if LV_USE_CHECKBOX
        lv_obj_t *cb = lv_checkbox_create(parent);
        yui_register_widget_id(node, cb);
        yui_apply_common_widget_attrs(cb, node, schema);
        char text_buf[YUI_TEXT_BUFFER_MAX];
        const char *text = yui_node_resolved_localized_scalar(node, "text", "text_key", scope, text_buf, sizeof(text_buf));
        if (text) {
            lv_checkbox_set_text(cb, text);
        }
        if (yui_node_resolved_bool(node, "value", scope, false)) {
            lv_obj_add_state(cb, LV_STATE_CHECKED);
        }
        yui_widget_runtime_t *runtime = yui_widget_runtime_create(cb, scope);
        if (runtime) {
            const char *value_tmpl = yui_node_scalar(node, "value");
            if (value_tmpl) {
                (void)yui_widget_bind_value(runtime, value_tmpl, cb, YUI_VALUE_BIND_CHECKBOX);
            }
            (void)yui_widget_bind_conditions(runtime, node, cb);
            (void)yui_widget_parse_events(node, runtime);
        }
        return ESP_OK;
#else
        yamui_log(YAMUI_LOG_LEVEL_WARN, YAMUI_LOG_CAT_LVGL, "Widget type 'checkbox' unavailable: LV_USE_CHECKBOX=0");
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
    if (strcmp(type, "roller") == 0) {
#if LV_USE_ROLLER
        lv_obj_t *roller = lv_roller_create(parent);
        yui_register_widget_id(node, roller);
        yui_apply_common_widget_attrs(roller, node, schema);
        char *options_joined = yui_node_join_sequence_scalar(node, "options");
        const char *options = options_joined ? options_joined : yui_node_scalar(node, "options");
        if (options && options[0] != '\0') {
            const char *mode = yui_node_scalar(node, "mode");
            lv_roller_set_options(roller,
                                  options,
                                  (mode && strcasecmp(mode, "infinite") == 0) ? LV_ROLLER_MODE_INFINITE : LV_ROLLER_MODE_NORMAL);
        }
        uint32_t visible_rows = (uint32_t)yui_node_resolved_i32(node, "visible_row_count", scope, 3);
        if (visible_rows > 0U) {
            lv_roller_set_visible_row_count(roller, visible_rows);
        }
        char value_buf[YUI_TEXT_BUFFER_MAX];
        const char *value = yui_node_resolved_scalar(node, "value", scope, value_buf, sizeof(value_buf));
        if (value && value[0] != '\0') {
            (void)yui_roller_select_value(roller, value);
        }
        yui_widget_runtime_t *runtime = yui_widget_runtime_create(roller, scope);
        if (runtime) {
            const char *value_tmpl = yui_node_scalar(node, "value");
            if (value_tmpl) {
                (void)yui_widget_bind_value(runtime, value_tmpl, roller, YUI_VALUE_BIND_ROLLER);
            }
            (void)yui_widget_bind_conditions(runtime, node, roller);
            (void)yui_widget_parse_events(node, runtime);
        }
        free(options_joined);
        return ESP_OK;
#else
        yamui_log(YAMUI_LOG_LEVEL_WARN, YAMUI_LOG_CAT_LVGL, "Widget type 'roller' unavailable: LV_USE_ROLLER=0");
        return ESP_OK;
#endif
    }
    if (strcmp(type, "led") == 0) {
#if LV_USE_LED
        lv_obj_t *led = lv_led_create(parent);
        yui_register_widget_id(node, led);
        if (!yui_node_has_child(node, "width")) {
            lv_obj_set_width(led, 24);
        }
        if (!yui_node_has_child(node, "height")) {
            lv_obj_set_height(led, 24);
        }
        yui_apply_common_widget_attrs(led, node, schema);
        char color_buf[YUI_TEXT_BUFFER_MAX];
        const char *color = yui_node_resolved_scalar(node, "color", scope, color_buf, sizeof(color_buf));
        if (color && color[0] != '\0') {
            lv_led_set_color(led, yui_color_from_string(color, lv_color_hex(0x22D3EE)));
        }
        char value_buf[YUI_TEXT_BUFFER_MAX];
        const char *value = yui_node_resolved_scalar(node, "value", scope, value_buf, sizeof(value_buf));
        if (value && value[0] != '\0') {
            char *end = NULL;
            long bright = strtol(value, &end, 10);
            if (end && end != value) {
                if (bright < 0) {
                    bright = 0;
                }
                if (bright > 255) {
                    bright = 255;
                }
                lv_led_set_brightness(led, (uint8_t)bright);
                if (bright <= 0) {
                    lv_led_off(led);
                }
            } else if (yui_parse_bool(value, false)) {
                lv_led_on(led);
            } else {
                lv_led_off(led);
            }
        }
        yui_widget_runtime_t *runtime = yui_widget_runtime_create(led, scope);
        if (runtime) {
            const char *value_tmpl = yui_node_scalar(node, "value");
            if (value_tmpl) {
                (void)yui_widget_bind_value(runtime, value_tmpl, led, YUI_VALUE_BIND_LED);
            }
            (void)yui_widget_bind_conditions(runtime, node, led);
            (void)yui_widget_parse_events(node, runtime);
        }
        return ESP_OK;
#else
        yamui_log(YAMUI_LOG_LEVEL_WARN, YAMUI_LOG_CAT_LVGL, "Widget type 'led' unavailable: LV_USE_LED=0");
        return ESP_OK;
#endif
    }
    if (strcmp(type, "chart") == 0) {
#if LV_USE_CHART
        lv_obj_t *chart = lv_chart_create(parent);
        yui_register_widget_id(node, chart);
        if (!yui_node_has_child(node, "width") && yui_parent_flows_column(parent)) {
            lv_obj_set_width(chart, LV_PCT(100));
        }
        if (!yui_node_has_child(node, "height")) {
            lv_obj_set_height(chart, 180);
        }
        yui_apply_common_widget_attrs(chart, node, schema);
        lv_chart_set_type(chart, yui_chart_type_from_string(yui_node_scalar(node, "chart_type")));
        lv_chart_set_update_mode(chart, yui_chart_update_mode_from_string(yui_node_scalar(node, "update_mode")));
        lv_chart_set_point_count(chart, (uint32_t)yui_node_resolved_i32(node, "point_count", scope, 7));
        lv_chart_set_div_line_count(chart,
                                    (uint32_t)yui_node_resolved_i32(node, "horizontal_dividers", scope, 4),
                                    (uint32_t)yui_node_resolved_i32(node, "vertical_dividers", scope, 6));

        int32_t y_min = yui_node_resolved_i32(node, "min", scope, 0);
        int32_t y_max = yui_node_resolved_i32(node, "max", scope, 100);
        lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, y_min, y_max);

        const yml_node_t *series_node = yml_node_get_child(node, "series");
        if (series_node && yml_node_get_type(series_node) == YML_NODE_SEQUENCE) {
            uint32_t series_count = (uint32_t)yml_node_child_count(series_node);
            for (uint32_t i = 0; i < series_count; ++i) {
                const yml_node_t *series_item = yml_node_child_at(series_node, i);
                if (!series_item || yml_node_get_type(series_item) != YML_NODE_MAPPING) {
                    continue;
                }

                const char *color_text = yui_node_scalar(series_item, "color");
                lv_color_t color = yui_color_from_string(color_text, lv_palette_main((lv_palette_t)(LV_PALETTE_BLUE + (i % 5))));
                lv_chart_axis_t axis = yui_chart_axis_from_string(yui_node_scalar(series_item, "axis"));
                lv_chart_series_t *ser = lv_chart_add_series(chart, color, axis);
                if (!ser) {
                    continue;
                }

                const yml_node_t *values_node = yml_node_get_child(series_item, "values");
                if (values_node && yml_node_get_type(values_node) == YML_NODE_SEQUENCE) {
                    uint32_t value_count = (uint32_t)yml_node_child_count(values_node);
                    int32_t *series_values = lv_chart_get_series_y_array(chart, ser);
                    uint32_t point_count = lv_chart_get_point_count(chart);
                    if (series_values) {
                        for (uint32_t point = 0; point < point_count; ++point) {
                            series_values[point] = 0;
                        }
                        for (uint32_t point = 0; point < value_count && point < point_count; ++point) {
                            const yml_node_t *value_node = yml_node_child_at(values_node, point);
                            char value_buf[32];
                            const char *value_text = yui_format_node_text(value_node, scope, value_buf, sizeof(value_buf)) ? value_buf : NULL;
                            if (value_text && value_text[0] != '\0') {
                                series_values[point] = atoi(value_text);
                            }
                        }
                    }
                }
            }
        }

        lv_obj_set_style_bg_color(chart, lv_color_hex(0x0F172A), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(chart, lv_color_hex(0x334155), LV_PART_MAIN);
        lv_obj_set_style_border_width(chart, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(chart, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_all(chart, 8, LV_PART_MAIN);
        lv_obj_set_style_line_color(chart, lv_color_hex(0x334155), LV_PART_MAIN);
        lv_obj_set_style_line_opa(chart, LV_OPA_60, LV_PART_MAIN);
        lv_obj_set_style_text_color(chart, lv_color_hex(0xCBD5E1), LV_PART_MAIN);
        lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);
        lv_chart_refresh(chart);

        yui_widget_runtime_t *runtime = yui_widget_runtime_create(chart, scope);
        if (runtime) {
            (void)yui_widget_bind_conditions(runtime, node, chart);
            (void)yui_widget_parse_events(node, runtime);
        }
        return ESP_OK;
#else
        yamui_log(YAMUI_LOG_LEVEL_WARN, YAMUI_LOG_CAT_LVGL, "Widget type 'chart' unavailable: LV_USE_CHART=0");
        return ESP_OK;
#endif
    }
    if (strcmp(type, "calendar") == 0) {
#if LV_USE_CALENDAR
        lv_obj_t *calendar = lv_calendar_create(parent);
        yui_register_widget_id(node, calendar);
        if (!yui_node_has_child(node, "width") && yui_parent_flows_column(parent)) {
            lv_obj_set_width(calendar, LV_PCT(100));
        }
        if (!yui_node_has_child(node, "height")) {
            lv_obj_set_height(calendar, 260);
        }
        yui_apply_common_widget_attrs(calendar, node, schema);

        lv_obj_set_style_bg_color(calendar, lv_color_hex(0x0F172A), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(calendar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(calendar, lv_color_hex(0x334155), LV_PART_MAIN);
        lv_obj_set_style_border_width(calendar, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(calendar, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_all(calendar, 8, LV_PART_MAIN);
        lv_obj_set_style_text_color(calendar, lv_color_hex(0xE2E8F0), LV_PART_MAIN);
        lv_obj_set_style_bg_color(calendar, lv_color_hex(0x1E293B), LV_PART_ITEMS);
        lv_obj_set_style_bg_opa(calendar, LV_OPA_COVER, LV_PART_ITEMS);
        lv_obj_set_style_border_color(calendar, lv_color_hex(0x334155), LV_PART_ITEMS);
        lv_obj_set_style_border_width(calendar, 1, LV_PART_ITEMS);
        lv_obj_set_style_text_color(calendar, lv_color_hex(0xE2E8F0), LV_PART_ITEMS);

        lv_calendar_date_t parsed_date;
        char date_buf[32];
        const char *today_text = yui_node_resolved_scalar(node, "today", scope, date_buf, sizeof(date_buf));
        if (today_text && yui_parse_calendar_date_string(today_text, &parsed_date)) {
            lv_calendar_set_today_date(calendar, parsed_date.year, parsed_date.month, parsed_date.day);
        }

        char shown_buf[32];
        const char *shown_text = yui_node_resolved_scalar(node, "shown_month", scope, shown_buf, sizeof(shown_buf));
        if (shown_text && yui_parse_calendar_date_string(shown_text, &parsed_date)) {
            lv_calendar_set_month_shown(calendar, parsed_date.year, parsed_date.month);
        } else if (today_text && yui_parse_calendar_date_string(today_text, &parsed_date)) {
            lv_calendar_set_month_shown(calendar, parsed_date.year, parsed_date.month);
        }

        const yml_node_t *highlights_node = yml_node_get_child(node, "highlighted_dates");
        if (highlights_node && yml_node_get_type(highlights_node) == YML_NODE_SEQUENCE) {
            size_t highlight_count = yml_node_child_count(highlights_node);
            if (highlight_count > 0U) {
                yui_calendar_runtime_t *calendar_runtime = (yui_calendar_runtime_t *)calloc(1, sizeof(yui_calendar_runtime_t));
                if (calendar_runtime) {
                    calendar_runtime->highlighted_dates = (lv_calendar_date_t *)calloc(highlight_count, sizeof(lv_calendar_date_t));
                    if (calendar_runtime->highlighted_dates) {
                        size_t resolved_count = 0U;
                        for (size_t i = 0; i < highlight_count; ++i) {
                            const yml_node_t *highlight_node = yml_node_child_at(highlights_node, i);
                            char highlight_buf[32];
                            const char *highlight_text = yui_format_node_text(highlight_node, scope, highlight_buf, sizeof(highlight_buf)) ? highlight_buf : NULL;
                            if (highlight_text && yui_parse_calendar_date_string(highlight_text, &calendar_runtime->highlighted_dates[resolved_count])) {
                                resolved_count++;
                            }
                        }
                        if (resolved_count > 0U) {
                            calendar_runtime->highlighted_count = resolved_count;
                            lv_calendar_set_highlighted_dates(calendar, calendar_runtime->highlighted_dates, calendar_runtime->highlighted_count);
                            lv_obj_add_event_cb(calendar, yui_calendar_delete_cb, LV_EVENT_DELETE, calendar_runtime);
                            calendar_runtime = NULL;
                        }
                    }
                    if (calendar_runtime) {
                        free(calendar_runtime->highlighted_dates);
                        free(calendar_runtime);
                    }
                }
            }
        }

#if LV_USE_CALENDAR_HEADER_ARROW
        lv_calendar_add_header_arrow(calendar);
#elif LV_USE_CALENDAR_HEADER_DROPDOWN
        lv_calendar_add_header_dropdown(calendar);
#endif

        yui_widget_runtime_t *runtime = yui_widget_runtime_create(calendar, scope);
        if (runtime) {
            (void)yui_widget_bind_conditions(runtime, node, calendar);
            (void)yui_widget_parse_events(node, runtime);
        }
        return ESP_OK;
#else
        yamui_log(YAMUI_LOG_LEVEL_WARN, YAMUI_LOG_CAT_LVGL, "Widget type 'calendar' unavailable: LV_USE_CALENDAR=0");
        return ESP_OK;
#endif
    }
    if (strcmp(type, "menu") == 0) {
#if LV_USE_MENU
        lv_obj_t *menu = lv_menu_create(parent);
        yui_register_widget_id(node, menu);
        if (!yui_node_has_child(node, "width") && yui_parent_flows_column(parent)) {
            lv_obj_set_width(menu, LV_PCT(100));
        }
        if (!yui_node_has_child(node, "height")) {
            lv_obj_set_height(menu, 320);
        }
        yui_apply_common_widget_attrs(menu, node, schema);

        lv_menu_set_mode_header(menu, yui_menu_header_mode_from_string(yui_node_scalar(node, "header_mode")));
        lv_menu_set_mode_root_back_button(menu,
                                          yui_menu_root_back_button_mode_from_string(yui_node_scalar(node, "root_back_button")));

        lv_obj_set_style_bg_color(menu, lv_color_hex(0x0F172A), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(menu, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(menu, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(menu, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(menu, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(menu, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_opa(menu, LV_OPA_TRANSP, LV_PART_MAIN);

        lv_obj_t *main_header = lv_menu_get_main_header(menu);
        if (main_header) {
            lv_obj_set_style_bg_color(main_header, lv_color_hex(0x111827), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(main_header, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_width(main_header, 0, LV_PART_MAIN);
            lv_obj_set_style_text_color(main_header, lv_color_hex(0xE2E8F0), LV_PART_MAIN);
            lv_obj_set_style_shadow_width(main_header, 0, LV_PART_MAIN);
            lv_obj_set_style_shadow_opa(main_header, LV_OPA_TRANSP, LV_PART_MAIN);
        }

        char root_title_buf[YUI_TEXT_BUFFER_MAX];
        const char *root_title = yui_node_resolved_localized_scalar(node,
                                                                    "root_title",
                                                                    "root_title_key",
                                                                    scope,
                                                                    root_title_buf,
                                                                    sizeof(root_title_buf));
        lv_obj_t *root_page = lv_menu_page_create(menu, root_title && root_title[0] != '\0' ? root_title : NULL);
        if (root_page) {
            lv_obj_set_style_bg_color(root_page, lv_color_hex(0x0F172A), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(root_page, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_pad_all(root_page, 8, LV_PART_MAIN);
            lv_obj_set_style_shadow_width(root_page, 0, LV_PART_MAIN);
            lv_obj_set_style_shadow_opa(root_page, LV_OPA_TRANSP, LV_PART_MAIN);
        }

        lv_obj_t *root_section = root_page ? lv_menu_section_create(root_page) : NULL;
        if (root_section) {
            lv_obj_set_style_bg_opa(root_section, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(root_section, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(root_section, 0, LV_PART_MAIN);
            lv_obj_set_style_shadow_width(root_section, 0, LV_PART_MAIN);
            lv_obj_set_style_shadow_opa(root_section, LV_OPA_TRANSP, LV_PART_MAIN);
        }

        const yml_node_t *items_node = yml_node_get_child(node, "items");
        if (root_page && root_section && items_node && yml_node_get_type(items_node) == YML_NODE_SEQUENCE) {
            size_t item_count = yml_node_child_count(items_node);
            for (size_t i = 0; i < item_count; ++i) {
                const yml_node_t *item_node = yml_node_child_at(items_node, i);
                if (!item_node || yml_node_get_type(item_node) != YML_NODE_MAPPING) {
                    continue;
                }

                char item_title_buf[YUI_TEXT_BUFFER_MAX];
                const char *item_title = yui_node_resolved_localized_scalar(item_node,
                                                                            "title",
                                                                            "title_key",
                                                                            scope,
                                                                            item_title_buf,
                                                                            sizeof(item_title_buf));
                if (!item_title || item_title[0] == '\0') {
                    item_title = "Item";
                }

                lv_obj_t *dest_page = NULL;
                const yml_node_t *page_node = yml_node_get_child(item_node, "page");
                if (page_node && yml_node_get_type(page_node) == YML_NODE_MAPPING) {
                    char page_title_buf[YUI_TEXT_BUFFER_MAX];
                    const char *page_title = yui_node_resolved_localized_scalar(page_node,
                                                                                "title",
                                                                                "title_key",
                                                                                scope,
                                                                                page_title_buf,
                                                                                sizeof(page_title_buf));
                    if (!page_title || page_title[0] == '\0') {
                        page_title = item_title;
                    }

                    dest_page = lv_menu_page_create(menu, page_title);
                    if (dest_page) {
                        lv_obj_set_style_bg_color(dest_page, lv_color_hex(0x0F172A), LV_PART_MAIN);
                        lv_obj_set_style_bg_opa(dest_page, LV_OPA_COVER, LV_PART_MAIN);
                        lv_obj_set_style_pad_all(dest_page, 12, LV_PART_MAIN);
                        lv_obj_set_style_shadow_width(dest_page, 0, LV_PART_MAIN);
                        lv_obj_set_style_shadow_opa(dest_page, LV_OPA_TRANSP, LV_PART_MAIN);
                        lv_obj_t *page_section = lv_menu_section_create(dest_page);
                        if (page_section) {
                            lv_obj_set_style_bg_opa(page_section, LV_OPA_TRANSP, LV_PART_MAIN);
                            lv_obj_set_style_border_width(page_section, 0, LV_PART_MAIN);
                            lv_obj_set_style_pad_all(page_section, 0, LV_PART_MAIN);
                            lv_obj_set_style_shadow_width(page_section, 0, LV_PART_MAIN);
                            lv_obj_set_style_shadow_opa(page_section, LV_OPA_TRANSP, LV_PART_MAIN);

                            lv_obj_t *page_container = lv_menu_cont_create(page_section);
                            if (page_container) {
                                lv_obj_set_style_bg_color(page_container, lv_color_hex(0x0F172A), LV_PART_MAIN);
                                lv_obj_set_style_bg_opa(page_container, LV_OPA_COVER, LV_PART_MAIN);
                                lv_obj_set_style_border_width(page_container, 0, LV_PART_MAIN);
                                lv_obj_set_style_pad_all(page_container, 0, LV_PART_MAIN);
                                lv_obj_set_style_shadow_width(page_container, 0, LV_PART_MAIN);
                                lv_obj_set_style_shadow_opa(page_container, LV_OPA_TRANSP, LV_PART_MAIN);
                                yui_apply_layout(page_container, yml_node_get_child(page_node, "layout"), "column");
                                esp_err_t render_err = yui_render_widget_list(yml_node_get_child(page_node, "widgets"), schema, page_container, scope);
                                if (render_err != ESP_OK) {
                                    return render_err;
                                }
                            }
                        }
                    }
                }

                lv_obj_t *cont = lv_menu_cont_create(root_section);
                if (!cont) {
                    continue;
                }
                lv_obj_set_style_bg_color(cont, lv_color_hex(0x111827), LV_PART_MAIN);
                lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, LV_PART_MAIN);
                lv_obj_set_style_border_width(cont, 0, LV_PART_MAIN);
                lv_obj_set_style_radius(cont, 0, LV_PART_MAIN);
                lv_obj_set_style_pad_all(cont, 10, LV_PART_MAIN);
                lv_obj_set_style_shadow_width(cont, 0, LV_PART_MAIN);
                lv_obj_set_style_shadow_opa(cont, LV_OPA_TRANSP, LV_PART_MAIN);

                lv_obj_t *label = lv_label_create(cont);
                lv_label_set_text(label, item_title);
                lv_obj_set_style_text_color(label, lv_color_hex(0xE2E8F0), LV_PART_MAIN);

                char subtitle_buf[YUI_TEXT_BUFFER_MAX];
                const char *subtitle = yui_node_resolved_localized_scalar(item_node,
                                                                          "subtitle",
                                                                          "subtitle_key",
                                                                          scope,
                                                                          subtitle_buf,
                                                                          sizeof(subtitle_buf));
                if (subtitle && subtitle[0] != '\0') {
                    lv_obj_t *sub = lv_label_create(cont);
                    lv_label_set_text(sub, subtitle);
                    lv_obj_set_style_text_color(sub, lv_color_hex(0x94A3B8), LV_PART_MAIN);
                }

                if (dest_page) {
                    lv_menu_set_load_page_event(menu, cont, dest_page);
                }
            }
        }

        lv_menu_set_page(menu, root_page);
        yui_disable_shadows_recursive(menu);

        yui_widget_runtime_t *runtime = yui_widget_runtime_create(menu, scope);
        if (runtime) {
            (void)yui_widget_bind_conditions(runtime, node, menu);
            (void)yui_widget_parse_events(node, runtime);
        }
        return ESP_OK;
#else
        yamui_log(YAMUI_LOG_LEVEL_WARN, YAMUI_LOG_CAT_LVGL, "Widget type 'menu' unavailable: LV_USE_MENU=0");
        return ESP_OK;
#endif
    }
    if (strcmp(type, "tabview") == 0) {
#if LV_USE_TABVIEW
        lv_obj_t *tabview = lv_tabview_create(parent);
        yui_register_widget_id(node, tabview);
        if (!yui_node_has_child(node, "width") && yui_parent_flows_column(parent)) {
            lv_obj_set_width(tabview, LV_PCT(100));
        }
        if (!yui_node_has_child(node, "height")) {
            lv_obj_set_height(tabview, 280);
        }
        yui_apply_common_widget_attrs(tabview, node, schema);

        lv_tabview_set_tab_bar_position(tabview, yui_dir_from_string(yui_node_scalar(node, "tab_bar_position"), LV_DIR_TOP));
        lv_tabview_set_tab_bar_size(tabview, yui_node_resolved_i32(node, "tab_bar_size", scope, 44));

        lv_obj_set_style_bg_color(tabview, lv_color_hex(0x0F172A), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(tabview, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(tabview, lv_color_hex(0x334155), LV_PART_MAIN);
        lv_obj_set_style_border_width(tabview, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(tabview, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_all(tabview, 0, LV_PART_MAIN);

        lv_obj_t *tab_bar = lv_tabview_get_tab_bar(tabview);
        if (tab_bar) {
            lv_obj_set_style_bg_color(tab_bar, lv_color_hex(0x111827), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(tab_bar, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_width(tab_bar, 0, LV_PART_MAIN);
            lv_obj_set_style_text_color(tab_bar, lv_color_hex(0xCBD5E1), LV_PART_MAIN);
            lv_obj_set_style_bg_color(tab_bar, lv_color_hex(0x22D3EE), LV_PART_ITEMS | LV_STATE_CHECKED);
            lv_obj_set_style_text_color(tab_bar, lv_color_hex(0x0F172A), LV_PART_ITEMS | LV_STATE_CHECKED);
        }

        lv_obj_t *content = lv_tabview_get_content(tabview);
        if (content) {
            lv_obj_set_style_bg_color(content, lv_color_hex(0x0F172A), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(content, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_width(content, 0, LV_PART_MAIN);
        }

        const yml_node_t *tabs_node = yml_node_get_child(node, "tabs");
        if (tabs_node && yml_node_get_type(tabs_node) == YML_NODE_SEQUENCE) {
            size_t tab_count = yml_node_child_count(tabs_node);
            for (size_t i = 0; i < tab_count; ++i) {
                const yml_node_t *tab_node = yml_node_child_at(tabs_node, i);
                if (!tab_node || yml_node_get_type(tab_node) != YML_NODE_MAPPING) {
                    continue;
                }

                char title_buf[YUI_TEXT_BUFFER_MAX];
                const char *tab_title = yui_node_resolved_localized_scalar(tab_node, "title", "title_key", scope, title_buf, sizeof(title_buf));
                if (!tab_title || tab_title[0] == '\0') {
                    tab_title = "Tab";
                }

                lv_obj_t *tab = lv_tabview_add_tab(tabview, tab_title);
                lv_obj_set_style_bg_color(tab, lv_color_hex(0x0F172A), LV_PART_MAIN);
                lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, LV_PART_MAIN);
                lv_obj_set_style_pad_all(tab, 12, LV_PART_MAIN);
                yui_apply_layout(tab, yml_node_get_child(tab_node, "layout"), "column");
                esp_err_t render_err = yui_render_widget_list(yml_node_get_child(tab_node, "widgets"), schema, tab, scope);
                if (render_err != ESP_OK) {
                    return render_err;
                }
            }
        }

        lv_tabview_set_active(tabview, (uint32_t)yui_node_resolved_i32(node, "active_tab", scope, 0), LV_ANIM_OFF);

        yui_widget_runtime_t *runtime = yui_widget_runtime_create(tabview, scope);
        if (runtime) {
            (void)yui_widget_bind_conditions(runtime, node, tabview);
            (void)yui_widget_parse_events(node, runtime);
        }
        return ESP_OK;
#else
        yamui_log(YAMUI_LOG_LEVEL_WARN, YAMUI_LOG_CAT_LVGL, "Widget type 'tabview' unavailable: LV_USE_TABVIEW=0");
        return ESP_OK;
#endif
    }
    if (strcmp(type, "table") == 0) {
#if LV_USE_TABLE
        lv_obj_t *table = lv_table_create(parent);
        yui_register_widget_id(node, table);
        if (!yui_node_has_child(node, "width") && yui_parent_flows_column(parent)) {
            lv_obj_set_width(table, LV_PCT(100));
        }
        if (!yui_node_has_child(node, "height")) {
            lv_obj_set_height(table, 160);
        }
        yui_apply_common_widget_attrs(table, node, schema);
        lv_obj_clear_flag(table, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(table, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(table, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
        lv_obj_remove_flag(table, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
        lv_obj_remove_flag(table, LV_OBJ_FLAG_SCROLL_ELASTIC);
        lv_obj_remove_flag(table, LV_OBJ_FLAG_SCROLL_MOMENTUM);
        lv_obj_set_style_bg_color(table, lv_color_hex(0x0F172A), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(table, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(table, lv_color_hex(0x334155), LV_PART_MAIN);
        lv_obj_set_style_border_width(table, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(table, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_all(table, 6, LV_PART_MAIN);
        lv_obj_set_style_bg_color(table, lv_color_hex(0x0F172A), LV_PART_ITEMS);
        lv_obj_set_style_bg_opa(table, LV_OPA_COVER, LV_PART_ITEMS);
        lv_obj_set_style_text_color(table, lv_color_hex(0xE2E8F0), LV_PART_ITEMS);
        lv_obj_set_style_border_color(table, lv_color_hex(0x334155), LV_PART_ITEMS);
        lv_obj_set_style_border_width(table, 1, LV_PART_ITEMS);

        const yml_node_t *widths_node = yml_node_get_child(node, "column_widths");
        uint32_t max_cols = 0U;
        if (widths_node && yml_node_get_type(widths_node) == YML_NODE_SEQUENCE) {
            max_cols = (uint32_t)yml_node_child_count(widths_node);
        }

        if (widths_node && yml_node_get_type(widths_node) == YML_NODE_SEQUENCE) {
            uint32_t width_count = (uint32_t)yml_node_child_count(widths_node);
            max_cols = width_count > max_cols ? width_count : max_cols;
        }

        const yml_node_t *rows_node = yml_node_get_child(node, "rows");
        if (rows_node && yml_node_get_type(rows_node) == YML_NODE_SEQUENCE) {
            uint32_t row_count = (uint32_t)yml_node_child_count(rows_node);
            lv_table_set_row_count(table, row_count);
            for (uint32_t row = 0; row < row_count; ++row) {
                const yml_node_t *row_node = yml_node_child_at(rows_node, row);
                uint32_t col_count = yui_table_row_column_count(row_node);
                if (col_count > max_cols) {
                    max_cols = col_count;
                }
            }

            if (max_cols > 0U) {
                lv_table_set_column_count(table, max_cols);
            }
            if (widths_node && yml_node_get_type(widths_node) == YML_NODE_SEQUENCE) {
                uint32_t width_count = (uint32_t)yml_node_child_count(widths_node);
                for (uint32_t i = 0; i < width_count; ++i) {
                    const yml_node_t *width_node = yml_node_child_at(widths_node, i);
                    const char *width_text = width_node ? yml_node_get_scalar(width_node) : NULL;
                    if (width_text && width_text[0] != '\0') {
                        lv_table_set_column_width(table, i, atoi(width_text));
                    }
                }
            }

            for (uint32_t row = 0; row < row_count; ++row) {
                const yml_node_t *row_node = yml_node_child_at(rows_node, row);
                uint32_t col_count = yui_table_row_column_count(row_node);
                for (uint32_t col = 0; col < col_count; ++col) {
                    const yml_node_t *cell_node = yui_table_row_cell_at(row_node, col);
                    char cell_buf[YUI_TEXT_BUFFER_MAX];
                    const char *cell_text = yui_format_node_text(cell_node, scope, cell_buf, sizeof(cell_buf)) ? cell_buf : "";
                    lv_table_set_cell_value(table, row, col, cell_text ? cell_text : "");
                }
            }
        }

        yui_widget_runtime_t *runtime = yui_widget_runtime_create(table, scope);
        if (runtime) {
            (void)yui_widget_bind_conditions(runtime, node, table);
            (void)yui_widget_parse_events(node, runtime);
        }
        return ESP_OK;
#else
        yamui_log(YAMUI_LOG_LEVEL_WARN, YAMUI_LOG_CAT_LVGL, "Widget type 'table' unavailable: LV_USE_TABLE=0");
        return ESP_OK;
#endif
    }
    if (strcmp(type, "keyboard") == 0) {
#if LV_USE_KEYBOARD
        bool overlay = yui_node_resolved_bool(node, "overlay", scope, false);
        lv_obj_t *kb_parent = overlay ? lv_obj_get_screen(parent) : parent;
        lv_obj_t *kb = lv_keyboard_create(kb_parent);
        yui_register_widget_id(node, kb);
        yui_apply_common_widget_attrs(kb, node, schema);
        if (overlay) {
            if (!yui_node_has_child(node, "width")) {
                lv_obj_set_width(kb, LV_PCT(100));
            }
            if (!yui_node_has_child(node, "height")) {
                lv_obj_set_height(kb, 300);
            }
            lv_obj_add_flag(kb, LV_OBJ_FLAG_FLOATING);
            lv_obj_add_flag(kb, LV_OBJ_FLAG_IGNORE_LAYOUT);
            lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
            lv_obj_move_foreground(kb);
        }
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
        yui_prepare_layout_container(container);
        /* Size to fit content by default */
        lv_obj_set_size(container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        if (!yui_node_has_child(node, "width") && yui_parent_flows_column(parent)) {
            lv_obj_set_width(container, LV_PCT(100));
        }
        yui_apply_layout(container, yml_node_get_child(node, "layout"), type);
        yui_apply_common_widget_attrs(container, node, schema);
        yui_widget_runtime_t *runtime = yui_widget_runtime_create(container, scope);
        if (runtime) {
            (void)yui_widget_bind_conditions(runtime, node, container);
        }
        return yui_render_widget_list(yml_node_get_child(node, "widgets"), schema, container, scope);
    }
    if (strcmp(type, "list") == 0) {
#if LV_USE_LIST
        lv_obj_t *list = lv_list_create(parent);
        yui_register_widget_id(node, list);
        lv_obj_set_size(list, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_scroll_dir(list, LV_DIR_VER);
        lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
        lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLL_ELASTIC);
        lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
        lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
        lv_obj_set_style_outline_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_shadow_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_radius(list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
        if (!yui_node_has_child(node, "width") && yui_parent_flows_column(parent)) {
            lv_obj_set_width(list, LV_PCT(100));
        }
        if (!yui_node_has_child(node, "height")) {
            lv_obj_set_height(list, LV_SIZE_CONTENT);
        }
        yui_apply_layout(list, yml_node_get_child(node, "layout"), "column");
        yui_apply_common_widget_attrs(list, node, schema);
        yui_widget_runtime_t *runtime = yui_widget_runtime_create(list, scope);
        if (runtime) {
            (void)yui_widget_bind_conditions(runtime, node, list);
        }
        return yui_render_widget_list(yml_node_get_child(node, "widgets"), schema, list, scope);
#else
        yamui_log(YAMUI_LOG_LEVEL_WARN, YAMUI_LOG_CAT_LVGL, "Widget type 'list' unavailable: LV_USE_LIST=0");
        return ESP_OK;
#endif
    }
    if (strcmp(type, "panel") == 0) {
        lv_obj_t *panel = lv_obj_create(parent);
        yui_register_widget_id(node, panel);
        lv_obj_set_size(panel, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
        if (!yui_node_has_child(node, "width") && yui_parent_flows_column(parent)) {
            lv_obj_set_width(panel, LV_PCT(100));
        }
        yui_apply_common_widget_attrs(panel, node, schema);
        yui_widget_runtime_t *runtime = yui_widget_runtime_create(panel, scope);
        if (runtime) {
            (void)yui_widget_bind_conditions(runtime, node, panel);
        }
        const yml_node_t *layout = yml_node_get_child(node, "layout");
        if (layout) {
            yui_apply_layout(panel, layout, "column");
        }

        const yml_node_t *props_node = yml_node_get_child(node, "props");
        char title_buf[YUI_TEXT_BUFFER_MAX];
        const char *title = yui_node_resolved_localized_scalar(node, "title", "title_key", scope, title_buf, sizeof(title_buf));
        if ((!title || title[0] == '\0') && props_node) {
            title = yui_node_resolved_localized_scalar(props_node, "title", "title_key", scope, title_buf, sizeof(title_buf));
            if ((!title || title[0] == '\0')) {
                title = yui_node_resolved_localized_scalar(props_node, "title", "titleKey", scope, title_buf, sizeof(title_buf));
            }
        }
        if (title && title[0] != '\0') {
            lv_obj_t *label = lv_label_create(panel);
            const yui_style_t *title_style = yui_resolve_style(&schema->schema, "stat-label");
            if (!title_style) {
                title_style = yui_resolve_style(&schema->schema, "heading");
            }
            if (title_style) {
                yui_apply_style(label, title_style);
            }
            lv_label_set_text(label, title);
        }

        return yui_render_widget_list(yml_node_get_child(node, "widgets"), schema, panel, scope);
    }
    if (strcmp(type, "spacer") == 0) {
        lv_obj_t *spacer = lv_obj_create(parent);
        yui_register_widget_id(node, spacer);
        lv_obj_remove_style_all(spacer);
        lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(spacer, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
        yui_apply_common_widget_attrs(spacer, node, schema);

        lv_coord_t width = 0;
        lv_coord_t height = 0;
        bool has_width = yui_node_parse_size(node, "width", &width);
        bool has_height = yui_node_parse_size(node, "height", &height);
        int32_t size = yui_node_i32(node, "size", 8);
        if (size < 0) {
            size = 0;
        }

        if (!has_width && !has_height) {
            lv_flex_flow_t parent_flow = lv_obj_get_style_flex_flow(parent, 0);
            if (parent_flow == LV_FLEX_FLOW_ROW || parent_flow == LV_FLEX_FLOW_ROW_WRAP) {
                lv_obj_set_size(spacer, (lv_coord_t)size, 1);
            } else {
                lv_obj_set_size(spacer, 1, (lv_coord_t)size);
            }
        } else {
            if (!has_width) {
                width = 1;
            }
            if (!has_height) {
                height = 1;
            }
            lv_obj_set_size(spacer, width, height);
        }

        yui_widget_runtime_t *runtime = yui_widget_runtime_create(spacer, scope);
        if (runtime) {
            (void)yui_widget_bind_conditions(runtime, node, spacer);
        }
        return ESP_OK;
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
    lv_obj_set_style_bg_color(root, yui_theme_screen_bg_color(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(root, yui_font_default(), 0);

    lv_obj_add_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(root, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(root, LV_SCROLLBAR_MODE_AUTO);
    yui_apply_layout(root, yml_node_get_child(screen_node, "layout"), "column");

    const yml_node_t *widgets = yml_node_get_child(screen_node, "widgets");
    esp_err_t err = yui_render_widget_list(widgets, schema, root, NULL);
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
        case YUI_NAV_REQUEST_REFRESH:
            return yui_navigation_render_current();
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

static void yui_native_fn_async_reset(int argc, const char **argv)
{
    if (argc <= 0 || !argv || !argv[0]) {
        return;
    }
    const char *message = (argc > 1 && argv[1]) ? argv[1] : NULL;
    (void)yamui_async_reset(argv[0], message);
}

static void yui_native_fn_async_begin(int argc, const char **argv)
{
    if (argc <= 0 || !argv || !argv[0]) {
        return;
    }
    const char *message = (argc > 1 && argv[1]) ? argv[1] : NULL;
    (void)yamui_async_begin(argv[0], message);
}

static void yui_native_fn_async_progress(int argc, const char **argv)
{
    if (argc <= 0 || !argv || !argv[0]) {
        return;
    }
    int32_t progress = 0;
    if (argc > 1 && argv[1]) {
        progress = (int32_t)strtol(argv[1], NULL, 10);
    }
    const char *message = (argc > 2 && argv[2]) ? argv[2] : NULL;
    (void)yamui_async_progress(argv[0], progress, message);
}

static void yui_native_fn_async_complete(int argc, const char **argv)
{
    if (argc <= 0 || !argv || !argv[0]) {
        return;
    }
    const char *message = (argc > 1 && argv[1]) ? argv[1] : NULL;
    (void)yamui_async_complete(argv[0], message);
}

static void yui_native_fn_async_fail(int argc, const char **argv)
{
    if (argc <= 0 || !argv || !argv[0]) {
        return;
    }
    const char *message = (argc > 1 && argv[1]) ? argv[1] : NULL;
    (void)yamui_async_fail(argv[0], message);
}

static void yui_register_builtin_natives(void)
{
    yamui_runtime_register_function("ui_goto", yui_native_fn_goto);
    yamui_runtime_register_function("ui_push", yui_native_fn_push);
    yamui_runtime_register_function("ui_pop", yui_native_fn_pop);
    yamui_runtime_register_function("ui_async_reset", yui_native_fn_async_reset);
    yamui_runtime_register_function("ui_async_begin", yui_native_fn_async_begin);
    yamui_runtime_register_function("ui_async_progress", yui_native_fn_async_progress);
    yamui_runtime_register_function("ui_async_complete", yui_native_fn_async_complete);
    yamui_runtime_register_function("ui_async_fail", yui_native_fn_async_fail);
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
    esp_err_t err = yui_register_display_watchers();
    if (err != ESP_OK) {
        return err;
    }
    err = yui_register_theme_watchers();
    if (err != ESP_OK) {
        return err;
    }
    return yui_register_locale_watchers();
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




