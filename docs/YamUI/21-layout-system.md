# H — YamUI Layout System  
**Declarative Flex, Grid, Alignment, and Responsive Layout for LVGL**

This document defines the **YamUI Layout System**, the declarative mechanism that controls widget positioning, spacing, alignment, and container behavior in LVGL.  
The layout system is inspired by React Native’s Flexbox and modern UI frameworks, but optimized for LVGL’s embedded-friendly layout engine.

YamUI supports:

- flex layouts (row/column)  
- grid layouts  
- alignment and justification  
- spacing and padding  
- responsive sizing  
- conditional layout via expressions  
- nested containers  
- layout inheritance inside components  

This system enables YamUI to build complex, scalable, and visually consistent interfaces using YAML alone.

---

# 1. Overview

Every widget in YamUI is placed inside a **layout container**.  
Layouts can be:

- **implicit** (default behavior)  
- **explicit** (row, column, grid, page, panel)  

Example:

```yaml
layout:
  type: column
  gap: 12
```

Widgets inside the container follow the layout rules.

---

# 2. Layout Types

YamUI supports the following layout types:

| Layout | Description |
|--------|-------------|
| `row` | Horizontal flex layout |
| `column` | Vertical flex layout |
| `grid` | Multi-column grid layout |
| `page` | Scrollable container |
| `panel` | Simple container with no layout rules |
| `none` | Absolute positioning (rarely used) |

---

# 3. Flex Layouts (Row & Column)

Flex layouts are the most common and map directly to LVGL’s flexbox engine.

---

## 3.1 Row Layout

Widgets arranged horizontally.

```yaml
- type: row
  gap: 8
  align: center
  widgets:
    - type: label
    - type: button
```

LVGL mapping:

```c
lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
```

---

## 3.2 Column Layout

Widgets arranged vertically.

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

## 3.3 Flex Properties

| Property | Description |
|----------|-------------|
| `gap` | Space between children |
| `align` | Align children (start, center, end) |
| `justify` | Distribute space (start, center, end, space-between) |
| `wrap` | Wrap children to next line (row only) |

### Example

```yaml
layout:
  type: row
  gap: 10
  align: center
  justify: space-between
```

---

# 4. Grid Layout

Grid layout arranges widgets in rows and columns.

### Example

```yaml
- type: grid
  columns: 2
  spacing: 12
  widgets:
    - type: label
    - type: label
    - type: label
    - type: label
```

Properties:

| Property | Description |
|----------|-------------|
| `columns` | Number of columns |
| `spacing` | Space between cells |
| `row_height` | Optional fixed row height |
| `column_width` | Optional fixed column width |

LVGL mapping:

```c
lv_obj_set_layout(obj, LV_LAYOUT_GRID);
```

---

# 5. Page Layout (Scrollable)

Scrollable container for long content.

```yaml
- type: page
  scroll: vertical
  widgets:
    - type: label
      text: "Long content..."
```

Properties:

| Property | Description |
|----------|-------------|
| `scroll` | `vertical`, `horizontal`, or `both` |

LVGL mapping:

```c
lv_obj_set_scroll_dir(obj, LV_DIR_VER);
```

---

# 6. Panel Layout (Neutral Container)

A simple container with no layout rules.

```yaml
- type: panel
  style: card_style
  widgets:
    - type: label
      text: "Inside panel"
```

Used for:

- cards  
- dialogs  
- custom layouts inside components  

---

# 7. Absolute Layout (Advanced)

Rarely used, but supported:

```yaml
layout:
  type: none
```

Widgets must specify:

- `x`
- `y`
- `width`
- `height`

This is for highly custom UI only.

---

# 8. Widget-Level Layout Properties

Widgets can override layout behavior:

### 8.1 Alignment

```yaml
align: center
```

### 8.2 Width / Height

```yaml
width: 200
height: "{{ui.sidebar_open ? 240 : 0}}"
```

### 8.3 Flex Grow / Shrink

```yaml
grow: 1
shrink: 0
```

### 8.4 Padding / Margin

```yaml
padding: 8
margin: 4
```

---

# 9. Responsive Layout (Expressions)

Layout properties can use expressions:

### Conditional width

```yaml
width: "{{ui.sidebar_open ? 240 : 0}}"
```

### Conditional visibility

```yaml
visible: "{{wifi.status == 'connected'}}"
```

### Dynamic spacing

```yaml
gap: "{{ui.compact_mode ? 4 : 12}}"
```

This enables:

- responsive UIs  
- dynamic dashboards  
- collapsible sidebars  
- adaptive Wi-Fi provisioning flows  

---

# 10. Layout in Components

Components can define their own layout:

```yaml
components:
  WifiCard:
    layout:
      type: row
      gap: 8
    widgets:
      - type: label
      - type: label
```

Component layout applies to the component root container.

---

# 11. Nested Layouts

Layouts can be nested arbitrarily:

```yaml
- type: column
  gap: 12
  widgets:
    - type: row
      gap: 8
      widgets:
        - type: label
        - type: button

    - type: grid
      columns: 2
      spacing: 10
      widgets:
        - type: label
        - type: label
```

This enables complex UI structures.

---

# 12. Layout Debugging

YamUI provides optional debugging:

- missing layout warnings  
- invalid property warnings  
- flex/grid resolution logs  
- widget bounding box logs  

Useful during UI development.

---

# 13. Summary

The YamUI Layout System provides:

- flex layouts (row/column)  
- grid layouts  
- scrollable pages  
- panels and neutral containers  
- alignment, spacing, padding, margins  
- responsive layout via expressions  
- nested layout structures  
- component-level layout definitions  

This system enables YamUI to generate **complex, responsive LVGL interfaces** using declarative YAML.
