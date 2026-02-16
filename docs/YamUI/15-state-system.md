# B — YamUI State System  
**Global Reactive State for Declarative LVGL Applications**

This document defines the **YamUI State System**, the React-like global state store that powers dynamic, data-driven LVGL interfaces.  
State enables YamUI to update UI elements automatically when data changes, making it possible to build full applications such as Wi-Fi provisioning flows, settings pages, dashboards, and multi-screen navigation.

The state system provides:

- a global key/value store  
- reactive bindings (`{{wifi.status}}`)  
- event-driven updates (`set(wifi.status, "connected")`)  
- automatic widget updates  
- deterministic, embedded-friendly behavior  

This is the YamUI equivalent of React’s `useState()` and global store combined.

---

# 1. Overview

The YamUI State System is a **global, hierarchical key/value store** that UI elements can:

- **read** via template expressions  
- **write** via events  
- **react to** when values change  

Example:

```yaml
state:
  wifi.status: "disconnected"
  wifi.ssid: ""
```

Widgets can bind to state:

```yaml
text: "Status: {{wifi.status}}"
```

Events can update state:

```yaml
on_click: set(wifi.status, "connecting")
```

When state changes, YamUI updates all bound widgets automatically.

---

# 2. State Definition (YAML)

State is defined at the top level:

```yaml
state:
  wifi.status: "disconnected"
  wifi.ssid: ""
  wifi.password: ""
  ui.theme: "dark"
```

### Rules

- Keys are dot-separated paths  
- Values may be strings, numbers, or booleans  
- State is global across all screens and components  
- State is initialized once at startup  

---

# 3. State Access (Bindings)

Widgets can reference state values using template expressions:

```yaml
text: "Connected to {{wifi.ssid}}"
```

Or:

```yaml
visible: "{{wifi.status == 'connected'}}"
```

Or:

```yaml
value: "{{sensor.pH}}"
```

Bindings are evaluated at render time and whenever state changes.

---

# 4. State Updates (Events)

Events can modify state using the `set()` action:

```yaml
on_click: set(wifi.status, "connecting")
```

Or dynamic updates:

```yaml
on_change: set(wifi.ssid, {{value}})
```

Or numeric updates:

```yaml
on_click: set(counter.value, {{counter.value + 1}})
```

This is the YamUI equivalent of React’s `setState()`.

---

# 5. Automatic UI Updates

When a state value changes:

1. YamUI finds all widgets bound to that key  
2. Re-evaluates their template expressions  
3. Updates the LVGL objects in place  

### Example

State:

```
wifi.status = "connecting"
```

Widget:

```yaml
text: "Status: {{wifi.status}}"
```

Event:

```yaml
set(wifi.status, "connected")
```

Result:

- LVGL label updates automatically  
- No screen reload  
- No manual redraw  

This is lightweight, embedded-friendly reactivity.

---

# 6. State Store Structure (Runtime)

The state store is a hierarchical map:

```c
typedef struct {
    char key[64];
    char value[64];
} yamui_state_entry_t;
```

Stored in a dynamic array or hash map:

```c
yamui_state_entry_t *yamui_state;
size_t yamui_state_count;
```

Lookup:

```c
const char *yamui_get(const char *key);
```

Update:

```c
void yamui_set(const char *key, const char *value);
```

---

# 7. Expression Evaluation

Template expressions support:

### **Simple substitution**
```
{{wifi.status}}
```

### **Arithmetic**
```
{{counter.value + 1}}
```

### **Comparisons**
```
{{wifi.status == 'connected'}}
```

### **Boolean logic**
```
{{wifi.enabled && wifi.status == 'connected'}}
```

### **Nested state**
```
{{ui.theme}}
```

Expressions are evaluated using a lightweight embedded expression engine.

---

# 8. Widget Binding Model

Each widget stores:

- its original template string  
- the parsed expression tree  
- a pointer to the LVGL object  
- the state keys it depends on  

When a state key changes:

- only widgets depending on that key update  
- no full re-render  
- no virtual DOM  
- no diffing  

This keeps the system efficient on embedded hardware.

---

# 9. State Change Notifications

When `yamui_set()` is called:

1. Update the value  
2. Identify affected widgets  
3. Recompute their expressions  
4. Apply updates to LVGL  

### Example: Updating a label

```c
lv_label_set_text(widget->obj, new_text);
```

### Example: Updating visibility

```c
lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
```

### Example: Updating numeric values

```c
lv_bar_set_value(obj, new_value, LV_ANIM_OFF);
```

---

# 10. State and Components

Components can read state:

```yaml
text: "{{wifi.status}}"
```

Components can update state:

```yaml
on_click: set(wifi.status, "connected")
```

Components do **not** have local state yet (that comes later), but they can:

- read global state  
- write global state  
- react to global state  

This keeps the system simple and predictable.

---

# 11. State and Navigation

Screens can react to state:

```yaml
visible: "{{wifi.status == 'connected'}}"
```

Navigation events can update state:

```yaml
on_click: set(ui.current_screen, "wifi_setup")
```

This enables:

- conditional screens  
- loading indicators  
- success/failure flows  
- multi-step wizards  

---

# 12. Summary

The YamUI State System provides:

- a global key/value store  
- reactive bindings  
- event-driven updates  
- automatic LVGL widget updates  
- expression evaluation  
- predictable, embedded-friendly behavior  

This system is the backbone of dynamic UI behavior in YamUI and enables full application development using YAML alone.
