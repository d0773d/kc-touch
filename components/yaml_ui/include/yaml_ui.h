#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "yaml_core.h"
#include "yamui_events.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    YUI_WIDGET_LABEL = 0,
    YUI_WIDGET_BUTTON,
    YUI_WIDGET_SPACER,
} yui_widget_type_t;

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
    yui_widget_type_t type;
    char *text;
    char *variant;
    int32_t size;
    yui_widget_events_t events;
    char **state_bindings;
    size_t state_binding_count;
} yui_widget_t;

typedef enum {
    YUI_COMPONENT_FLOW_COLUMN = 0,
    YUI_COMPONENT_FLOW_ROW,
} yui_component_flow_t;

typedef enum {
    YUI_COMPONENT_ALIGN_START = 0,
    YUI_COMPONENT_ALIGN_CENTER,
    YUI_COMPONENT_ALIGN_END,
    YUI_COMPONENT_ALIGN_STRETCH,
} yui_component_align_t;

typedef struct {
    yui_component_flow_t flow;
    yui_component_align_t main_align;
    yui_component_align_t cross_align;
    uint8_t gap;
    uint8_t padding;
    char *background_color;
} yui_component_layout_t;

typedef struct {
    char *name;
    yui_component_layout_t layout;
    yui_widget_t *widgets;
    size_t widget_count;
} yui_component_t;

typedef struct {
    char *name;
    char *title;
    char *subtitle;
    char *style;
    yui_widget_t *widgets;
    size_t widget_count;
} yui_template_t;

typedef struct {
    char *name;
    char *background_color;
    char *text_color;
    char *accent_color;
    int32_t radius;
    int32_t padding;
} yui_style_t;

typedef struct {
    uint8_t columns;
    uint8_t h_spacing;
    uint8_t v_spacing;
    uint8_t padding;
    char *background_color;
} yui_layout_t;

typedef struct {
    yui_layout_t layout;
    yui_style_t *styles;
    size_t style_count;
    yui_template_t *templates;
    size_t template_count;
    yui_component_t *components;
    size_t component_count;
} yui_schema_t;

esp_err_t yui_schema_from_tree(const yml_node_t *root, yui_schema_t *out_schema);
void yui_schema_free(yui_schema_t *schema);
const yui_template_t *yui_schema_get_template(const yui_schema_t *schema, const char *name);
const yui_style_t *yui_schema_get_style(const yui_schema_t *schema, const char *name);
const yui_component_t *yui_schema_get_component(const yui_schema_t *schema, const char *name);

#ifdef __cplusplus
}
#endif
