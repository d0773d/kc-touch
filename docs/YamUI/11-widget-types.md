# Widget Types
**Supported Widgets + YAML Schema + LVGL Mapping + Roadmap**

Widgets are declared inside sensor templates and rendered by the LVGL builder. Each widget type defines how YAML describes it, how variables are substituted, and which LVGL objects get created.

---

## 1. Overview

```yaml
sensor_templates:
  temperature:
    type: card
    widgets:
      - type: label
        text: "{{value}} °C"
```

Every widget must specify `type` and may include `text`, `style`, or type-specific fields.

---

## 2. Supported Widgets (Initial Release)

1. `label`
2. `button`
3. `spacer`

---

## 3. `label`

```yaml
- type: label
  text: "{{value}} °C"
  style: value_style
```

| Field | Required | Description |
|-------|----------|-------------|
| `type` | yes | must be `label` |
| `text` | no | literal or template string |
| `style` | no | style name |

LVGL mapping:

```c
lv_obj_t *lbl = lv_label_create(parent);
lv_label_set_text(lbl, substituted_text);
apply_style(lbl, widget->style);
```

Supports placeholders: `{{name}}`, `{{value}}`, `{{unit}}`, `{{address}}`.

---

## 4. `button`

```yaml
- type: button
  text: "Refresh"
  style: button_style
```

| Field | Required | Description |
|-------|----------|-------------|
| `type` | yes | `button` |
| `text` | no | button label |
| `style` | no | style name |

LVGL mapping:

```c
lv_obj_t *btn = lv_btn_create(parent);
lv_obj_t *lbl = lv_label_create(btn);
lv_label_set_text(lbl, substituted_text);
apply_style(btn, widget->style);
```

Notes: visual only; no callbacks yet.

---

## 5. `spacer`

```yaml
- type: spacer
  size: 8
```

| Field | Required | Description |
|-------|----------|-------------|
| `type` | yes | `spacer` |
| `size` | no | pixel height, default 8 |

LVGL mapping:

```c
lv_obj_t *sp = lv_obj_create(parent);
lv_obj_set_size(sp, LV_SIZE_CONTENT, size);
lv_obj_clear_flag(sp, LV_OBJ_FLAG_CLICKABLE);
```

---

## 6. Roadmap Widgets (Future)

- `icon` → `lv_img`
- `chart` → `lv_chart`
- `bar` → `lv_bar`
- `switch` → `lv_switch`
- Container widgets (`row`, `column`) for grouping child widgets

---

## 7. Error Handling

Unknown widget types are logged and skipped so rendering can continue without crashing.

---

## 8. Summary

Current widget set keeps the declarative language simple while covering core needs. The roadmap allows gradual expansion without destabilizing existing dashboards.
