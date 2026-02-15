# UI Schema Interpreter (Layer 2)
**Declarative UI Language for YAML-Driven LVGL**

The `yaml_ui` component implements Layer 2 of the system: it interprets the generic YAML node tree produced by `yaml_core` and converts it into a structured UI schema.

This layer defines the declarative UI language used by the project. It is responsible for understanding what the YAML means, but not how it is rendered.

---

## 1. Purpose

The UI Schema Interpreter:

- Reads the generic YAML tree (`yml_node_t`)
- Validates the structure according to the UI schema
- Extracts:
  - templates
  - widgets
  - styles
  - layout
- Produces a strongly-typed `yml_ui_schema_t`
- Provides lookup helpers for templates

This layer is application-specific, but still independent of LVGL.

---

## 2. Expected YAML Structure

The interpreter expects a YAML file with the following top-level keys:

```yaml
sensor_templates:
  <template_name>:
    type: <card_type>
    style: <style_name>
    widgets:
      - type: <widget_type>
        text: <text>
        style: <style_name>

layout:
  columns: <int>
  spacing: <int>
```

### Example

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

  humidity:
    type: card
    style: humidity_style
    widgets:
      - type: label
        text: "{{name}}"
      - type: label
        text: "{{value}} %"

layout:
  columns: 2
  spacing: 12
```

---

## 3. UI Schema Data Structures

These structures are produced by the interpreter and consumed by the LVGL builder.

### 3.1 Widget Definition

```c
typedef struct {
    char *type;     // "label", "button", etc.
    char *text;     // "{{value}} °C"
    char *style;    // "temp_style"
} yml_widget_t;
```

### 3.2 Template Definition

```c
typedef struct {
    char *name;          // template name: "temperature"
    char *card_type;     // "card"
    char *style;         // "temp_style"
    yml_widget_t *widgets;
    size_t widget_count;
} yml_template_t;
```

Each template describes how to render a sensor type.

### 3.3 Layout Definition

```c
typedef struct {
    int columns;
    int spacing;
} yml_layout_t;
```

This controls how dynamically generated widgets are arranged.

### 3.4 Full UI Schema

```c
typedef struct {
    yml_template_t *templates;
    size_t template_count;
    yml_layout_t layout;
} yml_ui_schema_t;
```

This is the final output of Layer 2.

---

## 4. Public API

### Parse a YAML tree into a UI schema

```c
yml_ui_schema_t *yaml_ui_from_tree(const yml_node_t *root);
```

### Free the schema

```c
void yaml_ui_free(yml_ui_schema_t *schema);
```

### Lookup a template by name

```c
const yml_template_t *yaml_ui_get_template(
    const yml_ui_schema_t *schema,
    const char *name);
```

---

## 5. Interpretation Rules

The interpreter follows these rules:

### 5.1 `sensor_templates` must be a mapping

Example:

```yaml
sensor_templates:
  temperature: { ... }
  humidity: { ... }
```

Each key becomes a `yml_template_t`.

### 5.2 Template fields

Each template must contain:

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `type` | scalar | yes | Card type (e.g., "card") |
| `style` | scalar | no | Style name |
| `widgets` | sequence | yes | List of widget definitions |

### 5.3 Widget fields

Each widget must contain:

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `type` | scalar | yes | Widget type (e.g., "label") |
| `text` | scalar | no | Text or template string |
| `style` | scalar | no | Style name |

### 5.4 Layout fields

Layout is optional. Defaults:

```c
columns = 1
spacing = 8
```

If provided:

```yaml
layout:
  columns: 2
  spacing: 12
```

---

## 6. Example: YAML → UI Schema

### YAML

```yaml
sensor_templates:
  temperature:
    type: card
    style: temp_style
    widgets:
      - type: label
        text: "{{value}} °C"
layout:
  columns: 2
  spacing: 10
```

### Resulting Schema (conceptual)

```
schema
├── templates[0]
│     name = "temperature"
│     card_type = "card"
│     style = "temp_style"
│     widgets[0]
│         type = "label"
│         text = "{{value}} °C"
│         style = NULL
└── layout
      columns = 2
      spacing = 10
```

---

## 7. Component Structure

```
yaml_ui/
├── CMakeLists.txt
├── include/
│   └── yaml_ui.h
└── src/
    └── yaml_ui.c
```

---

## 8. Responsibilities of Layer 2

### Layer 2 DOES:
- Interpret YAML meaning
- Validate schema
- Extract templates, widgets, layout
- Produce a structured UI schema

### Layer 2 DOES NOT:
- Create LVGL objects
- Apply styles
- Bind sensor values
- Perform layout calculations

Those responsibilities belong to Layer 3.

---

## 9. Why This Layer Matters

This layer gives you:

- A clean declarative UI language
- A stable contract between YAML and LVGL
- A schema that can evolve independently of the parser
- A structure that AI agents can reliably ingest and generate

It ensures that UI logic is data-driven, not hardcoded.

---

## 10. Next Layer

The next document (`03-lvgl-builder.md`) defines how the UI schema is converted into:

- LVGL objects
- Styled widgets
- Dynamic sensor cards
- Layouted screens

This is where the UI becomes visible.
