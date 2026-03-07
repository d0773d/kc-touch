# LVGL Builder (Layer 3)
**Rendering Declarative YAML UI Schemas into LVGL Objects**

The `lvgl_yaml_gui` component implements Layer 3 of the system. It takes the structured UI schema produced by Layer 2 (`yml_ui_schema_t`) and converts it into actual LVGL objects at runtime.

This layer is responsible for rendering, not parsing or interpreting YAML.

---

## 1. Purpose

The LVGL Builder:

- Creates LVGL objects based on template definitions
- Applies styles defined in YAML
- Substitutes variables (e.g., `{{name}}`, `{{value}}`)
- Lays out widgets using a grid or flex layout
- Produces a complete LVGL screen object

This layer is where the declarative UI becomes visible.

---

## 2. Inputs and Outputs

### Input
- `yml_ui_schema_t *schema`
- A list of discovered sensors (`sensor_list_t`)
- Optional runtime data (values, names, units)

### Output
- A fully constructed LVGL screen (`lv_obj_t *`)

---

## 3. Public API

### Build a screen from a schema and sensor list

```c
lv_obj_t *lvgl_yaml_build_screen(
    const yml_ui_schema_t *schema,
    const sensor_list_t *sensors);
```

### Build a single widget from a template

```c
lv_obj_t *lvgl_yaml_build_widget(
    const yml_template_t *tpl,
    const sensor_t *sensor,
    lv_obj_t *parent);
```

### Apply variable substitution

```c
char *lvgl_yaml_substitute(
    const char *template_str,
    const sensor_t *sensor);
```

---

## 4. Rendering Pipeline

The LVGL Builder follows a deterministic pipeline:

```
UI Schema → Templates → Widgets → LVGL Objects → Screen
```

### Step 1 — Create root screen

```c
lv_obj_t *screen = lv_obj_create(NULL);
```

### Step 2 — For each sensor, find matching template

```c
const yml_template_t *tpl =
    yaml_ui_get_template(schema, sensor->type_name);
```

### Step 3 — Build card widget

```c
lv_obj_t *card = lvgl_yaml_build_widget(tpl, sensor, screen);
```

### Step 4 — Add card to layout

- Grid layout (default)
- Flex layout (optional future extension)

### Step 5 — Return screen

---

## 5. Widget Rendering Rules

Each widget in a template is rendered according to its `type`.

### 5.1 Label Widget

#### YAML

```yaml
- type: label
  text: "{{value}} °C"
  style: value_style
```

#### LVGL Rendering

```c
lv_obj_t *lbl = lv_label_create(parent);
lv_label_set_text(lbl, substituted_text);
apply_style(lbl, widget->style);
```

### 5.2 Button Widget (future extension)

#### YAML

```yaml
- type: button
  text: "Refresh"
  style: button_style
```

#### LVGL Rendering

```c
lv_obj_t *btn = lv_btn_create(parent);
lv_obj_t *lbl = lv_label_create(btn);
lv_label_set_text(lbl, substituted_text);
apply_style(btn, widget->style);
```

### 5.3 Card Widget (template root)

A template’s `type: card` maps to:

```c
lv_obj_t *card = lv_obj_create(parent);
lv_obj_set_style_pad_all(card, 8, LV_PART_MAIN);
apply_style(card, tpl->style);
```

---

## 6. Variable Substitution

Widget text may contain placeholders:

- `{{name}}`
- `{{value}}`
- `{{unit}}`
- `{{address}}`

### Example

Template: `"{{value}} °C"`

Sensor value: `23.5`

Result: `"23.5 °C"`

### Implementation

```c
char *lvgl_yaml_substitute(const char *template_str, const sensor_t *sensor)
{
    // Replace {{name}}, {{value}}, etc.
}
```

Substitution is simple string replacement — no logic or loops.

---

## 7. Layout Engine

The layout engine arranges dynamically generated widgets based on:

```yaml
layout:
  columns: 2
  spacing: 12
```

### Grid Layout Algorithm

Given N widgets and C columns:

```
row = index / columns
col = index % columns
```

Position:

```
x = col * (card_width + spacing)
y = row * (card_height + spacing)
```

### LVGL Implementation

```c
lv_obj_set_pos(card, x, y);
```

Future extension: LVGL flex layout.

---

## 8. Style Application

Styles are referenced by name:

```yaml
style: temp_style
```

The LVGL Builder calls:

```c
apply_style(obj, "temp_style");
```

The style registry is defined by the application, not the builder.

---

## 9. Example: Full Rendering Flow

### YAML Template

```yaml
sensor_templates:
  temperature:
    type: card
    style: temp_style
    widgets:
      - type: label
        text: "{{name}}"
      - type: label
        text: "{{value}} °C"
layout:
  columns: 2
  spacing: 10
```

### Sensors Detected

```
SHT40 Temp → 23.5
BMP280 Temp → 22.1
```

### LVGL Output

Two cards:

```
+---------------------+   +---------------------+
| SHT40 Temp          |   | BMP280 Temp         |
| 23.5 °C             |   | 22.1 °C             |
+---------------------+   +---------------------+
```

---

## 10. Component Structure

```
lvgl_yaml_gui/
├── CMakeLists.txt
├── include/
│   └── lvgl_yaml_gui.h
└── src/
    ├── lvgl_yaml_builder.c
    ├── lvgl_yaml_subst.c
    └── lvgl_yaml_layout.c
```

---

## 11. Responsibilities of Layer 3

### Layer 3 DOES:
- Create LVGL objects
- Apply styles
- Substitute variables
- Arrange widgets
- Build screens

### Layer 3 DOES NOT:
- Parse YAML
- Interpret schema
- Discover sensors
- Store or load templates

Those belong to Layers 1 and 2.

---

## 12. Why This Layer Matters

This layer gives you:

- A fully dynamic UI
- Zero hardcoded LVGL layout
- Clean separation between data and rendering
- A system that can scale to dozens of sensors
- A foundation for future UI features (charts, gauges, icons)

It is the final step in turning YAML into a live, interactive UI.
