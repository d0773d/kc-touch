# M — YamUI YAML Schema  
**Formal Specification of All YamUI YAML Structures and Validation Rules**

This document defines the **complete YAML schema** for YamUI — the authoritative reference for all valid fields, types, structures, and constraints used by the YamUI declarative UI engine.

This schema covers:

- top-level application structure  
- screens  
- components  
- widgets  
- layouts  
- styles  
- themes  
- state  
- actions  
- events  
- expressions  
- native integration  

This is the definitive contract between YAML and the YamUI runtime.

---

# 1. Top-Level Structure

A YamUI file consists of the following optional and required sections:

```yaml
app:                # optional
state:              # optional
styles:             # optional
themes:             # optional
components:         # optional
screens:            # required
```

### 1.1 Top-Level Schema

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `app` | object | no | Application-level configuration |
| `state` | object | no | Global state key/value pairs |
| `styles` | object | no | Named style definitions |
| `themes` | object | no | Theme definitions and overrides |
| `components` | object | no | Reusable component definitions |
| `screens` | object | yes | Screen definitions (must contain at least one) |

---

# 2. `app` Section

```yaml
app:
  initial_screen: home
  theme: dark
```

| Field | Type | Description |
|--------|------|-------------|
| `initial_screen` | string | Name of the screen to load first |
| `theme` | string | Name of the active theme |

---

# 3. `state` Section

Defines global key/value pairs.

```yaml
state:
  wifi.status: "disconnected"
  wifi.ssid: ""
  ui.loading: false
```

### Rules

- Keys must be strings  
- Values may be strings, numbers, or booleans  
- Keys may be nested using dot notation  

---

# 4. `styles` Section

Defines named LVGL style objects.

```yaml
styles:
  title_style:
    text_color: "#FFFFFF"
    text_font: "montserrat_20"
```

### Style Schema

| Field | Type | Description |
|--------|------|-------------|
| `<style_name>` | object | Style definition |
| `extends` | string | Optional parent style |
| `<property>` | varies | LVGL style property |

---

# 5. `themes` Section

Defines theme-level style overrides.

```yaml
themes:
  dark:
    overrides:
      card_style.bg_color: "#1A1A1A"
```

### Theme Schema

| Field | Type | Description |
|--------|------|-------------|
| `<theme_name>` | object | Theme definition |
| `overrides` | object | Key/value overrides for styles |

---

# 6. `components` Section

Defines reusable UI components.

```yaml
components:
  WifiCard:
    props: [ssid, rssi]
    layout: <layout>
    widgets: <widget_list>
```

### Component Schema

| Field | Type | Required | Description |
|--------|------|----------|-------------|
| `<component_name>` | object | yes | Component definition |
| `props` | list of strings | no | Input props |
| `layout` | object | no | Layout rules |
| `widgets` | list | yes | Widget tree |

---

# 7. `screens` Section

Defines top-level screens.

```yaml
screens:
  home:
    layout: <layout>
    widgets: <widget_list>
    on_load: <action_list>
```

### Screen Schema

| Field | Type | Required | Description |
|--------|------|----------|-------------|
| `<screen_name>` | object | yes | Screen definition |
| `layout` | object | no | Layout rules |
| `widgets` | list | yes | Widget tree |
| `on_load` | action or list | no | Lifecycle actions |

---

# 8. Widget Schema

Widgets are defined as:

```yaml
- type: <widget_type>
  <properties...>
```

### Required Field

| Field | Type | Description |
|--------|------|-------------|
| `type` | string | Widget type name |

### Common Widget Fields

| Field | Type | Description |
|--------|------|-------------|
| `id` | string | Unique identifier |
| `style` | string or list | Style(s) to apply |
| `visible` | expression | Conditional visibility |
| `align` | string | Alignment rule |
| `width` | number or expression | Width |
| `height` | number or expression | Height |
| `padding` | number | Padding |
| `margin` | number | Margin |
| `on_*` | action or list | Event handlers |

---

# 9. Widget Types

### 9.1 Core Widgets

| Type | Required Fields | Description |
|------|------------------|-------------|
| `label` | `text` | Text display |
| `button` | `text` | Clickable button |
| `img` | `src` | Image/icon |
| `spacer` | `size` | Empty space |

### 9.2 Input Widgets

