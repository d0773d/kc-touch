#include "yaml_ui.h"
#include "yamui_state.h"
#include "yamui_events.h"
#include "yamui_expr.h"
#include "yamui_logging.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>


typedef struct {
    const char *yaml_key;
    yui_widget_event_type_t event_type;
} yui_widget_event_field_t;

typedef struct {
    yui_widget_t *widget;
} yui_binding_ctx_t;

static char *yui_strdup(const char *src);
static bool yui_is_valid_state_token(const char *token);
static esp_err_t yui_widget_add_state_binding(yui_widget_t *widget, char *token);
static void yui_free_widgets(yui_widget_t *widgets, size_t count);
static void yui_free_components(yui_component_t *components, size_t count);
static void yui_component_layout_defaults(yui_component_layout_t *layout);
static esp_err_t yui_parse_component_layout(const yml_node_t *node, yui_component_layout_t *layout);

static void yui_binding_collect_cb(const char *identifier, void *user_ctx)
{
    yui_binding_ctx_t *ctx = (yui_binding_ctx_t *)user_ctx;
    if (!ctx || !ctx->widget || !identifier) {
        return;
    }
    if (!yui_is_valid_state_token(identifier)) {
        return;
    }
    char *copy = yui_strdup(identifier);
    if (!copy) {
        yamui_log(YAMUI_LOG_LEVEL_WARN, YAMUI_LOG_CAT_PARSER, "Failed to allocate binding for '%s'", identifier);
        return;
    }
    esp_err_t err = yui_widget_add_state_binding(ctx->widget, copy);
    if (err != ESP_OK) {
        yamui_log(YAMUI_LOG_LEVEL_WARN, YAMUI_LOG_CAT_PARSER, "Failed to track binding '%s' (%s)", identifier, esp_err_to_name(err));
    }
}

static const yui_widget_event_field_t s_widget_event_fields[] = {
    {"on_click", YUI_WIDGET_EVENT_CLICK},
    {"on_press", YUI_WIDGET_EVENT_PRESS},
    {"on_release", YUI_WIDGET_EVENT_RELEASE},
    {"on_change", YUI_WIDGET_EVENT_CHANGE},
    {"on_focus", YUI_WIDGET_EVENT_FOCUS},
    {"on_blur", YUI_WIDGET_EVENT_BLUR},
    {"on_load", YUI_WIDGET_EVENT_LOAD},
};
static const size_t s_widget_event_field_count = sizeof(s_widget_event_fields) / sizeof(s_widget_event_fields[0]);

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

