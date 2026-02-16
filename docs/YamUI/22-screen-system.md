# I — YamUI Screen System  
**Declarative Screen Definitions, Lifecycle, and Rendering**

This document defines the **YamUI Screen System**, the top-level structural unit of a YamUI application.  
Screens represent full-page views such as:

- Home dashboard  
- Wi-Fi provisioning  
- Settings  
- Sensor pages  
- Diagnostics  
- Onboarding flows  

Screens integrate with the Navigation System (Section D), the State System (Section B), and the Component System (Section A) to form a complete application framework.

---

# 1. Overview

A **screen** in YamUI is a named, top-level UI definition that contains:

- layout rules  
- widget tree  
- optional lifecycle events  
- optional state bindings  
- optional navigation actions  

Screens are declared in YAML under the `screens:` section.

Example:

```yaml
screens:
  home:
    layout:
      type: column
      gap: 12
    widgets:
      - type: label
        text: "Welcome"
```

Screens are rendered into the LVGL root object and managed by the Navigation System.

---

# 2. Screen Definition Syntax

Screens are defined as:

```yaml
screens:
  <screen_name>:
    layout: <layout_definition>
    widgets: <widget_list>
    on_load: <actions>
```

### Required fields

| Field | Description |
|--------|-------------|
| `widgets` | The widget tree for the screen |

### Optional fields

| Field | Description |
|--------|-------------|
| `layout` | Layout rules for the screen root container |
| `on_load` | Actions executed when the screen becomes active |
| `visible` | Expression controlling screen visibility (rarely used) |

---

# 3. Initial Screen

The initial screen is defined in the `app:` section:

```yaml
app:
  initial_screen: home
```

If omitted, the first screen in the YAML file is used.

---

# 4. Screen Rendering Pipeline

When a screen becomes active, YamUI performs:

### **1. Create LVGL root container**
A new LVGL object is created:

```c
lv_obj_t *root = lv_obj_create(lv_scr_act());
```

### **2. Apply layout**
If defined:

```yaml
layout:
  type: column
  gap: 12
```

### **3. Render widgets**
The widget tree is rendered recursively.

### **4. Execute `on_load` actions**
If present:

```yaml
on_load:
  - call(wifi_start_scan)
  - set(ui.scanning, true)
```

### **5. Bind state expressions**
All expressions inside the screen are registered for reactive updates.

---

# 5. Screen Lifecycle

Screens support the following lifecycle events:

### 5.1 `on_load`
Executed when the screen becomes active.

Example:

```yaml
on_load:
  - call(wifi_start_scan)
  - set(ui.scanning, true)
```

Used for:

- starting Wi-Fi scans  
- resetting temporary state  
- loading sensor data  
- initializing animations  

### 5.2 `on_unload` (future extension)
Executed when navigating away.

---

# 6. Screen Composition

Screens can contain:

- widgets  
- components  
- nested layouts  
- dynamic bindings  
- event handlers  

Example:

```yaml
screens:
  wifi_setup:
    layout:
      type: column
      gap: 10
    widgets:
      - type: label
        text: "WiFi Setup"

      - type: WifiCard
        ssid: "{{wifi.ssid}}"
        rssi: "{{wifi.rssi}}"

      - type: button
        text: "Scan"
        on_click: goto(wifi_scanning)
```

---

# 7. Screen Visibility (Conditional Screens)

Screens can be conditionally visible:

```yaml
visible: "{{wifi.enabled}}"
```

This is rarely used because navigation usually controls visibility, but it is supported.

---

# 8. Screen Transitions

Transitions are handled by the Navigation System:

### Replace screen

```yaml
on_click: goto(wifi_setup)
```

### Push screen

```yaml
on_click: push(wifi_password)
```

### Pop screen

```yaml
on_click: pop()
```

Transitions are instantaneous unless animations are added (future extension).

---

# 9. Screen + State Integration

Screens can bind to state:

```yaml
text: "Connected to {{wifi.ssid}}"
```

Screens can update state:

```yaml
on_click: set(wifi.status, "connecting")
```

Screens can react to state:

```yaml
visible: "{{wifi.status == 'connected'}}"
```

This enables:

- loading screens  
- success/failure screens  
- reactive dashboards  
- conditional flows  

---

# 10. Example: Full Wi-Fi Provisioning Screens

### 10.1 Home Screen

```yaml
screens:
  home:
    layout:
      type: column
      gap: 12
    widgets:
      - type: label
        text: "Welcome"

      - type: button
        text: "Setup WiFi"
        on_click: goto(wifi_setup)
```

### 10.2 Wi-Fi Setup Screen

```yaml
screens:
  wifi_setup:
    layout:
      type: column
      gap: 8
    widgets:
      - type: label
        text: "WiFi Setup"

      - type: button
        text: "Scan Networks"
        on_click:
          - call(wifi_start_scan)
          - goto(wifi_scanning)
```

### 10.3 Scanning Screen

```yaml
screens:
  wifi_scanning:
    on_load:
      - call(wifi_start_scan)
    widgets:
      - type: label
        text: "Scanning..."
      - type: spinner
```

### 10.4 Network List Screen

```yaml
screens:
  wifi_list:
    widgets:
      - type: WifiListItem
        ssid: "{{network.ssid}}"
        rssi: "{{network.rssi}}"
        on_click:
          - set(wifi.ssid, "{{network.ssid}}")
          - push(wifi_password)
```

### 10.5 Password Screen

```yaml
screens:
  wifi_password:
    widgets:
      - type: textarea
        id: password_input
        placeholder: "Password"
        password_mode: true
        on_change: set(wifi.password, {{value}})

      - type: button
        text: "Connect"
        on_click:
          - call(wifi_connect, {{wifi.ssid}}, {{wifi.password}})
          - goto(wifi_connecting)
```

This demonstrates a complete multi-screen flow.

---

# 11. Summary

The YamUI Screen System provides:

- declarative screen definitions  
- screen lifecycle (`on_load`)  
- integration with navigation  
- integration with state  
- full widget and component support  
- dynamic, reactive screen behavior  
- scalable multi-screen application structure  

This system enables YamUI to function as a **complete LVGL application framework**, not just a UI renderer.
