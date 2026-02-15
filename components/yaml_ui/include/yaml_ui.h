#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "yaml_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    YUI_WIDGET_LABEL = 0,
} yui_widget_type_t;

typedef struct {
    yui_widget_type_t type;
    char *text;
    char *variant;
} yui_widget_t;

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
} yui_schema_t;

esp_err_t yui_schema_from_tree(const yml_node_t *root, yui_schema_t *out_schema);
void yui_schema_free(yui_schema_t *schema);
const yui_template_t *yui_schema_get_template(const yui_schema_t *schema, const char *name);
const yui_style_t *yui_schema_get_style(const yui_schema_t *schema, const char *name);

#ifdef __cplusplus
}
#endif
