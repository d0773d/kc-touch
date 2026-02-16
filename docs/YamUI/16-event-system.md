# C — YamUI Event System  
**Declarative Events and Actions for Interactive LVGL Applications**

This document defines the **YamUI Event System**, the React-like mechanism that connects user interactions to application behavior.  
Events allow widgets to trigger:

- state updates  
- navigation  
- component actions  
- Wi-Fi provisioning commands  
- dialogs and modals  
- dynamic UI changes  

The event system is the glue between **UI structure (components)** and **application logic (state + actions)**.

---

# 1. Overview

In YamUI, events are declared directly in YAML:

```yaml
on_click: set(wifi.status, "connecting")
```

Events are:

- **declarative**  
- **bound to LVGL events**  
- **evaluated at runtime**  
- **able to trigger multiple actions**  
- **integrated with the global state system**  

This is the YamUI equivalent of React’s event handlers (`onClick`, `onChange`, etc.).

---

# 2. Event Syntax

Events are attached to widgets:

```yaml
- type: button
  text: "Connect"
  on_click: wifi.connect()
```

Supported event names:

| Event | LVGL Equivalent | Description |
|-------|------------------|-------------|
| `on_click` | `LV_EVENT_CLICKED` | Button press/release |
| `on_press` | `LV_EVENT_PRESSED` | Touch down |
| `on_release` | `LV_EVENT_RELEASED` | Touch up |
| `on_change` | `LV_EVENT_VALUE_CHANGED` | Sliders, switches, text inputs |
| `on_focus` | `LV_EVENT_FOCUSED` | Keyboard navigation |
| `on_blur` | `LV_EVENT_DEFOCUSED` | Focus lost |
| `on_load` | Screen/component mount | Called when screen loads |

Each event triggers one or more **actions**.

---

# 3. Actions

Actions are small, declarative commands executed when an event fires.

### Supported actions:

| Action | Description |
|--------|-------------|
| `set(key, value)` | Update global state |
| `goto(screen)` | Navigate to another screen |
| `push(screen)` | Push screen onto navigation stack |
| `pop()` | Pop current screen |
| `modal(component)` | Show modal dialog |
| `close_modal()` | Close active modal |
| `call(function)` | Call a registered C function |
| `emit(event)` | Emit custom YamUI event |

Actions can be chained:

```yaml
on_click:
  - set(wifi.status, "connecting")
  - goto(wifi_connecting)
```

---

# 4. State Update Actions

The most common action is `set()`:

```yaml
on_change: set(wifi.ssid, {{value}})
```

This updates global state and triggers reactive UI updates.

### Examples

#### Set a string
```yaml
set(wifi.password, {{value}})
```

#### Set a number
```yaml
set(counter.value, {{counter.value + 1}})
```

#### Set a boolean
```yaml
set(ui.loading, true)
```

---

# 5. Navigation Actions

YamUI supports three navigation models:

### **1. Direct navigation**
```yaml
on_click: goto(wifi_setup)
```

Replaces the current screen.

### **2. Stack navigation**
```yaml
on_click: push(wifi_password)
```

Pushes a new screen on top of the stack.

### **3. Pop**
```yaml
on_click: pop()
```

Returns to the previous screen.

This enables:

- multi-step wizards  
- back buttons  
- modal flows  
- onboarding sequences  

---

# 6. Modal Actions

Modals are components rendered above the current screen.

### Show modal
```yaml
on_click: modal(AlertDialog)
```

### Close modal
```yaml
on_click: close_modal()
```

Modals are ideal for:

- confirmations  
- alerts  
- Wi-Fi password prompts  
- error messages  

---

# 7. Calling Native Functions

YamUI can call registered C functions:

```yaml
on_click: call(wifi_start_scan)
```

Or with arguments:

```yaml
on_click: call(wifi_connect, {{wifi.ssid}}, {{wifi.password}})
```

This is how YamUI integrates with:

- Wi-Fi provisioning  
- sensor commands  
- hardware actions  
- system utilities  

---

# 8. Custom Events

Components can emit custom events:

```yaml
on_click: emit(network_selected)
```

Screens or components can listen:

```yaml
on_network_selected: set(ui.selected_network, {{ssid}})
```

This enables:

- list item selection  
- component-to-component communication  
- decoupled UI logic  

---

# 9. Event Handler Resolution

When an event fires:

1. YamUI finds the widget’s event handler  
2. Parses the action list  
3. Evaluates expressions  
4. Executes actions in order  
5. Updates state or navigation  
6. Triggers reactive UI updates  

This is deterministic and embedded-friendly.

---

# 10. Event Binding (Runtime)

At render time, YamUI attaches LVGL event callbacks:

```c
lv_obj_add_event_cb(obj, yamui_event_dispatcher, LV_EVENT_ALL, widget_ref);
```

The dispatcher:

1. Maps LVGL event → YamUI event name  
2. Looks up the YAML handler  
3. Executes actions  

This keeps the C layer simple and generic.

---

# 11. Example: Wi-Fi Provisioning Flow

### Button to start scan

```yaml
- type: button
  text: "Scan Networks"
  on_click:
    - set(ui.scanning, true)
    - call(wifi_start_scan)
    - goto(wifi_scanning)
```

### Selecting a network

```yaml
- type: WifiListItem
  ssid: "{{network.ssid}}"
  rssi: "{{network.rssi}}"
  on_click:
    - set(wifi.ssid, "{{network.ssid}}")
    - goto(wifi_password)
```

### Connecting

```yaml
- type: button
  text: "Connect"
  on_click:
    - set(ui.loading, true)
    - call(wifi_connect, {{wifi.ssid}}, {{wifi.password}})
    - goto(wifi_connecting)
```

This is a full React-like flow, but declarative and LVGL-native.

---

# 12. Summary

The YamUI Event System provides:

- declarative event handlers  
- a rich action vocabulary  
- state updates  
- navigation  
- modals  
- native function calls  
- custom events  
- reactive UI updates  

This system enables YamUI to function as a **full application framework**, not just a UI renderer.
