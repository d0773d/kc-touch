# D — YamUI Navigation System  
**Declarative Screens, Stacks, and Modals for LVGL Applications**

This document defines the **YamUI Navigation System**, the React-like mechanism that manages screens, navigation stacks, modal dialogs, and transitions in a declarative LVGL application.

Navigation is a core part of building full applications such as:

- Wi-Fi provisioning flows  
- multi-step wizards  
- settings menus  
- dashboards  
- dialogs and alerts  
- onboarding sequences  

YamUI provides a simple, declarative, embedded-friendly navigation model inspired by React Router and mobile UI frameworks.

---

# 1. Overview

YamUI supports three navigation primitives:

1. **Screens** — top-level views  
2. **Stack navigation** — push/pop screens  
3. **Modals** — dialogs rendered above the current screen  

Navigation is triggered by **events** and driven by **state**, enabling fully declarative UI flows.

---

# 2. Screen Definitions

Screens are defined under the top-level `screens:` section.

### Example

```yaml
screens:
  home:
    layout:
      type: column
      gap: 12
    widgets:
      - type: label
        text: "Welcome"

  wifi_setup:
    layout:
      type: column
      gap: 8
    widgets:
      - type: label
        text: "WiFi Setup"
```

Each screen contains:

- optional layout  
- widget tree  
- optional lifecycle events (`on_load`)  

Screens are rendered into the LVGL root object.

---

# 3. Initial Screen

The initial screen is defined by:

```yaml
app:
  initial_screen: home
```

If omitted, the first screen in the YAML file is used.

---

# 4. Navigation Actions

Navigation is triggered by actions inside event handlers.

### 4.1 `goto(screen_name)`

Replaces the current screen.

```yaml
on_click: goto(wifi_setup)
```

Equivalent to:

- React Router `navigate("/wifi_setup")`
- Mobile “replace screen”

### 4.2 `push(screen_name)`

Pushes a new screen onto the navigation stack.

```yaml
on_click: push(wifi_password)
```

Used for:

- multi-step flows  
- wizards  
- nested screens  

### 4.3 `pop()`

Returns to the previous screen.

```yaml
on_click: pop()
```

Used for:

- back buttons  
- cancel buttons  
- modal dismissals (if no modal is active)  

---

# 5. Navigation Stack

YamUI maintains an internal stack:

```
[ home ] → [ wifi_setup ] → [ wifi_password ]
```

Operations:

- `push()` → adds to stack  
- `pop()` → removes top  
- `goto()` → replaces top  

The stack ensures:

- predictable back navigation  
- multi-step flows  
- nested screens  

---

# 6. Modal System

Modals are components rendered above the current screen.

### 6.1 Showing a modal

```yaml
on_click: modal(AlertDialog)
```

### 6.2 Closing a modal

```yaml
on_click: close_modal()
```

### 6.3 Modal Definition

Modals are defined as components:

```yaml
components:
  AlertDialog:
    layout:
      type: column
      gap: 8
    widgets:
      - type: label
        text: "{{message}}"
      - type: button
        text: "OK"
        on_click: close_modal()
```

### 6.4 Modal Behavior

- Blocks interaction with underlying screen  
- Does not affect navigation stack  
- Can be nested (stacked modals)  
- Automatically centered  

---

# 7. Lifecycle Events

Screens can define lifecycle events:

### 7.1 `on_load`

Triggered when a screen becomes active.

```yaml
screens:
  wifi_scanning:
    on_load:
      - call(wifi_start_scan)
      - set(ui.scanning, true)
```

### 7.2 `on_unload` (optional future extension)

Triggered when navigating away.

---

# 8. Navigation + State Integration

Navigation can depend on state:

### Conditional visibility

```yaml
visible: "{{wifi.status == 'connected'}}"
```

### Conditional navigation

```yaml
on_click:
  - set(wifi.status, "connecting")
  - goto(wifi_connecting)
```

### Automatic navigation via state change

```yaml
on_change: goto(success_screen)
```

This enables:

- loading screens  
- success/failure flows  
- reactive navigation  

---

# 9. Example: Wi-Fi Provisioning Flow

### Screen 1 — Home

```yaml
- type: button
  text: "Setup WiFi"
  on_click: goto(wifi_setup)
```

### Screen 2 — Wi-Fi Setup

```yaml
- type: button
  text: "Scan Networks"
  on_click:
    - call(wifi_start_scan)
    - goto(wifi_scanning)
```

### Screen 3 — Scanning

```yaml
on_load: call(wifi_start_scan)

widgets:
  - type: label
    text: "Scanning..."
  - type: spinner
```

### Screen 4 — Network List

```yaml
- type: WifiListItem
  ssid: "{{network.ssid}}"
  rssi: "{{network.rssi}}"
  on_click:
    - set(wifi.ssid, "{{network.ssid}}")
    - push(wifi_password)
```

### Screen 5 — Password Entry

```yaml
- type: textarea
  id: password_input
  on_change: set(wifi.password, {{value}})

- type: button
  text: "Connect"
  on_click:
    - call(wifi_connect, {{wifi.ssid}}, {{wifi.password}})
    - goto(wifi_connecting)
```

This is a complete, declarative, React-like navigation flow.

---

# 10. Summary

The YamUI Navigation System provides:

- declarative screen definitions  
- direct navigation (`goto`)  
- stack navigation (`push`, `pop`)  
- modal dialogs (`modal`, `close_modal`)  
- lifecycle events (`on_load`)  
- state-driven navigation  
- LVGL-native rendering  

This system enables YamUI to function as a **full application framework**, supporting complex multi-screen flows with clean, declarative YAML.
