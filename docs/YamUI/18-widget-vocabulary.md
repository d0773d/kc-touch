# E — YamUI Widget Vocabulary  
**Declarative Mapping of YAML Widgets to LVGL Objects**

This document defines the **YamUI Widget Vocabulary** — the complete set of LVGL widgets that YamUI can generate declaratively from YAML.  
Widgets are the fundamental building blocks of YamUI screens and components.  
Each widget maps directly to an LVGL object, with declarative configuration, styling, layout, events, and state bindings.

This vocabulary enables YamUI to generate **full applications**, including dashboards, Wi-Fi provisioning flows, settings pages, dialogs, and multi-screen interfaces.

---

# 1. Overview

A widget in YamUI is defined by:

```yaml
- type: <widget_name>
  <properties...>
```

Widgets support:

- **props** (from components)  
- **state bindings** (`{{wifi.status}}`)  
- **events** (`on_click`, `on_change`, etc.)  
- **styles** (`style: my_style`)  
- **layout rules** (flex/grid)  
- **visibility conditions** (`visible: {{condition}}`)  

Widgets are rendered into LVGL objects by the YamUI LVGL Builder.

---

# 2. Core Widgets

These are the most commonly used LVGL widgets.

---

## 2.1 `label`

Displays text.

```yaml
- type: label
  text: "Hello World"
  style: title_style
```

Supports:

- `text`
- `style`
- `align`
- `visible`

LVGL mapping:

```c
lv_label_create(parent);
```

---

## 2.2 `button`

Clickable button with optional label.

```yaml
- type: button
  text: "Connect"
  on_click: wifi.connect()
```

Supports:

- `text`
- `style`
- `on_click`
- `on_press`
- `on_release`

LVGL mapping:

```c
lv_btn_create(parent);
lv_label_create(btn);
```

---

## 2.3 `img`

Displays an image or icon.

```yaml
- type: img
  src: "wifi_icon"
```

Supports:

- `src`
- `style`

LVGL mapping:

```c
lv_img_create(parent);
```

---

## 2.4 `camera_preview`

Displays a live camera feed using the active board camera service.

```yaml
- type: camera_preview
  height: 260
```

Supports:

- `width`
- `height`
- `style`
- `visible`

LVGL mapping:

```c
lv_image_create(parent);
```

---

## 2.5 `spacer`

Adds vertical or horizontal space.

```yaml
- type: spacer
  size: 12
```

LVGL mapping:

```c
lv_obj_create(parent);
```

---

## 2.6 `spinner`

Animated loading indicator for async operations.

```yaml
- type: spinner
  width: 24
  height: 24
  duration: 800
```

Supports:

- `width`
- `height`
- `duration`
- `arc_sweep`
- `visible_if`

LVGL mapping:

```c
lv_spinner_create(parent);
```

---

## 2.7 `roller`

Scrollable option picker for compact mode/status selection.

```yaml
- type: roller
  options:
    - Eco
    - Balanced
    - Boost
  visible_row_count: 3
  value: "{{state.demo.mode}}"
  on_change: set(demo.mode, {{value}})
```

Supports:

- `options`
- `mode`
- `visible_row_count`
- `value`
- `on_change`

LVGL mapping:

```c
lv_roller_create(parent);
lv_roller_set_options(obj, "Eco\nBalanced\nBoost", LV_ROLLER_MODE_NORMAL);
```

---

## 2.8 `led`

Lightweight status indicator for online/offline or intensity state.

```yaml
- type: led
  color: "#22d3ee"
  value: "{{state.demo.led_level}}"
```

Supports:

- `color`
- `value`
- `width`
- `height`

LVGL mapping:

```c
lv_led_create(parent);
```

---

## 2.9 `table`

Compact grid for structured read-only data.

```yaml
- type: table
  column_widths: [150, 120]
  rows:
    - cells: ["Widget", "State"]
    - cells: ["Camera", "On demand"]
```

Supports:

- `rows`
- `column_widths`
- `width`
- `height`

LVGL mapping:

```c
lv_table_create(parent);
```

---

## 2.10 `chart`

Displays structured series data as a line or bar chart.

```yaml
- type: chart
  chart_type: line
  point_count: 7
  min: 0
  max: 100
  series:
    - color: "#22d3ee"
      values: [18, 36, 52, 47, 61, 74, 68]
```

Supports:

- `chart_type`
- `point_count`
- `min`
- `max`
- `horizontal_dividers`
- `vertical_dividers`
- `series`

LVGL mapping:

```c
lv_chart_create(parent);
```

---

## 2.11 `calendar`

Displays a monthly calendar with optional today and highlighted dates.

```yaml
- type: calendar
  today: "2026-04-02"
  shown_month: "2026-04-01"
  highlighted_dates:
    - "2026-04-07"
    - "2026-04-18"
```

Supports:

- `today`
- `shown_month`
- `highlighted_dates`
- `width`
- `height`

LVGL mapping:

```c
lv_calendar_create(parent);
```

---

## 2.12 `tabview`

Displays multiple child panels behind a tab bar.

```yaml
- type: tabview
  active_tab: 0
  tabs:
    - title: "Status"
      widgets:
        - type: label
          text: "Overview"
```

Supports:

- `tabs`
- `active_tab`
- `tab_bar_position`
- `tab_bar_size`
- `width`
- `height`

LVGL mapping:

```c
lv_tabview_create(parent);
lv_tabview_add_tab(obj, "Status");
```

---

## 2.13 `menu`

Displays a multi-page LVGL menu with a root page and drill-down entries.

```yaml
- type: menu
  root_title: "Settings"
  items:
    - title: "Connectivity"
      subtitle: "Wi-Fi and security"
      page:
        title: "Connectivity"
        widgets:
          - type: label
            text: "SSID: {{state.network.ssid}}"
```