| Type | Fields |
|------|--------|
| `textarea` | `placeholder`, `password_mode`, `on_change` |
| `keyboard` | `target` |
| `switch` | `value`, `on_change` |
| `slider` | `min`, `max`, `value`, `on_change` |

### 9.3 Display Widgets

| Type | Fields |
|------|--------|
| `bar` | `min`, `max`, `value` |
| `arc` | `value` |
| `chart` | `series` |

### 9.4 Container Widgets

| Type | Fields |
|------|--------|
| `row` | `gap`, `align`, `justify`, `widgets` |
| `column` | `gap`, `align`, `justify`, `widgets` |
| `grid` | `columns`, `spacing`, `widgets` |
| `panel` | `widgets` |
| `page` | `scroll`, `widgets` |
| `tabview` | `tabs` |

---

# 10. Layout Schema

Layouts appear inside:

- screens  
- components  
- container widgets  

### Layout Types

```yaml
layout:
  type: row | column | grid | page | panel | none
```

### Layout Fields

| Field | Type | Applies To |
|--------|------|------------|
| `type` | string | all |
| `gap` | number | row/column |
| `align` | string | row/column |
| `justify` | string | row/column |
| `columns` | number | grid |
| `spacing` | number | grid |
| `scroll` | string | page |

---

# 11. Event Schema

Events are attached to widgets:

```yaml
on_click: <action>
on_change: <action_list>
```

### Supported Events

- `on_click`
- `on_press`
- `on_release`
- `on_change`
- `on_focus`
- `on_blur`
- `on_load` (screens only)

---

# 12. Action Schema

Actions follow the form:

```
action_name(arg1, arg2, ...)
```

### Supported Actions

| Action | Arguments | Description |
|--------|-----------|-------------|
| `set` | key, value | Update state |
| `goto` | screen | Replace screen |
| `push` | screen | Push screen |
| `pop` | none | Pop screen |
| `modal` | component | Show modal |
| `close_modal` | none | Close modal |
| `call` | function, args | Call native function |
| `emit` | event, args | Emit custom event |

Actions may be a single string or a list.

---

# 13. Expression Schema

Expressions appear inside:

- `{{ ... }}`  
- widget properties  
- actions  
- visibility rules  
- style bindings  
- layout values  

### Supported Expression Types

- identifiers (`wifi.status`)  
- literals (`"text"`, `42`, `true`)  
- arithmetic (`a + b`)  
- comparisons (`a == b`)  
- boolean logic (`a && b`)  
- ternary (`cond ? a : b`)  
- null coalescing (`a ?? b`)  

---

# 14. Validation Rules

### 14.1 Required Sections

- `screens` must exist  
- at least one screen must be defined  

### 14.2 Required Fields

- widgets must have `type`  
- screens must have `widgets`  
- components must have `widgets`  

### 14.3 Type Rules

- numeric fields must be numbers or expressions  
- boolean fields must be booleans or expressions  
- style names must reference existing styles  
- component names must reference existing components  
- screen names must reference existing screens  

### 14.4 Expression Rules

- expressions must be valid  
- identifiers must resolve to state or props  
- invalid expressions evaluate to empty string  

---

# 15. Example: Full Valid YamUI File

```yaml
app:
  initial_screen: home
  theme: dark

state:
  wifi.status: "disconnected"
  wifi.ssid: ""

styles:
  title_style:
    text_color: "#FFFFFF"
    text_font: "montserrat_22"

components:
  WifiCard:
    props: [ssid, rssi]
    layout:
      type: row
      gap: 8
    widgets:
      - type: label
        text: "{{ssid}}"
      - type: label
        text: "{{rssi}} dBm"

screens:
  home:
    layout:
      type: column
      gap: 12
    widgets:
      - type: label
        text: "Welcome"
        style: title_style

      - type: button
        text: "Setup WiFi"
        on_click: goto(wifi_setup)

  wifi_setup:
    widgets:
      - type: WifiCard
        ssid: "{{wifi.ssid}}"
        rssi: -62
```

---

# 16. Summary

The YamUI YAML Schema defines:

- the full structure of YamUI files  
- all valid fields and types  
- widget, component, screen, and layout rules  
- event and action syntax  
- expression semantics  
- validation constraints  

This schema is the authoritative reference for building **complete, declarative LVGL applications** using YamUI.
