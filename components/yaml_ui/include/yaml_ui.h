#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "yaml_core.h"
#include "yamui_events.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *name;
    char *value;
} yui_kv_pair_t;

typedef enum {
    YUI_WIDGET_EVENT_CLICK = 0,
    YUI_WIDGET_EVENT_PRESS,
    YUI_WIDGET_EVENT_RELEASE,
    YUI_WIDGET_EVENT_CHANGE,
    YUI_WIDGET_EVENT_FOCUS,
    YUI_WIDGET_EVENT_BLUR,
    YUI_WIDGET_EVENT_LOAD,
    YUI_WIDGET_EVENT_COUNT,
    YUI_WIDGET_EVENT_INVALID = -1,
} yui_widget_event_type_t;

typedef struct {
    yui_action_list_t lists[YUI_WIDGET_EVENT_COUNT];
} yui_widget_events_t;

typedef struct {
    char *name;
    char **props;
    size_t prop_count;
    const yml_node_t *layout_node;
    const yml_node_t *widgets_node;
} yui_component_def_t;

typedef struct {
    char *name;
    char *background_color;
    char *text_color;
    char *accent_color;
    char *text_font;
    int32_t width;
    int32_t height;
    int32_t padding;
    int32_t padding_x;
    int32_t padding_y;
    int32_t radius;
    int32_t spacing;
    bool shadow;
    char *align;
} yui_style_t;

typedef struct {
    char *initial_screen;
    char *locale;
} yui_app_config_t;

typedef struct {
    yml_node_t *root;
    const yml_node_t *app_node;
    const yml_node_t *state_node;
    const yml_node_t *styles_node;
    const yml_node_t *components_node;
    const yml_node_t *screens_node;
    yui_app_config_t app;
    yui_style_t *styles;
    size_t style_count;
    yui_component_def_t *components;
    size_t component_count;
} yui_schema_t;

esp_err_t yui_schema_from_tree(const yml_node_t *root, yui_schema_t *out_schema);
void yui_schema_free(yui_schema_t *schema);
const yml_node_t *yui_schema_get_screen(const yui_schema_t *schema, const char *name);
const yui_component_def_t *yui_schema_get_component(const yui_schema_t *schema, const char *name);
const yui_style_t *yui_schema_get_style(const yui_schema_t *schema, const char *name);
const char *yui_schema_default_screen(const yui_schema_t *schema);
const char *yui_schema_locale(const yui_schema_t *schema);

#ifdef __cplusplus
}
#endif
