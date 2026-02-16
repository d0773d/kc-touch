# G — YamUI Style System  
**Declarative Styling, Themes, and LVGL Style Integration**

This document defines the **YamUI Style System**, the declarative mechanism for defining, organizing, and applying LVGL styles across components, widgets, screens, and themes.

The style system provides:

- named reusable styles  
- declarative style definitions in YAML  
- style inheritance  
- dynamic style binding via expressions  
- theme-level overrides  
- LVGL style object generation  
- style application to widgets and components  

This system enables YamUI to deliver consistent, themeable, and maintainable UI design across the entire application.

---

# 1. Overview

Styles in YamUI are defined in YAML and mapped to LVGL `lv_style_t` objects at runtime.

A style:

- is declared once  
- can be applied to any widget  
- can be overridden by themes  
- supports dynamic selection via expressions  
- maps directly to LVGL style properties  

Example:

```yaml
styles:
  title_style:
    text_color: "#FFFFFF"
    text_font: "montserrat_20"
    pad_all: 4
```

Applied to a widget:

```yaml
- type: label
  text: "WiFi Setup"
  style: title_style
```

---

# 2. Style Definition Syntax

Styles are defined under the top-level `styles:` section.

### Example

```yaml
styles:
  card_style:
    bg_color: "#222222"
    radius: 8
    pad_all: 12

  value_style:
    text_color: "#00FFAA"
    text_font: "montserrat_18"
```

Each style maps to an LVGL style object.

---

# 3. Supported Style Properties

YamUI supports a wide range of LVGL style properties.

### 3.1 Colors

| Property | Description |
|----------|-------------|
| `bg_color` | Background color |
| `text_color` | Text color |
| `border_color` | Border color |
| `outline_color` | Outline color |

### 3.2 Dimensions

| Property | Description |
|----------|-------------|
| `width` | Fixed width |
| `height` | Fixed height |
| `min_width` | Minimum width |
| `min_height` | Minimum height |

### 3.3 Padding & Margin

| Property | Description |
|----------|-------------|
| `pad_all` | Padding on all sides |
| `pad_x` | Horizontal padding |
| `pad_y` | Vertical padding |
| `pad_top` | Top padding |
| `pad_bottom` | Bottom padding |
| `pad_left` | Left padding |
| `pad_right` | Right padding |
| `margin` | External spacing |

### 3.4 Borders

| Property | Description |
|----------|-------------|
| `border_width` | Border thickness |
| `border_color` | Border color |
| `radius` | Corner radius |

### 3.5 Text

| Property | Description |
|----------|-------------|
| `text_font` | Font name |
| `text_color` | Text color |
| `text_align` | Left/center/right |

### 3.6 Misc

| Property | Description |
|----------|-------------|
| `opacity` | 0–255 |
| `shadow_width` | Shadow size |
| `shadow_color` | Shadow color |

---

# 4. Style Application

A widget applies a style using:

```yaml
style: card_style
```

Multiple styles can be applied:

```yaml
style:
  - base_style
  - highlight_style
```

Styles are applied in order.

---

# 5. Dynamic Style Binding

Styles can be selected using expressions:

```yaml
style: "{{wifi.status == 'error' ? 'error_style' : 'normal_style'}}"
```

This enables:

- error highlighting  
- active/selected states  
- theme switching  
- conditional formatting  

---

# 6. Style Inheritance

Styles can extend other styles:

```yaml
styles:
  base_card:
    bg_color: "#222"
    radius: 8

  sensor_card:
    extends: base_card
    pad_all: 12
```

Inheritance rules:

- child overrides parent  
- multiple inheritance is not allowed  
- inheritance is resolved at load time  

---

# 7. Themes

Themes allow global style overrides.

### Example

```yaml
themes:
  dark:
    overrides:
      card_style.bg_color: "#1A1A1A"
      title_style.text_color: "#FFFFFF"

  light:
    overrides:
      card_style.bg_color: "#FFFFFF"
      title_style.text_color: "#000000"
```

Active theme:

```yaml
app:
  theme: dark
```

Themes can be switched at runtime:

```yaml
on_click: set(ui.theme, "light")
```

YamUI automatically reapplies affected styles.

---

# 8. Style Registry (Runtime)

At startup, YamUI builds a registry:

```c
typedef struct {
    char name[32];
    lv_style_t style;
} yamui_style_t;
```

Stored in a hash map:

```c
yamui_style_t *yamui_get_style(const char *name);
```

Styles are created once and reused.

---

# 9. LVGL Style Generation

Each YAML style is converted into an LVGL `lv_style_t`:

```c
lv_style_init(&style);
rv_style_set_bg_color(&style, lv_color_hex(bg_color));
lv_style_set_radius(&style, radius);
lv_style_set_pad_all(&style, pad_all);
...
```

Widgets apply styles using:

```c
lv_obj_add_style(obj, &style, LV_PART_MAIN);
```

---

# 10. Style Debugging

YamUI provides optional debugging:

- missing style warnings  
- invalid property warnings  
- theme override logs  
- inheritance resolution logs  

This ensures predictable styling.

---

# 11. Example: Wi-Fi Setup Styles

```yaml
styles:
  title_style:
    text_color: "#FFFFFF"
    text_font: "montserrat_22"
    pad_bottom: 8

  card_style:
    bg_color: "#2A2A2A"
    radius: 10
    pad_all: 12

  error_style:
    text_color: "#FF4444"
```

Usage:

```yaml
- type: label
  text: "WiFi Setup"
  style: title_style

- type: panel
  style: card_style
  widgets:
    - type: label
      text: "{{wifi.status}}"
      style: "{{wifi.status == 'error' ? 'error_style' : 'value_style'}}"
```

---

# 12. Summary

The YamUI Style System provides:

- declarative style definitions  
- reusable named styles  
- dynamic style binding  
- theme support  
- inheritance  
- LVGL style object generation  
- consistent, maintainable UI design  

This system enables YamUI to deliver a **fully themeable, declarative LVGL UI**, suitable for complete applications.
