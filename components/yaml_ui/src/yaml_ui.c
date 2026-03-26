#include "yaml_ui.h"
#include "yamui_logging.h"
#include "yamui_state.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

static char *yui_strdup(const char *src)
{
    if (!src) {
        return NULL;
    }
    size_t len = strlen(src);
    char *copy = (char *)malloc(len + 1U);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, src, len + 1U);
    return copy;
}

static bool yui_node_is_mapping(const yml_node_t *node)
{
    return node && yml_node_get_type(node) == YML_NODE_MAPPING;
}

static bool yui_node_is_sequence(const yml_node_t *node)
{
    return node && yml_node_get_type(node) == YML_NODE_SEQUENCE;
}

static const yml_node_t *yui_style_source_node(const yml_node_t *node)
{
    if (!node) {
        return NULL;
    }
    const yml_node_t *value_node = yml_node_get_child(node, "value");
    if (yui_node_is_mapping(value_node)) {
        return value_node;
    }
    return node;
}

static char *yui_read_string(const yml_node_t *node, const char *key)
{
    if (!node || !key) {
        return NULL;
    }
    const yml_node_t *child = yml_node_get_child(node, key);
    if (!child) {
        return NULL;
    }
    const char *scalar = yml_node_get_scalar(child);
    return scalar ? yui_strdup(scalar) : NULL;
}

static char *yui_read_string_alias(const yml_node_t *node, const char *key, const char *alias)
{
    char *value = yui_read_string(node, key);
    if (value || !alias) {
        return value;
    }
    return yui_read_string(node, alias);
}

static int32_t yui_read_i32(const yml_node_t *node, const char *key, int32_t def)
{
    if (!node || !key) {
        return def;
    }
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

static int32_t yui_read_i32_alias(const yml_node_t *node, const char *key, const char *alias, int32_t def)
{
    if (!node) {
        return def;
    }
    const yml_node_t *child = yml_node_get_child(node, key);
    if (!child && alias) {
        child = yml_node_get_child(node, alias);
    }
    if (!child) {
        return def;
    }
    const char *scalar = yml_node_get_scalar(child);
    if (!scalar) {
        return def;
    }
    return atoi(scalar);
}

static bool yui_read_bool(const yml_node_t *node, const char *key, bool def)
{
    if (!node || !key) {
        return def;
    }
    const yml_node_t *child = yml_node_get_child(node, key);
    if (!child) {
        return def;
    }
    const char *scalar = yml_node_get_scalar(child);
    if (!scalar) {
        return def;
    }
    if (strcasecmp(scalar, "true") == 0 || strcmp(scalar, "1") == 0) {
        return true;
    }
    if (strcasecmp(scalar, "false") == 0 || strcmp(scalar, "0") == 0) {
        return false;
    }
    if (strcasecmp(scalar, "none") == 0 || strcasecmp(scalar, "off") == 0) {
        return false;
    }
    if (strcasecmp(scalar, "true") == 0 || strcmp(scalar, "1") == 0) {
        return true;
    }
    if (strcasecmp(scalar, "yes") == 0 || strcasecmp(scalar, "on") == 0) {
        return true;
    }
    if (scalar[0] != '\0') {
        return true;
    }
    return def;
}

static esp_err_t yui_parse_state(const yml_node_t *node)
{
    if (!node) {
        return ESP_OK;
    }
    esp_err_t err = yui_state_init();
    if (err != ESP_OK) {
        return err;
    }
    return yui_state_seed_from_yaml(node);
}

static esp_err_t yui_parse_app(const yml_node_t *node, yui_schema_t *schema)
{
    if (!schema) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!node) {
        schema->app.initial_screen = NULL;
        schema->app.locale = NULL;
        return ESP_OK;
    }
    if (!yui_node_is_mapping(node)) {
        yamui_log(YAMUI_LOG_LEVEL_ERROR, YAMUI_LOG_CAT_PARSER, "app block must be a mapping");
        return ESP_ERR_INVALID_ARG;
    }
    schema->app.initial_screen = yui_read_string(node, "initial_screen");
    schema->app.locale = yui_read_string(node, "locale");
    if (schema->app.locale && schema->app.locale[0] != '\0' && yui_state_get("ui.locale", "")[0] == '\0') {
        (void)yui_state_set("ui.locale", schema->app.locale);
    }
    return ESP_OK;
}

