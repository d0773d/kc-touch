#include "yaml_ui.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "yaml_ui";

static char *yui_strdup(const char *src)
{
    if (!src) {
        return NULL;
    }
    size_t len = strlen(src);
    char *out = (char *)malloc(len + 1U);
    if (!out) {
        return NULL;
    }
    memcpy(out, src, len + 1U);
    return out;
}

static uint8_t yui_read_u8(const yml_node_t *node, const char *key, uint8_t def)
{
    const yml_node_t *child = yml_node_get_child(node, key);
    if (!child) {
        return def;
    }
    const char *scalar = yml_node_get_scalar(child);
    if (!scalar) {
        return def;
    }
    int value = atoi(scalar);
    if (value < 0) {
        value = 0;
    } else if (value > 255) {
        value = 255;
    }
    return (uint8_t)value;
}

static int32_t yui_read_i32(const yml_node_t *node, const char *key, int32_t def)
{
    const yml_node_t *child = yml_node_get_child(node, key);
    if (!child) {
        return def;
    }
    const char *scalar = yml_node_get_scalar(child);
    if (!scalar) {
        return def;
    }
    return atoi(scalar);
}

static char *yui_read_string(const yml_node_t *node, const char *key)
{
    const yml_node_t *child = yml_node_get_child(node, key);
    if (!child) {
        return NULL;
    }
    const char *scalar = yml_node_get_scalar(child);
    if (!scalar) {
        return NULL;
    }
    return yui_strdup(scalar);
}

static bool yui_widget_type_from_string(const char *type, yui_widget_type_t *out)
{
    if (!type || !out) {
        return false;
    }
    if (strcmp(type, "label") == 0) {
        *out = YUI_WIDGET_LABEL;
        return true;
    }
    ESP_LOGW(TAG, "Unsupported widget type '%s'", type);
    return false;
}

static esp_err_t yui_parse_widgets(const yml_node_t *widgets_node, yui_template_t *tpl)
{
    if (!widgets_node || yml_node_get_type(widgets_node) != YML_NODE_SEQUENCE) {
        ESP_LOGE(TAG, "Template '%s' is missing a widgets sequence", tpl->name);
        return ESP_ERR_INVALID_ARG;
    }
    size_t count = yml_node_child_count(widgets_node);
    if (count == 0) {
        ESP_LOGE(TAG, "Template '%s' has an empty widget list", tpl->name);
        return ESP_ERR_INVALID_ARG;
    }
    tpl->widgets = (yui_widget_t *)calloc(count, sizeof(yui_widget_t));
    if (!tpl->widgets) {
        return ESP_ERR_NO_MEM;
    }
    tpl->widget_count = count;
    size_t idx = 0;
    for (const yml_node_t *child = yml_node_child_at(widgets_node, 0); child; child = yml_node_next(child)) {
        if (yml_node_get_type(child) != YML_NODE_MAPPING) {
            ESP_LOGE(TAG, "Widget entry %u in template '%s' must be a mapping", (unsigned)idx, tpl->name);
            return ESP_ERR_INVALID_ARG;
        }
        yui_widget_t *widget = &tpl->widgets[idx++];
        char *type_str = yui_read_string(child, "type");
        if (!type_str || !yui_widget_type_from_string(type_str, &widget->type)) {
            free(type_str);
            return ESP_ERR_INVALID_ARG;
        }
        free(type_str);
        widget->text = yui_read_string(child, "text");
        if (!widget->text) {
            ESP_LOGE(TAG, "Widget entry %u in template '%s' missing text", (unsigned)(idx - 1), tpl->name);
            return ESP_ERR_INVALID_ARG;
        }
        widget->variant = yui_read_string(child, "variant");
    }
    return ESP_OK;
}

