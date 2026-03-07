# F — YamUI Expression Engine  
**Template Expressions, Evaluation Rules, and Data Binding**

This document defines the **YamUI Expression Engine**, the subsystem responsible for evaluating template expressions inside widgets, components, and layout properties.

Expressions allow YamUI to bind UI elements to:

- **state** (`{{wifi.status}}`)  
- **props** (`{{ssid}}`)  
- **computed values** (`{{sensor.pH + 1}}`)  
- **conditions** (`{{wifi.status == 'connected'}}`)  
- **boolean logic** (`{{wifi.enabled && wifi.status != 'error'}}`)  

This engine is the foundation of YamUI’s **reactive UI model**, enabling dynamic updates without re-rendering entire screens.

---

# 1. Overview

Expressions appear inside double curly braces:

```
{{ expression }}
```

They can be used in:

- widget properties  
- visibility rules  
- style conditions  
- event arguments  
- component props  
- layout values  

Example:

```yaml
text: "Connected to {{wifi.ssid}}"
visible: "{{wifi.status == 'connected'}}"
value: "{{sensor.pH + 1}}"
```

Expressions are evaluated:

- at initial render  
- whenever referenced state changes  
- whenever props change (inside components)  

---

# 2. Expression Syntax

The expression engine supports:

### ✔ Identifiers  
```
wifi.status
sensor.pH
ui.theme
```

### ✔ Literals  
```
"hello"
42
3.14
true
false
```

### ✔ Arithmetic  
```
a + b
value * 2
(sensor.pH - 7) * 10
```

### ✔ Comparisons  
```
wifi.status == "connected"
sensor.pH > 7
```

### ✔ Boolean logic  
```
wifi.enabled && wifi.status != "error"
```

### ✔ Parentheses  
```
(wifi.status == "connected") && ui.ready
```

### ✔ Ternary operator  
```
wifi.status == "connected" ? "Online" : "Offline"
```

### ✔ Null coalescing  
```
wifi.ssid ?? "Unknown"
```

---

# 3. Expression Evaluation Model

Expressions are evaluated using a lightweight embedded interpreter.

### 3.1 Evaluation Steps

1. **Parse** the expression into an AST (abstract syntax tree)  
2. **Resolve identifiers** from:
   - props (highest priority)
   - state (global)
3. **Evaluate** operators  
4. **Return** a string, number, or boolean  
5. **Apply** the result to the widget property  

### 3.2 Types

The engine supports:

- string  
- number (int/float)  
- boolean  

Type coercion rules follow JavaScript-like semantics:

- `"5" + 1` → `"51"`  
- `"5" * 1` → `5`  
- `true == 1` → `true`  

---

# 4. Identifier Resolution

Identifiers are resolved in this order:

### 1. **Props**  
Inside components:

```
{{ssid}}
{{value}}
```

### 2. **State**  
Global:

```
{{wifi.status}}
{{sensor.pH}}
```

### 3. **Local widget values** (future extension)

If an identifier cannot be resolved, the engine returns an empty string.

---

# 5. Binding Types

Expressions can bind to:

### 5.1 Text  
```yaml
text: "pH: {{sensor.pH}}"
```

### 5.2 Numeric values  
```yaml
value: "{{sensor.pH}}"
```

### 5.3 Visibility  
```yaml
visible: "{{wifi.status == 'connected'}}"
```

### 5.4 Styles  
```yaml
style: "{{wifi.status == 'error' ? 'error_style' : 'normal_style'}}"
```

### 5.5 Layout  
```yaml
width: "{{ui.sidebar_open ? 240 : 0}}"
```

### 5.6 Event arguments  
```yaml
on_click: call(wifi_connect, {{wifi.ssid}}, {{wifi.password}})
```

---

# 6. Reactive Updates

When state changes:

1. YamUI identifies widgets whose expressions reference that state key  
2. Re-evaluates their expressions  
3. Updates the LVGL object in place  

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

- Label updates to `"Status: connected"`  
- No screen reload  
- No re-render of unrelated widgets  

---

# 7. Expression Caching

To optimize performance:

- Expressions are parsed once  
- ASTs are cached  
- Only evaluation happens on state changes  

This keeps the engine efficient on embedded hardware.

---

# 8. Error Handling

If an expression fails:

- YamUI logs a warning  
- The widget receives an empty string or default value  
- The UI continues running  

Invalid expressions never crash the system.

---

# 9. Examples

### 9.1 Conditional text

```yaml
text: "{{wifi.status == 'connected' ? 'Online' : 'Offline'}}"
```

### 9.2 Derived values

```yaml
text: "{{(sensor.pH - 7) * 10}}"
```

### 9.3 Boolean visibility

```yaml
visible: "{{sensor.pH > 7}}"
```

### 9.4 Combined logic

```yaml
visible: "{{wifi.enabled && wifi.status != 'error'}}"
```

### 9.5 Null coalescing

```yaml
text: "{{wifi.ssid ?? 'Unknown Network'}}"
```

---

# 10. Summary

The YamUI Expression Engine provides:

- template expressions (`{{...}}`)  
- arithmetic, comparisons, boolean logic  
- state and prop resolution  
- reactive updates  
- cached AST evaluation  
- safe, embedded-friendly execution  

This engine enables YamUI to deliver a **fully reactive, declarative LVGL UI**, similar to React’s JSX expressions but optimized for embedded systems.