static void yui_style_free(yui_style_t *style)
{
    if (!style) {
        return;
    }
    free(style->name);
    free(style->background_color);
    free(style->text_color);
    free(style->accent_color);
    free(style->text_font);
    free(style->align);
}

static void yui_translation_locale_free(yui_translation_locale_t *locale)
{
    if (!locale) {
        return;
    }
    free(locale->locale);
    free(locale->label);
    if (locale->entries) {
        for (size_t i = 0; i < locale->entry_count; ++i) {
            free(locale->entries[i].key);
            free(locale->entries[i].value);
        }
        free(locale->entries);
    }
    locale->entries = NULL;
    locale->entry_count = 0U;
}

static esp_err_t yui_parse_styles(const yml_node_t *node, yui_schema_t *schema)
{
    if (!schema) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!node) {
        return ESP_OK;
    }
    if (!yui_node_is_mapping(node)) {
        yamui_log(YAMUI_LOG_LEVEL_ERROR, YAMUI_LOG_CAT_PARSER, "styles block must be a mapping");
        return ESP_ERR_INVALID_ARG;
    }
    size_t count = yml_node_child_count(node);
    if (count == 0U) {
        return ESP_OK;
    }
    schema->styles = (yui_style_t *)calloc(count, sizeof(yui_style_t));
    if (!schema->styles) {
        return ESP_ERR_NO_MEM;
    }
    schema->style_count = count;
    size_t idx = 0;
    for (const yml_node_t *child = yml_node_child_at(node, 0); child; child = yml_node_next(child)) {
        if (!yui_node_is_mapping(child)) {
            yamui_log(YAMUI_LOG_LEVEL_WARN, YAMUI_LOG_CAT_PARSER, "Style '%s' must be a mapping", yml_node_get_key(child));
            continue;
        }
        const yml_node_t *style_node = yui_style_source_node(child);
        yui_style_t *style = &schema->styles[idx++];
        style->name = yui_strdup(yml_node_get_key(child));
        style->background_color = yui_read_string_alias(style_node, "bg_color", "backgroundColor");
        style->text_color = yui_read_string_alias(style_node, "text_color", "color");
        style->accent_color = yui_read_string_alias(style_node, "accent_color", "accentColor");
        style->text_font = yui_read_string_alias(style_node, "text_font", "fontFamily");
        style->font_size = yui_read_i32_alias(style_node, "font_size", "fontSize", 0);
        style->font_weight = yui_read_i32_alias(style_node, "font_weight", "fontWeight", 0);
        style->letter_spacing = yui_read_i32_alias(style_node, "letter_spacing", "letterSpacing", 0);
        style->width = yui_read_i32_alias(style_node, "width", "minWidth", 0);
        style->height = yui_read_i32_alias(style_node, "height", "minHeight", 0);
        style->padding = yui_read_i32(style_node, "padding", 0);
        style->padding_x = yui_read_i32_alias(style_node, "padding_x", "paddingHorizontal", -1);
        style->padding_y = yui_read_i32_alias(style_node, "padding_y", "paddingVertical", -1);
        style->radius = yui_read_i32_alias(style_node, "radius", "borderRadius", 0);
        style->spacing = yui_read_i32_alias(style_node, "spacing", "gap", 0);
        style->shadow = yui_read_bool(style_node, "shadow", false);
        style->align = yui_read_string(style_node, "align");
    }
    return ESP_OK;
}

static esp_err_t yui_parse_translation_entries(const yml_node_t *node, yui_translation_locale_t *locale)
{
    if (!node) {
        return ESP_OK;
    }
    if (!yui_node_is_mapping(node)) {
        yamui_log(YAMUI_LOG_LEVEL_ERROR, YAMUI_LOG_CAT_PARSER, "translation entries must be a mapping");
        return ESP_ERR_INVALID_ARG;
    }
    size_t count = yml_node_child_count(node);
    if (count == 0U) {
        return ESP_OK;
    }
    locale->entries = (yui_translation_entry_t *)calloc(count, sizeof(yui_translation_entry_t));
    if (!locale->entries) {
        return ESP_ERR_NO_MEM;
    }
    locale->entry_count = count;
    size_t idx = 0;
    for (const yml_node_t *child = yml_node_child_at(node, 0); child; child = yml_node_next(child)) {
        const char *key = yml_node_get_key(child);
        const char *value = yml_node_get_scalar(child);
        if (!key || !value) {
            continue;
        }
        locale->entries[idx].key = yui_strdup(key);
        locale->entries[idx].value = yui_strdup(value);
        if (!locale->entries[idx].key || !locale->entries[idx].value) {
            return ESP_ERR_NO_MEM;
        }
        idx++;
    }
    locale->entry_count = idx;
    return ESP_OK;
}

