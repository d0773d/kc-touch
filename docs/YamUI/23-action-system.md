# J — YamUI Action System  
**Declarative Commands for State, Navigation, Modals, and Native Integration**

This document defines the **YamUI Action System**, the declarative command layer that powers interactivity in YamUI.  
Actions are executed in response to events (Section C) and enable widgets to:

- update global state  
- navigate between screens  
- open and close modals  
- call native C functions  
- emit custom events  
- chain multiple behaviors  

Actions are the “verbs” of YamUI — the operational layer that turns UI into application logic.

---

# 1. Overview

Actions are declared inside event handlers:

```yaml
on_click: set(wifi.status, "connecting")
```

Or as a list:

```yaml
on_click:
  - set(wifi.status, "connecting")
  - goto(wifi_connecting)
```

Actions are:

- **declarative**  
- **ordered**  
- **side-effectful**  
- **integrated with state, navigation, and modals**  
- **evaluated at runtime**  

---

# 2. Action Syntax

Each action is a function-like expression:

```
action_name(arg1, arg2, ...)
```

Arguments may be:

- literals  
- state bindings  
- expressions  
- props (inside components)  

Example:

```yaml
on_click: call(wifi_connect, {{wifi.ssid}}, {{wifi.password}})
```

---

# 3. Action List

YamUI supports the following built-in actions:

| Action | Description |
|--------|-------------|
| `set(key, value)` | Update global state |
| `goto(screen)` | Replace current screen |
| `push(screen)` | Push screen onto stack |
| `pop()` | Pop current screen |
| `modal(component)` | Show modal dialog |
| `close_modal()` | Close active modal |
| `call(function, args...)` | Call native C function |
| `emit(event, args...)` | Emit custom YamUI event |
| `delay(ms)` | Delay next action (future extension) |
| `animate(target, props...)` | Animate widget (future extension) |

This section documents the first seven, which are implemented in the core engine.

---

# 4. `set(key, value)`  
### Update Global State

Updates a value in the global state store.

```yaml
on_change: set(wifi.ssid, {{value}})
```

Supports:

- strings  
- numbers  
- booleans  
- expressions  

Examples:

```yaml
set(counter.value, {{counter.value + 1}})
set(ui.loading, true)
set(wifi.status, "connected")
```

Triggers reactive UI updates.

---

# 5. `goto(screen)`  
### Replace Current Screen

Navigates to a new screen, replacing the current one.

```yaml
on_click: goto(wifi_setup)
```

Used for:

- main navigation  
- success/failure screens  
- transitions between major app sections  

Stack is not modified.

---

# 6. `push(screen)`  
### Push Screen Onto Navigation Stack

Adds a new screen on top of the stack.

```yaml
on_click: push(wifi_password)
```

Used for:

- multi-step flows  
- wizards  
- nested screens  
- “Next” buttons  

---

# 7. `pop()`  
### Pop Current Screen

Returns to the previous screen.

```yaml
on_click: pop()
```

Used for:

- back buttons  
- cancel buttons  
- modal dismissals (if no modal is active)  

If the stack is empty, `pop()` does nothing.

---

# 8. `modal(component)`  
### Show Modal Dialog

Displays a component as a modal overlay.

```yaml
on_click: modal(AlertDialog)
```

Modals:

- appear above the current screen  
- block interaction with underlying UI  
- do not modify the navigation stack  
- can be nested  

Example modal component:

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

---

# 9. `close_modal()`  
### Close Active Modal

Closes the topmost modal.

```yaml
on_click: close_modal()
```

If no modal is active, this action does nothing.

---

# 10. `call(function, args...)`  
### Call Native C Function

Invokes a registered C function.

```yaml
on_click: call(wifi_start_scan)
```

With arguments:

```yaml
on_click: call(wifi_connect, {{wifi.ssid}}, {{wifi.password}})
```

Used for:

- Wi-Fi provisioning  
- sensor commands  
- hardware control  
- system utilities  

Functions must be registered in the YamUI runtime.

---

# 11. `emit(event, args...)`  
### Emit Custom YamUI Event

Broadcasts a custom event to the application.

```yaml
on_click: emit(network_selected, {{ssid}})
```

Screens or components can listen:

```yaml
on_network_selected:
  - set(ui.selected_network, {{args[0]}})
```

Enables:

- list item selection  
- component-to-component communication  
- decoupled UI logic  

---

# 12. Action Chaining

Multiple actions can be executed in sequence:

```yaml
on_click:
  - set(ui.loading, true)
  - call(wifi_connect, {{wifi.ssid}}, {{wifi.password}})
  - goto(wifi_connecting)
```

Actions run in order, synchronously.

---

# 13. Expressions in Actions

Arguments may contain expressions:

```yaml
set(counter.value, {{counter.value + 1}})
call(wifi_connect, {{wifi.ssid}}, {{wifi.password}})
goto({{ui.next_screen}})
```

Expressions are evaluated before the action executes.

---

# 14. Error Handling

If an action fails:

- YamUI logs a warning  
- The remaining actions still execute  
- The UI continues running  

Actions never crash the system.

---

# 15. Example: Wi-Fi Provisioning Flow

### Start scan

```yaml
on_click:
  - set(ui.scanning, true)
  - call(wifi_start_scan)
  - goto(wifi_scanning)
```

### Select network

```yaml
on_click:
  - set(wifi.ssid, "{{network.ssid}}")
  - push(wifi_password)
```

### Connect

```yaml
on_click:
  - set(ui.loading, true)
  - call(wifi_connect, {{wifi.ssid}}, {{wifi.password}})
  - goto(wifi_connecting)
```

This demonstrates a complete declarative flow.

---

# 16. Summary

The YamUI Action System provides:

- state updates (`set`)  
- navigation (`goto`, `push`, `pop`)  
- modal dialogs (`modal`, `close_modal`)  
- native integration (`call`)  
- custom events (`emit`)  
- expression-driven arguments  
- ordered action chaining  

This system forms the operational backbone of YamUI, enabling fully declarative, interactive LVGL applications.
