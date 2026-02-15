# Example YAML Template File
**Demonstration of Templates, Widgets, Styles, and Layout**

This document provides a complete example YAML file (`ui_templates.yml`) used by the YAML-driven LVGL UI engine. It highlights sensor templates, widget definitions, variable substitution, layout configuration, and style references.

---

## 1. Full Example YAML

```yaml
# ============================================================
# YAML-Driven LVGL UI Templates
# Example file for ESP32-P4 declarative UI engine
# ============================================================

sensor_templates:

  # ----------------------------------------------------------
  # Temperature Sensor Template
  # ----------------------------------------------------------
  temperature:
    type: card
    style: temp_card_style

    widgets:
      - type: label
        text: "{{name}}"
        style: title_style

      - type: label
        text: "{{value}} °C"
        style: value_style

      - type: label
        text: "Address: {{address}}"
        style: meta_style

  # ----------------------------------------------------------
  # Humidity Sensor Template
  # ----------------------------------------------------------
  humidity:
    type: card
    style: humidity_card_style

    widgets:
      - type: label
        text: "{{name}}"
        style: title_style

      - type: label
        text: "{{value}} %"
        style: value_style

  # ----------------------------------------------------------
  # Light Sensor Template (BH1750)
  # ----------------------------------------------------------
  light:
    type: card
    style: light_card_style

    widgets:
      - type: label
        text: "{{name}}"
        style: title_style

      - type: label
        text: "{{value}} lux"
        style: value_style

  # ----------------------------------------------------------
  # Pressure Sensor Template (BMP280)
  # ----------------------------------------------------------
  pressure:
    type: card
    style: pressure_card_style

    widgets:
      - type: label
        text: "{{name}}"
        style: title_style

      - type: label
        text: "{{value}} hPa"
        style: value_style

# ============================================================
# Layout Configuration
# ============================================================

layout:
  columns: 2
  spacing: 12
```

---

## 2. Key Concepts

### Templates

Each entry under `sensor_templates` describes how to render a sensor type:

```yaml
temperature:
  type: card
  style: temp_card_style
```

### Widgets

Templates include a `widgets` sequence describing UI elements:

```yaml
widgets:
  - type: label
    text: "{{value}} °C"
    style: value_style
```

### Variable Substitution

Widgets can reference sensor data using placeholders (`{{name}}`, `{{value}}`, `{{unit}}`, `{{address}}`).

### Layout

Global layout controls card arrangement:

```yaml
layout:
  columns: 2
  spacing: 12
```

---

## 3. Minimal Variant

```yaml
sensor_templates:
  temperature:
    type: card
    widgets:
      - type: label
        text: "{{value}} °C"

layout:
  columns: 1
  spacing: 8
```

---

## 4. Future Extensions

Planned additions may include explicit `styles:` blocks and new widget types such as `icon`, `chart`, or `button`.

---

## 5. Summary

This YAML sample serves as the canonical reference for crafting declarative UI definitions. It demonstrates how templates, widgets, variable substitution, and layout cooperate to produce dynamic LVGL dashboards driven by live sensor data.