static esp_err_t yui_parse_translations(const yml_node_t *node, yui_schema_t *schema)
{
    if (!schema) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!node) {
        return ESP_OK;
    }
    if (!yui_node_is_mapping(node)) {
        yamui_log(YAMUI_LOG_LEVEL_ERROR, YAMUI_LOG_CAT_PARSER, "translations block must be a mapping");
        return ESP_ERR_INVALID_ARG;
    }
    size_t count = yml_node_child_count(node);
    if (count == 0U) {
        return ESP_OK;
    }
    schema->translations = (yui_translation_locale_t *)calloc(count, sizeof(yui_translation_locale_t));
    if (!schema->translations) {
        return ESP_ERR_NO_MEM;
    }
    schema->translation_count = count;
    size_t idx = 0;
    for (const yml_node_t *child = yml_node_child_at(node, 0); child; child = yml_node_next(child)) {
        if (!yui_node_is_mapping(child)) {
            yamui_log(YAMUI_LOG_LEVEL_WARN, YAMUI_LOG_CAT_PARSER, "Translation locale '%s' must be a mapping", yml_node_get_key(child));
            continue;
        }
        yui_translation_locale_t *locale = &schema->translations[idx++];
        locale->locale = yui_strdup(yml_node_get_key(child));
        locale->label = yui_read_string(child, "label");
        esp_err_t err = yui_parse_translation_entries(yml_node_get_child(child, "entries"), locale);
        if (err != ESP_OK) {
            return err;
        }
    }
    schema->translation_count = idx;
    return ESP_OK;
}

static void yui_component_def_free(yui_component_def_t *component)
{
    if (!component) {
        return;
    }
    free(component->name);
    if (component->props) {
        for (size_t i = 0; i < component->prop_count; ++i) {
            free(component->props[i]);
        }
        free(component->props);
        component->props = NULL;
    }
    component->prop_count = 0;
    component->layout_node = NULL;
    component->widgets_node = NULL;
}

static esp_err_t yui_parse_component_props(const yml_node_t *props_node, yui_component_def_t *component)
{
    if (!props_node) {
        return ESP_OK;
    }
    if (!yui_node_is_sequence(props_node)) {
        yamui_log(YAMUI_LOG_LEVEL_ERROR, YAMUI_LOG_CAT_PARSER, "Component '%s' props must be a sequence", component->name ? component->name : "<component>");
        return ESP_ERR_INVALID_ARG;
    }
    size_t count = yml_node_child_count(props_node);
    if (count == 0U) {
        return ESP_OK;
    }
    component->props = (char **)calloc(count, sizeof(char *));
    if (!component->props) {
        return ESP_ERR_NO_MEM;
    }
    component->prop_count = count;
    size_t idx = 0;
    for (const yml_node_t *entry = yml_node_child_at(props_node, 0); entry; entry = yml_node_next(entry)) {
        const char *scalar = yml_node_get_scalar(entry);
        if (!scalar) {
            continue;
        }
        component->props[idx++] = yui_strdup(scalar);
    }
    return ESP_OK;
}

