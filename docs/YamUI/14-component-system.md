# A — YamUI Component System  
**React-Like Components for Declarative LVGL UI Generation**

This document defines the **YamUI Component System**, the foundational mechanism that enables reusable, composable, React-like UI components for LVGL.  
Components allow YamUI to scale from simple sensor dashboards to full application interfaces, including Wi-Fi provisioning, settings pages, dialogs, and multi-screen navigation.

The component system provides:

- reusable UI blocks  
- props (inputs)  
- composition (components inside components)  
- declarative layout  
- deterministic rendering  
- clean separation of structure and logic  

This is the core building block of the entire YamUI engine.

---

# 1. Overview

A **component** in YamUI is a named, reusable UI definition written in YAML.  
Components accept **props**, render **widgets**, and optionally define **layout rules**.

Components are instantiated anywhere in the UI using:

```yaml
- type: ComponentName
  prop1: value
  prop2: value
```

This system mirrors React’s functional components, adapted for LVGL and embedded constraints.

---

# 2. Component Definition Syntax

Components are defined under the top-level `components:` section.

### Example

```yaml
components:
  WifiCard:
    props: [ssid, rssi]
    layout:
      type: row
      gap: 8
    widgets:
      - type: label
        text: "{{ssid}}"
        style: wifi_name_style

      - type: label
        text: "{{rssi}} dBm"
        style: wifi_rssi_style
```

### Structure

| Field | Description |
|-------|-------------|
| `props` | List of input values the component expects |
| `layout` | Optional layout rules applied to the component root |
| `widgets` | The internal widget tree rendered by the component |

---

# 3. Component Instancing

A component is instantiated by referencing its name in a widget list:

```yaml
- type: WifiCard
  ssid: "MyNetwork"
  rssi: -62
```

Or with dynamic data:

```yaml
- type: WifiCard
  ssid: "{{network.ssid}}"
  rssi: "{{network.rssi}}"
```

This is equivalent to:

```jsx
<WifiCard ssid="MyNetwork" rssi={-62} />
```

---

# 4. Rendering Pipeline

When YamUI encounters a component instance, it performs:

### **1. Component Lookup**
Find the component definition by name.

### **2. Root Object Creation**
Every component becomes an LVGL container:

```c
lv_obj_t *root = lv_obj_create(parent);
```

### **3. Layout Application**
If the component defines a layout, it is applied to the root:

```yaml
layout:
  type: row
  gap: 8
```

→ LVGL flexbox configuration.

### **4. Prop Binding**
Props passed to the component instance are stored in a local map:

```
ssid = "MyNetwork"
rssi = -62
```

### **5. Widget Rendering**
Each widget inside the component uses the prop map for substitution:

```yaml
text: "{{ssid}}"
```

→ "MyNetwork"

### **6. Return LVGL Object**
The component becomes a normal LVGL subtree.

---

# 5. Props

Props are:

- immutable  
- passed from parent to child  
- used only for rendering  
- available to all widgets inside the component  

### Example

```yaml
components:
  SensorCard:
    props: [title, value]
    widgets:
      - type: label
        text: "{{title}}"
      - type: label
        text: "{{value}}"
```

Usage:

```yaml
- type: SensorCard
  title: "pH"
  value: "{{sensor.pH}}"
```

---

# 6. Default Props

Components may define default values:

```yaml
components:
  ButtonPrimary:
    props:
      text: "OK"
      color: "#00AAFF"
    widgets:
      - type: button
        text: "{{text}}"
        style: primary_button_style
```

Usage:

```yaml
- type: ButtonPrimary
```

→ renders a button with text "OK".

---

# 7. Component Composition

Components can contain other components:

```yaml
components:
  WifiListItem:
    props: [ssid, rssi]
    widgets:
      - type: WifiCard
        ssid: "{{ssid}}"
        rssi: "{{rssi}}"
```

This enables:

- list items  
- nested cards  
- reusable UI patterns  
- complex layouts  

---

# 8. Component IDs

Components may expose internal widget IDs:

```yaml
widgets:
  - type: label
    id: title_label
    text: "{{title}}"
```

These IDs can be referenced by events or state updates.

---

# 9. Component Registry (Runtime)

At startup, YamUI builds a registry:

```c
typedef struct {
    char name[32];
    yml_node_t *definition;
} yamui_component_t;
```

Components are stored in a hash map for fast lookup:

```c
yamui_component_t *yamui_get_component(const char *name);
```

---

# 10. Rendering Function (Runtime)

Pseudo-code:

```c
lv_obj_t *yamui_render_component(
    const char *name,
    yamui_props_t *props,
    lv_obj_t *parent)
{
    const yamui_component_t *comp = yamui_get_component(name);

    lv_obj_t *root = lv_obj_create(parent);

    yamui_apply_layout(comp->layout, root);

    yamui_render_widgets(comp->widgets, props, root);

    return root;
}
```

This is the YamUI equivalent of React’s render function.

---

# 11. Benefits

The component system enables:

- reusable UI blocks  
- clean separation of structure and logic  
- scalable UI architecture  
- consistent styling  
- declarative composition  
- dynamic data binding (when state is added)  
- full-app generation (Wi-Fi, settings, dashboards, dialogs)  

This is the foundation of YamUI as a **React-like LVGL UI engine**.

---

# 12. Summary

The YamUI Component System provides:

- React-style components  
- props and composition  
- declarative widget trees  
- LVGL container generation  
- layout inheritance  
- reusable UI patterns  

This system is the backbone of YamUI and enables the creation of full, dynamic, multi-screen LVGL applications using YAML alone.