static esp_err_t yui_parse_templates(const yml_node_t *templates_node, yui_schema_t *schema)
{
    if (!templates_node || yml_node_get_type(templates_node) != YML_NODE_MAPPING) {
        ESP_LOGE(TAG, "sensor_templates must be a mapping");
        return ESP_ERR_INVALID_ARG;
    }
    size_t count = yml_node_child_count(templates_node);
    if (count == 0) {
        ESP_LOGE(TAG, "sensor_templates block is empty");
        return ESP_ERR_INVALID_ARG;
    }
    schema->templates = (yui_template_t *)calloc(count, sizeof(yui_template_t));
    if (!schema->templates) {
        return ESP_ERR_NO_MEM;
    }
    schema->template_count = count;

    size_t idx = 0;
    for (const yml_node_t *child = yml_node_child_at(templates_node, 0); child; child = yml_node_next(child)) {
        yui_template_t *tpl = &schema->templates[idx++];
        tpl->name = yui_strdup(yml_node_get_key(child));
        if (!tpl->name) {
            return ESP_ERR_NO_MEM;
        }
        tpl->title = yui_read_string(child, "title");
        tpl->subtitle = yui_read_string(child, "subtitle");
        tpl->style = yui_read_string(child, "style");
        const yml_node_t *widgets = yml_node_get_child(child, "widgets");
        esp_err_t err = yui_parse_widgets(widgets, tpl);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

static esp_err_t yui_parse_styles(const yml_node_t *styles_node, yui_schema_t *schema)
{
    if (!styles_node) {
        return ESP_OK;
    }
    if (yml_node_get_type(styles_node) != YML_NODE_MAPPING) {
        ESP_LOGE(TAG, "styles block must be a mapping");
        return ESP_ERR_INVALID_ARG;
    }
    size_t count = yml_node_child_count(styles_node);
    if (count == 0) {
        return ESP_OK;
    }
    schema->styles = (yui_style_t *)calloc(count, sizeof(yui_style_t));
    if (!schema->styles) {
        return ESP_ERR_NO_MEM;
    }
    schema->style_count = count;

    size_t idx = 0;
    for (const yml_node_t *child = yml_node_child_at(styles_node, 0); child; child = yml_node_next(child)) {
        if (yml_node_get_type(child) != YML_NODE_MAPPING) {
            ESP_LOGE(TAG, "Style '%s' must be a mapping", yml_node_get_key(child));
            return ESP_ERR_INVALID_ARG;
        }
        yui_style_t *style = &schema->styles[idx++];
        style->name = yui_strdup(yml_node_get_key(child));
        if (!style->name) {
            return ESP_ERR_NO_MEM;
        }
        style->background_color = yui_read_string(child, "bg_color");
        style->text_color = yui_read_string(child, "text_color");
        style->accent_color = yui_read_string(child, "accent_color");
        style->radius = yui_read_i32(child, "radius", 16);
        style->padding = yui_read_i32(child, "padding", 12);
    }
    return ESP_OK;
}

static void yui_apply_layout_defaults(yui_layout_t *layout)
{
    layout->columns = 2;
    layout->h_spacing = 16;
    layout->v_spacing = 16;
    layout->padding = 18;
    layout->background_color = yui_strdup("#0F0F18");
}

static esp_err_t yui_parse_layout(const yml_node_t *layout_node, yui_layout_t *layout)
{
    if (!layout_node) {
        return ESP_OK;
    }
    if (yml_node_get_type(layout_node) != YML_NODE_MAPPING) {
        ESP_LOGE(TAG, "layout block must be a mapping");
        return ESP_ERR_INVALID_ARG;
    }
    layout->columns = yui_read_u8(layout_node, "columns", layout->columns);
    layout->h_spacing = yui_read_u8(layout_node, "h_spacing", layout->h_spacing);
    layout->v_spacing = yui_read_u8(layout_node, "v_spacing", layout->v_spacing);
    layout->padding = yui_read_u8(layout_node, "padding", layout->padding);
    char *bg = yui_read_string(layout_node, "background_color");
    if (bg) {
        free(layout->background_color);
        layout->background_color = bg;
    }
    return ESP_OK;
}

static void yui_free_widgets(yui_widget_t *widgets, size_t count)
{
    if (!widgets) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        free(widgets[i].text);
        free(widgets[i].variant);
    }
    free(widgets);
}

static void yui_free_templates(yui_template_t *templates, size_t count)
{
    if (!templates) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        free(templates[i].name);
        free(templates[i].title);
        free(templates[i].subtitle);
        free(templates[i].style);
        yui_free_widgets(templates[i].widgets, templates[i].widget_count);
    }
    free(templates);
}

static void yui_free_styles(yui_style_t *styles, size_t count)
{
    if (!styles) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        free(styles[i].name);
        free(styles[i].background_color);
        free(styles[i].text_color);
        free(styles[i].accent_color);
    }
    free(styles);
}

esp_err_t yui_schema_from_tree(const yml_node_t *root, yui_schema_t *out_schema)
{
    if (!root || !out_schema) {
        return ESP_ERR_INVALID_ARG;
    }
    if (yml_node_get_type(root) != YML_NODE_MAPPING) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_schema, 0, sizeof(*out_schema));
    yui_apply_layout_defaults(&out_schema->layout);

    const yml_node_t *layout = yml_node_get_child(root, "layout");
    esp_err_t err = yui_parse_layout(layout, &out_schema->layout);
    if (err != ESP_OK) {
        yui_schema_free(out_schema);
        return err;
    }

    const yml_node_t *styles_node = yml_node_get_child(root, "styles");
    err = yui_parse_styles(styles_node, out_schema);
    if (err != ESP_OK) {
        yui_schema_free(out_schema);
        return err;
    }

    const yml_node_t *templates_node = yml_node_get_child(root, "sensor_templates");
    if (!templates_node) {
        ESP_LOGE(TAG, "YAML schema missing sensor_templates block");
        yui_schema_free(out_schema);
        return ESP_ERR_INVALID_ARG;
    }
    err = yui_parse_templates(templates_node, out_schema);
    if (err != ESP_OK) {
        yui_schema_free(out_schema);
        return err;
    }

    return ESP_OK;
}

void yui_schema_free(yui_schema_t *schema)
{
    if (!schema) {
        return;
    }
    free(schema->layout.background_color);
    schema->layout.background_color = NULL;
    yui_free_styles(schema->styles, schema->style_count);
    schema->styles = NULL;
    schema->style_count = 0;
    yui_free_templates(schema->templates, schema->template_count);
    schema->templates = NULL;
    schema->template_count = 0;
}

const yui_template_t *yui_schema_get_template(const yui_schema_t *schema, const char *name)
{
    if (!schema || !name) {
        return NULL;
    }
    for (size_t i = 0; i < schema->template_count; ++i) {
        if (schema->templates[i].name && strcmp(schema->templates[i].name, name) == 0) {
            return &schema->templates[i];
        }
    }
    return NULL;
}

const yui_style_t *yui_schema_get_style(const yui_schema_t *schema, const char *name)
{
    if (!schema || !name) {
        return NULL;
    }
    for (size_t i = 0; i < schema->style_count; ++i) {
        if (schema->styles[i].name && strcmp(schema->styles[i].name, name) == 0) {
            return &schema->styles[i];
        }
    }
    return NULL;
}