static esp_err_t yui_parse_component_prop_schema(const yml_node_t *prop_schema_node, yui_component_def_t *component)
{
    if (!prop_schema_node) {
        return ESP_OK;
    }
    if (!yui_node_is_sequence(prop_schema_node)) {
        yamui_log(YAMUI_LOG_LEVEL_ERROR, YAMUI_LOG_CAT_PARSER,
                  "Component '%s' prop_schema must be a sequence",
                  component->name ? component->name : "<component>");
        return ESP_ERR_INVALID_ARG;
    }
    size_t count = yml_node_child_count(prop_schema_node);
    if (count == 0U) {
        return ESP_OK;
    }
    component->props = (char **)calloc(count, sizeof(char *));
    if (!component->props) {
        return ESP_ERR_NO_MEM;
    }
    component->prop_count = count;
    size_t idx = 0;
    for (const yml_node_t *entry = yml_node_child_at(prop_schema_node, 0); entry; entry = yml_node_next(entry)) {
        if (!yui_node_is_mapping(entry)) {
            continue;
        }
        const char *name = NULL;
        const yml_node_t *name_node = yml_node_get_child(entry, "name");
        if (name_node) {
            name = yml_node_get_scalar(name_node);
        }
        if (!name || name[0] == '\0') {
            continue;
        }
        component->props[idx++] = yui_strdup(name);
    }
    component->prop_count = idx;
    return ESP_OK;
}