Supports:

- `root_title`
- `root_title_key`
- `header_mode`
- `root_back_button`
- `items`
- `width`
- `height`

LVGL mapping:

```c
lv_menu_create(parent);
lv_menu_page_create(menu, "Settings");
lv_menu_set_load_page_event(menu, cont, page);
```

---

# 3. Input Widgets

Used for Wi-Fi provisioning, settings, forms, etc.

---

## 3.1 `textarea`

Text input field.

```yaml
- type: textarea
  id: password_input
  placeholder: "Password"
  password_mode: true
  on_change: set(wifi.password, {{value}})
```

Supports:

- `placeholder`
- `password_mode`
- `on_change`
- `text`

LVGL mapping:

```c
lv_textarea_create(parent);
```

---

## 3.2 `keyboard`

On-screen keyboard.

```yaml
- type: keyboard
  target: password_input
```

LVGL mapping:

```c
lv_keyboard_create(parent);
```

---

## 3.3 `switch`

Toggle switch.

```yaml
- type: switch
  value: "{{settings.enabled}}"
  on_change: set(settings.enabled, {{value}})
```

LVGL mapping:

```c
lv_switch_create(parent);
```

---

## 3.4 `checkbox`

Checkbox with integrated label text.

```yaml
- type: checkbox
  text: "Enable Alerts"
  value: "{{settings.alerts}}"
  on_change: set(settings.alerts, {{checked}})
```

LVGL mapping:

```c
lv_checkbox_create(parent);
```

---

## 3.5 `slider`

Slider control.

```yaml
- type: slider
  min: 0
  max: 100
  value: "{{brightness}}"
  on_change: set(brightness, {{value}})
```

LVGL mapping:

```c
lv_slider_create(parent);
```

---

# 4. Display Widgets

Used for dashboards, sensor cards, and data visualization.

---

## 4.1 `bar`

Progress bar.

```yaml
- type: bar
  min: 0
  max: 14
  value: "{{sensor.pH}}"
```

LVGL mapping:

```c
lv_bar_create(parent);
```

---

## 4.2 `arc`

Circular progress indicator.

```yaml
- type: arc
  value: "{{battery.level}}"
```

LVGL mapping:

```c
lv_arc_create(parent);
```

---

## 4.3 `chart`

Line, bar, or scatter chart.

```yaml
- type: chart
  series:
    - name: ph
      color: "#00FF00"
      values: "{{sensor.pH_history}}"
```

LVGL mapping:

```c
lv_chart_create(parent);
```

---

# 5. Container Widgets

Containers define layout and grouping.

---

## 5.1 `row`

Horizontal flex container.

```yaml
- type: row
  gap: 8
  widgets:
    - type: label
      text: "SSID:"
    - type: label
      text: "{{wifi.ssid}}"
```

LVGL mapping:

```c
lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
```

---

## 5.2 `column`

Vertical flex container.

```yaml
- type: column
  gap: 12
  widgets:
    - type: label
    - type: button
```

LVGL mapping:

```c
lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
```

---

## 5.3 `grid`

Grid layout.

```yaml
- type: grid
  columns: 2
  spacing: 8
  widgets:
    - type: label
    - type: label
```

LVGL mapping:

```c
lv_obj_set_layout(obj, LV_LAYOUT_GRID);
```

---

## 5.4 `panel`

Simple container with optional style.

```yaml
- type: panel
  style: card_style
  widgets:
    - type: label
      text: "Panel Title"
```

LVGL mapping:

```c
lv_obj_create(parent);
```

---

## 5.5 `page`

Scrollable container.

```yaml
- type: page
  widgets:
    - type: label
      text: "Long content..."
```

LVGL mapping:

```c
lv_obj_set_scroll_dir(obj, LV_DIR_VER);
```

---

## 5.6 `list`

Scrollable vertical list container for grouped items or activity feeds.

```yaml
- type: list
  widgets:
    - type: label
      text: "Item one"
    - type: label
      text: "Item two"
```

LVGL mapping:

```c
lv_list_create(parent);
```

---

## 5.7 `tabview`

Tabbed interface.

```yaml
- type: tabview
  tabs:
    - title: "Status"
      widgets: [...]
    - title: "Settings"
      widgets: [...]
```

LVGL mapping:

```c
lv_tabview_create(parent);
```

---

# 6. Dialog Widgets

Used for alerts, confirmations, and modal flows.

---

## 6.1 `dialog`

Generic dialog container.

```yaml
- type: dialog
  title: "Error"
  message: "Connection failed"
  buttons:
    - text: "OK"
      on_click: close_modal()
```

Rendered as a modal component.

---

# 7. Widget Properties (Common)

All widgets support:

| Property | Description |
|----------|-------------|
| `id` | Unique identifier for referencing |
| `style` | Style name from style registry |
| `visible` | Boolean or expression |
| `align` | LVGL alignment |
| `width` / `height` | Fixed or content-based |
| `padding` | Padding around widget |
| `margin` | Margin around widget |
| `on_*` | Event handlers |

---

# 8. Widget Binding

Widgets can bind to:

- **props** (from components)  
- **state** (`{{wifi.status}}`)  
- **expressions** (`{{value > 7}}`)  

Bindings apply to:

- text  
- values  
- visibility  
- styles  
- layout properties  

---

# 9. Summary

The YamUI Widget Vocabulary provides:

- full coverage of LVGL widgets  
- declarative configuration  
- event support  
- state bindings  
- layout containers  
- input controls  
- charts and visualizations  
- dialogs and modals  

This vocabulary enables YamUI to generate **complete LVGL applications** using YAML alone.