static bool yui_is_valid_state_token(const char *token)
{
    if (!token || token[0] == '\0') {
        return false;
    }
    if (strncmp(token, "sensor.", 7) == 0) {
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

static bool yui_widget_binding_exists(const yui_widget_t *widget, const char *token)
{
    if (!widget || !token || widget->state_binding_count == 0U) {
        return false;
    }
    for (size_t i = 0; i < widget->state_binding_count; ++i) {
        if (widget->state_bindings[i] && strcmp(widget->state_bindings[i], token) == 0) {
            return true;
        }
    }
    return false;
}

static esp_err_t yui_widget_add_state_binding(yui_widget_t *widget, char *token)
{
    if (!widget || !token) {
        free(token);
        return ESP_ERR_INVALID_ARG;
    }
    if (!yui_is_valid_state_token(token)) {
        free(token);
        return ESP_OK;
    }
    if (yui_widget_binding_exists(widget, token)) {
        free(token);
        return ESP_OK;
    }
    char **next = (char **)realloc(widget->state_bindings, (widget->state_binding_count + 1U) * sizeof(char *));
    if (!next) {
        free(token);
        return ESP_ERR_NO_MEM;
    }
    widget->state_bindings = next;
    widget->state_bindings[widget->state_binding_count++] = token;
    return ESP_OK;
}

static char *yui_trimmed_token_copy(const char *start, size_t len)
{
    while (len > 0U && isspace((unsigned char)*start)) {
        ++start;
        --len;
    }
    while (len > 0U && isspace((unsigned char)start[len - 1U])) {
        --len;
    }
    if (len == 0U) {
        return NULL;
    }
    char *out = (char *)malloc(len + 1U);
    if (!out) {
        return NULL;
    }
    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

static esp_err_t yui_collect_bindings_from_text(yui_widget_t *widget, const char *text)
{
    if (!widget || !text) {
        return ESP_OK;
    }
    yui_binding_ctx_t ctx = {
        .widget = widget,
    };

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
        char *token = yui_trimmed_token_copy(token_start, len);
        if (token) {
            esp_err_t err = yui_expr_collect_identifiers(token, yui_binding_collect_cb, &ctx);
            free(token);
            if (err != ESP_OK) {
                return err;
            }
        }
        cursor = close + 2;
    }
    return ESP_OK;
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
    if (strcmp(type, "button") == 0) {
        *out = YUI_WIDGET_BUTTON;
        return true;
    }
    if (strcmp(type, "spacer") == 0) {
        *out = YUI_WIDGET_SPACER;
        return true;
    }
    yamui_log(YAMUI_LOG_LEVEL_WARN, YAMUI_LOG_CAT_PARSER, "Unsupported widget type '%s'", type);
    return false;
}

static esp_err_t yui_parse_widget_sequence(const yml_node_t *widgets_node, yui_widget_t **out_widgets, size_t *out_count, const char *owner_name)
{
    if (!out_widgets || !out_count) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_widgets = NULL;
    *out_count = 0;
    const char *label = owner_name ? owner_name : "<anonymous>";
    if (!widgets_node || yml_node_get_type(widgets_node) != YML_NODE_SEQUENCE) {
        yamui_log(YAMUI_LOG_LEVEL_ERROR, YAMUI_LOG_CAT_PARSER, "Block '%s' is missing a widgets sequence", label);
        return ESP_ERR_INVALID_ARG;
    }
    size_t count = yml_node_child_count(widgets_node);
    if (count == 0) {
        yamui_log(YAMUI_LOG_LEVEL_ERROR, YAMUI_LOG_CAT_PARSER, "Block '%s' has an empty widget list", label);
        return ESP_ERR_INVALID_ARG;
    }
    yui_widget_t *widgets = (yui_widget_t *)calloc(count, sizeof(yui_widget_t));
    if (!widgets) {
        return ESP_ERR_NO_MEM;
    }

    size_t idx = 0;
    for (const yml_node_t *child = yml_node_child_at(widgets_node, 0); child; child = yml_node_next(child)) {
        if (yml_node_get_type(child) != YML_NODE_MAPPING) {
            yamui_log(YAMUI_LOG_LEVEL_ERROR, YAMUI_LOG_CAT_PARSER, "Widget entry %u in '%s' must be a mapping", (unsigned)idx, label);
            yui_free_widgets(widgets, count);
            return ESP_ERR_INVALID_ARG;
        }
        yui_widget_t *widget = &widgets[idx++];
        char *type_str = yui_read_string(child, "type");
        if (!type_str || !yui_widget_type_from_string(type_str, &widget->type)) {
            free(type_str);
            yui_free_widgets(widgets, count);
            return ESP_ERR_INVALID_ARG;
        }
        free(type_str);
        widget->text = yui_read_string(child, "text");
        if (widget->type == YUI_WIDGET_LABEL && !widget->text) {
            yamui_log(YAMUI_LOG_LEVEL_ERROR, YAMUI_LOG_CAT_PARSER, "Label widget entry %u in '%s' missing text", (unsigned)(idx - 1), label);
            yui_free_widgets(widgets, count);
            return ESP_ERR_INVALID_ARG;
        }
        if (widget->type == YUI_WIDGET_BUTTON && !widget->text) {
            widget->text = yui_strdup("");
            if (!widget->text) {
                yui_free_widgets(widgets, count);
                return ESP_ERR_NO_MEM;
            }
        }
        if (widget->text) {
            esp_err_t binding_err = yui_collect_bindings_from_text(widget, widget->text);
            if (binding_err != ESP_OK) {
                yui_free_widgets(widgets, count);
                return binding_err;
            }
        }
        widget->variant = yui_read_string(child, "variant");
        if (widget->type == YUI_WIDGET_SPACER) {
            widget->size = yui_read_i32(child, "size", 8);
            if (widget->size < 0) {
                widget->size = 0;
            }
        }
        esp_err_t event_err = ESP_OK;
        for (size_t evt = 0; evt < s_widget_event_field_count; ++evt) {
            const yml_node_t *event_node = yml_node_get_child(child, s_widget_event_fields[evt].yaml_key);
            if (!event_node) {
                continue;
            }
            event_err = yui_action_list_from_node(event_node, &widget->events.lists[s_widget_event_fields[evt].event_type]);
            if (event_err != ESP_OK) {
                break;
            }
        }
        if (event_err != ESP_OK) {
            yui_free_widgets(widgets, count);
            return event_err;
        }
    }

    *out_widgets = widgets;
    *out_count = count;
    return ESP_OK;
}

static esp_err_t yui_parse_widgets(const yml_node_t *widgets_node, yui_template_t *tpl)
{
    return yui_parse_widget_sequence(widgets_node, &tpl->widgets, &tpl->widget_count, tpl->name);
}

static esp_err_t yui_parse_templates(const yml_node_t *templates_node, yui_schema_t *schema)
{
    if (!templates_node || yml_node_get_type(templates_node) != YML_NODE_MAPPING) {
        yamui_log(YAMUI_LOG_LEVEL_ERROR, YAMUI_LOG_CAT_PARSER, "sensor_templates must be a mapping");
        return ESP_ERR_INVALID_ARG;
    }
    size_t count = yml_node_child_count(templates_node);
    if (count == 0) {
        yamui_log(YAMUI_LOG_LEVEL_ERROR, YAMUI_LOG_CAT_PARSER, "sensor_templates block is empty");
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

static esp_err_t yui_parse_components(const yml_node_t *components_node, yui_schema_t *schema)
{
    if (!components_node) {
        return ESP_OK;
    }
    if (yml_node_get_type(components_node) != YML_NODE_MAPPING) {
        yamui_log(YAMUI_LOG_LEVEL_ERROR, YAMUI_LOG_CAT_PARSER, "components block must be a mapping");
        return ESP_ERR_INVALID_ARG;
    }
    size_t count = yml_node_child_count(components_node);
    if (count == 0) {
        return ESP_OK;
    }
    schema->components = (yui_component_t *)calloc(count, sizeof(yui_component_t));
    if (!schema->components) {
        return ESP_ERR_NO_MEM;
    }
    schema->component_count = count;

    esp_err_t err = ESP_OK;
    size_t idx = 0;
    for (const yml_node_t *child = yml_node_child_at(components_node, 0); child; child = yml_node_next(child)) {
        if (yml_node_get_type(child) != YML_NODE_MAPPING) {
            yamui_log(YAMUI_LOG_LEVEL_ERROR, YAMUI_LOG_CAT_PARSER, "Component '%s' must be a mapping", yml_node_get_key(child));
            err = ESP_ERR_INVALID_ARG;
            break;
        }
        yui_component_t *component = &schema->components[idx++];
        component->name = yui_strdup(yml_node_get_key(child));
        if (!component->name) {
            err = ESP_ERR_NO_MEM;
            break;
        }
        yui_component_layout_defaults(&component->layout);
        const yml_node_t *layout_node = yml_node_get_child(child, "layout");
        err = yui_parse_component_layout(layout_node, &component->layout);
        if (err != ESP_OK) {
            break;
        }
        const yml_node_t *widgets_node = yml_node_get_child(child, "widgets");
        err = yui_parse_widget_sequence(widgets_node, &component->widgets, &component->widget_count, component->name);
        if (err != ESP_OK) {
            break;
        }
    }

    if (err != ESP_OK) {
        yui_free_components(schema->components, schema->component_count);
        schema->components = NULL;
        schema->component_count = 0;
        return err;
    }
    return ESP_OK;
}

static esp_err_t yui_parse_styles(const yml_node_t *styles_node, yui_schema_t *schema)
{
    if (!styles_node) {
        return ESP_OK;
    }
    if (yml_node_get_type(styles_node) != YML_NODE_MAPPING) {
        yamui_log(YAMUI_LOG_LEVEL_ERROR, YAMUI_LOG_CAT_PARSER, "styles block must be a mapping");
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
            yamui_log(YAMUI_LOG_LEVEL_ERROR, YAMUI_LOG_CAT_PARSER, "Style '%s' must be a mapping", yml_node_get_key(child));
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
        yamui_log(YAMUI_LOG_LEVEL_ERROR, YAMUI_LOG_CAT_PARSER, "layout block must be a mapping");
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

static yui_component_align_t yui_component_align_from_string(const char *value)
{
    if (!value) {
        return YUI_COMPONENT_ALIGN_START;
    }
    if (strcasecmp(value, "center") == 0) {
        return YUI_COMPONENT_ALIGN_CENTER;
    }
    if (strcasecmp(value, "end") == 0) {
        return YUI_COMPONENT_ALIGN_END;
    }
    if (strcasecmp(value, "stretch") == 0) {
        return YUI_COMPONENT_ALIGN_STRETCH;
    }
    return YUI_COMPONENT_ALIGN_START;
}

static void yui_component_layout_defaults(yui_component_layout_t *layout)
{
    if (!layout) {
        return;
    }
    layout->flow = YUI_COMPONENT_FLOW_COLUMN;
    layout->main_align = YUI_COMPONENT_ALIGN_START;
    layout->cross_align = YUI_COMPONENT_ALIGN_START;
    layout->gap = 12;
    layout->padding = 16;
    layout->background_color = NULL;
}

static esp_err_t yui_parse_component_layout(const yml_node_t *node, yui_component_layout_t *layout)
{
    if (!layout) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!node) {
        return ESP_OK;
    }
    if (yml_node_get_type(node) != YML_NODE_MAPPING) {
        yamui_log(YAMUI_LOG_LEVEL_ERROR, YAMUI_LOG_CAT_PARSER, "component layout must be a mapping");
        return ESP_ERR_INVALID_ARG;
    }
    char *type = yui_read_string(node, "type");
    if (type) {
        if (strcasecmp(type, "row") == 0) {
            layout->flow = YUI_COMPONENT_FLOW_ROW;
        } else {
            layout->flow = YUI_COMPONENT_FLOW_COLUMN;
        }
        free(type);
    }
    layout->gap = yui_read_u8(node, "gap", layout->gap);
    layout->padding = yui_read_u8(node, "padding", layout->padding);
    char *align = yui_read_string(node, "align");
    if (align) {
        layout->main_align = yui_component_align_from_string(align);
        free(align);
    }
    char *cross_align = yui_read_string(node, "cross_align");
    if (cross_align) {
        layout->cross_align = yui_component_align_from_string(cross_align);
        free(cross_align);
    }
    char *bg = yui_read_string(node, "background_color");
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
        for (size_t b = 0; b < widgets[i].state_binding_count; ++b) {
            free(widgets[i].state_bindings[b]);
        }
        free(widgets[i].state_bindings);
        for (size_t evt = 0; evt < YUI_WIDGET_EVENT_COUNT; ++evt) {
            yui_action_list_free(&widgets[i].events.lists[evt]);
        }
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

static void yui_free_component_layout(yui_component_layout_t *layout)
{
    if (!layout) {
        return;
    }
    free(layout->background_color);
    layout->background_color = NULL;
}

static void yui_free_components(yui_component_t *components, size_t count)
{
    if (!components) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        free(components[i].name);
        yui_free_component_layout(&components[i].layout);
        yui_free_widgets(components[i].widgets, components[i].widget_count);
    }
    free(components);
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

    const yml_node_t *state_node = yml_node_get_child(root, "state");
    if (state_node) {
        esp_err_t state_err = yui_state_init();
        if (state_err != ESP_OK) {
            return state_err;
        }
        state_err = yui_state_seed_from_yaml(state_node);
        if (state_err != ESP_OK) {
            return state_err;
        }
    }

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
        yamui_log(YAMUI_LOG_LEVEL_ERROR, YAMUI_LOG_CAT_PARSER, "YAML schema missing sensor_templates block");
        yui_schema_free(out_schema);
        return ESP_ERR_INVALID_ARG;
    }
    err = yui_parse_templates(templates_node, out_schema);
    if (err != ESP_OK) {
        yui_schema_free(out_schema);
        return err;
    }

    const yml_node_t *components_node = yml_node_get_child(root, "components");
    err = yui_parse_components(components_node, out_schema);
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
    yui_free_components(schema->components, schema->component_count);
    schema->components = NULL;
    schema->component_count = 0;
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

const yui_component_t *yui_schema_get_component(const yui_schema_t *schema, const char *name)
{
    if (!schema || !name) {
        return NULL;
    }
    for (size_t i = 0; i < schema->component_count; ++i) {
        if (schema->components[i].name && strcmp(schema->components[i].name, name) == 0) {
            return &schema->components[i];
        }
    }
    return NULL;
}
