#include "lvgl_yaml_gui.h"

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

static const char *TAG = "lvgl_yaml";

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
        return "";
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
        return sensor_manager_kind_name(sensor->kind);
    }
    return "";
}

static void yui_format_text(const char *tmpl, const sensor_record_t *sensor, char *out, size_t out_len)
{
    if (!tmpl || !out || out_len == 0U) {
        return;
    }
    size_t pos = 0;
    while (*tmpl && pos + 1 < out_len) {
        if (tmpl[0] == '{' && tmpl[1] == '{') {
            const char *end = strstr(tmpl + 2, "}}");
            if (!end) {
                break;
            }
            char token[32];
            size_t tlen = (size_t)(end - (tmpl + 2));
            if (tlen >= sizeof(token)) {
                tlen = sizeof(token) - 1U;
            }
            memcpy(token, tmpl + 2, tlen);
            token[tlen] = '\0';
            char scratch[32];
            const char *resolved = yui_resolve_token(sensor, token, scratch, sizeof(scratch));
            size_t rlen = strlen(resolved);
            if (rlen > out_len - pos - 1) {
                rlen = out_len - pos - 1;
            }
            memcpy(out + pos, resolved, rlen);
            pos += rlen;
            tmpl = end + 2;
            continue;
        }
        out[pos++] = *tmpl++;
    }
    out[pos] = '\0';
}

static void yui_render_widget(const yui_widget_t *widget, const sensor_record_t *sensor, const yui_style_t *style, lv_obj_t *parent)
{
    if (!widget || !sensor || !parent) {
        return;
    }
    char buffer[128];
    yui_format_text(widget->text, sensor, buffer, sizeof(buffer));
    switch (widget->type) {
        case YUI_WIDGET_LABEL: {
            lv_obj_t *label = lv_label_create(parent);
            yui_apply_label_variant(label, widget, style);
            lv_label_set_text(label, buffer);
            lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(label, LV_PCT(100));
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
        lv_obj_t *subtitle = lv_label_create(card);
        lv_obj_set_style_text_font(subtitle, LV_FONT_DEFAULT, 0);
        lv_color_t color = style && style->accent_color ? yui_color_from_string(style->accent_color, lv_color_hex(0x8E9AC0)) : lv_color_hex(0x8E9AC0);
        lv_obj_set_style_text_color(subtitle, color, 0);
        lv_label_set_text(subtitle, tpl->subtitle);
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

    sensor_manager_tick();

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
        const char *type_name = sensor_manager_kind_name(sensors[i].kind);
        const yui_template_t *tpl = yui_schema_get_template(schema, type_name);
        if (!tpl) {
            ESP_LOGW(TAG, "No template defined for sensor type '%s'", type_name);
            continue;
        }
        yui_render_card(tpl, schema, &sensors[i], grid);
    }

    return ESP_OK;
}

esp_err_t lvgl_yaml_gui_load_default(void)
{
    size_t size = 0;
    const uint8_t *blob = ui_schemas_get_home(&size);
    if (!blob || size == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    yml_node_t *root = NULL;
    esp_err_t err = yaml_core_parse_buffer((const char *)blob, size, &root);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse YAML blob (%s)", esp_err_to_name(err));
        return err;
    }

    yui_schema_t schema = {0};
    err = yui_schema_from_tree(root, &schema);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Invalid UI schema (%s)", esp_err_to_name(err));
        yml_node_free(root);
        return err;
    }

    err = yui_render_schema(&schema);
    yui_schema_free(&schema);
    yml_node_free(root);
    return err;
}