static esp_err_t yui_parse_components(const yml_node_t *node, yui_schema_t *schema)
{
    if (!schema) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!node) {
        return ESP_OK;
    }
    if (!yui_node_is_mapping(node)) {
        yamui_log(YAMUI_LOG_LEVEL_ERROR, YAMUI_LOG_CAT_PARSER, "components block must be a mapping");
        return ESP_ERR_INVALID_ARG;
    }
    size_t count = yml_node_child_count(node);
    if (count == 0U) {
        return ESP_OK;
    }
    schema->components = (yui_component_def_t *)calloc(count, sizeof(yui_component_def_t));
    if (!schema->components) {
        return ESP_ERR_NO_MEM;
    }
    schema->component_count = count;
    size_t idx = 0;
    for (const yml_node_t *child = yml_node_child_at(node, 0); child; child = yml_node_next(child)) {
        if (!yui_node_is_mapping(child)) {
            yamui_log(YAMUI_LOG_LEVEL_WARN, YAMUI_LOG_CAT_PARSER, "Component '%s' must be a mapping", yml_node_get_key(child));
            continue;
        }
        yui_component_def_t *component = &schema->components[idx++];
        component->name = yui_strdup(yml_node_get_key(child));
        component->layout_node = yml_node_get_child(child, "layout");
        component->widgets_node = yml_node_get_child(child, "widgets");
        if (!component->widgets_node) {
            yamui_log(YAMUI_LOG_LEVEL_WARN, YAMUI_LOG_CAT_PARSER, "Component '%s' missing widgets block", component->name ? component->name : "<component>");
        }
        const yml_node_t *props = yml_node_get_child(child, "props");
        const yml_node_t *prop_schema = yml_node_get_child(child, "prop_schema");
        esp_err_t err = ESP_OK;
        if (props && yui_node_is_sequence(props)) {
            err = yui_parse_component_props(props, component);
        } else if (prop_schema) {
            err = yui_parse_component_prop_schema(prop_schema, component);
        }
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

static const yml_node_t *yui_mapping_find_key(const yml_node_t *mapping, const char *name)
{
    if (!mapping || !name) {
        return NULL;
    }
    if (yml_node_get_type(mapping) != YML_NODE_MAPPING) {
        return NULL;
    }
    return yml_node_get_child(mapping, name);
}

esp_err_t yui_schema_from_tree(const yml_node_t *root, yui_schema_t *out_schema)
{
    if (!root || !out_schema) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!yui_node_is_mapping(root)) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out_schema, 0, sizeof(*out_schema));
    out_schema->root = (yml_node_t *)root;

    const yml_node_t *state_node = yml_node_get_child(root, "state");
    esp_err_t err = yui_parse_state(state_node);
    if (err != ESP_OK) {
        return err;
    }
    out_schema->state_node = state_node;

    err = yui_parse_app(yml_node_get_child(root, "app"), out_schema);
    if (err != ESP_OK) {
        return err;
    }

    err = yui_parse_translations(yml_node_get_child(root, "translations"), out_schema);
    if (err != ESP_OK) {
        yui_schema_free(out_schema);
        return err;
    }

    out_schema->styles_node = yml_node_get_child(root, "styles");
    err = yui_parse_styles(out_schema->styles_node, out_schema);
    if (err != ESP_OK) {
        yui_schema_free(out_schema);
        return err;
    }

    out_schema->components_node = yml_node_get_child(root, "components");
    err = yui_parse_components(out_schema->components_node, out_schema);
    if (err != ESP_OK) {
        yui_schema_free(out_schema);
        return err;
    }

    out_schema->screens_node = yml_node_get_child(root, "screens");
    if (!out_schema->screens_node) {
        yamui_log(YAMUI_LOG_LEVEL_ERROR, YAMUI_LOG_CAT_PARSER, "YAML schema missing screens block");
        yui_schema_free(out_schema);
        return ESP_ERR_INVALID_ARG;
    }
    if (!yui_node_is_mapping(out_schema->screens_node)) {
        yamui_log(YAMUI_LOG_LEVEL_ERROR, YAMUI_LOG_CAT_PARSER, "screens block must be a mapping");
        yui_schema_free(out_schema);
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

void yui_schema_free(yui_schema_t *schema)
{
    if (!schema) {
        return;
    }
    free(schema->app.initial_screen);
    schema->app.initial_screen = NULL;
    free(schema->app.locale);
    schema->app.locale = NULL;

    if (schema->styles) {
        for (size_t i = 0; i < schema->style_count; ++i) {
            yui_style_free(&schema->styles[i]);
        }
        free(schema->styles);
    }
    schema->styles = NULL;
    schema->style_count = 0;

    if (schema->translations) {
        for (size_t i = 0; i < schema->translation_count; ++i) {
            yui_translation_locale_free(&schema->translations[i]);
        }
        free(schema->translations);
    }
    schema->translations = NULL;
    schema->translation_count = 0U;

    if (schema->components) {
        for (size_t i = 0; i < schema->component_count; ++i) {
            yui_component_def_free(&schema->components[i]);
        }
        free(schema->components);
    }
    schema->components = NULL;
    schema->component_count = 0;

    schema->root = NULL;
    schema->styles_node = NULL;
    schema->components_node = NULL;
    schema->screens_node = NULL;
    schema->state_node = NULL;
}

const yml_node_t *yui_schema_get_screen(const yui_schema_t *schema, const char *name)
{
    if (!schema || !schema->screens_node || !name) {
        return NULL;
    }
    return yui_mapping_find_key(schema->screens_node, name);
}

const yui_component_def_t *yui_schema_get_component(const yui_schema_t *schema, const char *name)
{
    if (!schema || !schema->components || !name) {
        return NULL;
    }
    for (size_t i = 0; i < schema->component_count; ++i) {
        if (schema->components[i].name && strcmp(schema->components[i].name, name) == 0) {
            return &schema->components[i];
        }
    }
    return NULL;
}

const yui_style_t *yui_schema_get_style(const yui_schema_t *schema, const char *name)
{
    if (!schema || !schema->styles || !name) {
        return NULL;
    }
    for (size_t i = 0; i < schema->style_count; ++i) {
        if (schema->styles[i].name && strcmp(schema->styles[i].name, name) == 0) {
            return &schema->styles[i];
        }
    }
    return NULL;
}

const char *yui_schema_default_screen(const yui_schema_t *schema)
{
    if (!schema) {
        return NULL;
    }
    if (schema->app.initial_screen && schema->app.initial_screen[0] != '\0') {
        return schema->app.initial_screen;
    }
    if (!schema->screens_node) {
        return NULL;
    }
    const yml_node_t *first = yml_node_child_at(schema->screens_node, 0);
    if (!first) {
        return NULL;
    }
    return yml_node_get_key(first);
}

const char *yui_schema_locale(const yui_schema_t *schema)
{
    if (!schema) {
        return NULL;
    }
    return schema->app.locale;
}

const char *yui_schema_translate(const yui_schema_t *schema, const char *locale, const char *key)
{
    if (!schema || !key || key[0] == '\0') {
        return NULL;
    }
    const char *target_locale = (locale && locale[0] != '\0') ? locale : schema->app.locale;
    if (!target_locale || target_locale[0] == '\0' || !schema->translations) {
        return NULL;
    }
    for (size_t i = 0; i < schema->translation_count; ++i) {
        const yui_translation_locale_t *bucket = &schema->translations[i];
        if (!bucket->locale || strcmp(bucket->locale, target_locale) != 0) {
            continue;
        }
        for (size_t j = 0; j < bucket->entry_count; ++j) {
            if (bucket->entries[j].key && strcmp(bucket->entries[j].key, key) == 0) {
                return bucket->entries[j].value;
            }
        }
        break;
    }
    return NULL;
}
