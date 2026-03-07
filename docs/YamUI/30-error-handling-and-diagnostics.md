# Q — YamUI Error Handling & Diagnostics System  
**Build-Time Validation, Runtime Logging, Recovery Strategies, and Developer Diagnostics**

This document defines the **YamUI Error Handling & Diagnostics System**, the subsystem responsible for detecting, reporting, and recovering from errors in both the build pipeline and the runtime engine.

YamUI is designed for embedded systems where:

- reliability is critical  
- debugging tools are limited  
- UI failures must not crash the device  
- errors must be visible and actionable  

This system ensures YamUI remains stable, predictable, and diagnosable in production environments.

---

# 1. Overview

YamUI handles errors in two phases:

1. **Build-time errors**  
   - YAML validation  
   - schema violations  
   - missing components/screens  
   - invalid expressions  
   - invalid actions  
   - missing assets  

2. **Runtime errors**  
   - expression evaluation failures  
   - missing state keys  
   - missing components  
   - invalid event handlers  
   - failed native function calls  
   - LVGL object creation errors  

Build-time errors **stop the build**.  
Runtime errors **never crash the UI** — YamUI recovers gracefully.

---

# 2. Build-Time Error Handling

The YamUI Build Pipeline (Section P) performs strict validation.

### 2.1 Schema Validation Errors

Triggered when YAML violates the YamUI schema.

Examples:

- missing `type` in a widget  
- missing `widgets` in a screen  
- invalid layout type  
- unknown widget type  

Error format:

```
[Error] screens/home.yaml:12: Unknown widget type 'labell'
```

### 2.2 Component & Screen Resolution Errors

Examples:

- referencing a component that doesn’t exist  
- duplicate component names  
- duplicate screen names  

### 2.3 Style & Theme Errors

Examples:

- referencing a style that doesn’t exist  
- invalid style property  
- circular style inheritance  

### 2.4 Expression Errors

Expressions are parsed at build time.

Examples:

```
{{wifi.status = 'connected'}}   # invalid operator
{{sensor.pH + }}                # syntax error
```

### 2.5 Action Errors

Examples:

- unknown action name  
- wrong number of arguments  
- invalid argument types  

### 2.6 Asset Errors

Examples:

- missing image file  
- unsupported font format  
- invalid asset path  

### Build Behavior

- build stops immediately  
- error includes file + line number  
- no bundle is generated  

---

# 3. Runtime Error Handling

Runtime errors are **non-fatal**.  
YamUI logs them and continues running.

---

## 3.1 Expression Evaluation Errors

Examples:

- division by zero  
- referencing missing state keys  
- invalid type operations  

Behavior:

- expression evaluates to empty string or zero  
- widget receives fallback value  
- warning logged  

---

## 3.2 Missing State Keys

If a widget references a missing key:

```yaml
text: "{{wifi.signal_strength}}"
```

Behavior:

- resolves to empty string  
- warning logged  
- UI continues normally  

---

## 3.3 Missing Component Definitions

If a component instance references a missing component:

```yaml
- type: WifiCardX
```

Behavior:

- placeholder LVGL object created  
- warning logged  
- UI continues  

---

## 3.4 Invalid Event Handlers

Examples:

- unknown event name  
- malformed action list  

Behavior:

- event ignored  
- warning logged  

---

## 3.5 Failed Native Function Calls

If a function is not registered:

```yaml
call(wifi_connect)
```

Behavior:

- warning logged  
- action skipped  
- UI continues  

If the function throws an error:

- caught by YamUI  
- logged  
- remaining actions still execute  

---

## 3.6 LVGL Object Creation Failures

Rare, but possible if:

- memory is low  
- invalid parameters are passed  

Behavior:

- object creation skipped  
- placeholder object created  
- warning logged  

---

# 4. Diagnostics & Logging

YamUI provides a structured logging system.

### 4.1 Log Levels

| Level | Description |
|--------|-------------|
| `ERROR` | Critical issue; operation failed |
| `WARN` | Non-fatal issue; UI continues |
| `INFO` | High-level runtime events |
| `DEBUG` | Detailed internal behavior |
| `TRACE` | Extremely verbose (optional) |

### 4.2 Log Categories

| Category | Description |
|----------|-------------|
| `parser` | YAML parsing & schema validation |
| `state` | state updates & missing keys |
| `expr` | expression evaluation |
| `event` | event dispatching |
| `action` | action execution |
| `lvgl` | LVGL object creation |
| `modal` | modal stack behavior |
| `nav` | navigation transitions |

### 4.3 Log Output Options

- UART console  
- ring buffer  
- in-memory debug panel (future)  
- persistent flash logs (optional)  

---

# 5. Developer Diagnostics Tools

YamUI includes optional diagnostics features.

---

## 5.1 Expression Debug Mode

Shows:

- expression source  
- evaluated result  
- referenced state keys  

Example log:

```
[expr] "{{wifi.status}}" -> "connected"
```

---

## 5.2 Layout Debug Mode

Draws bounding boxes around widgets.

Useful for:

- debugging flex layouts  
- verifying grid alignment  
- diagnosing spacing issues  

---

## 5.3 State Change Logging

Logs every state mutation:

```
[state] wifi.status = "connecting"
[state] wifi.status = "connected"
```

---

## 5.4 Event Trace Mode

Logs all events:

```
[event] button_1 on_click
[action] set(wifi.status, "connecting")
```

---

## 5.5 Modal Stack Debugging

Logs modal push/pop:

```
[modal] push AlertDialog
[modal] pop AlertDialog
```

---

# 6. Recovery Strategies

YamUI uses several strategies to ensure UI stability.

### 6.1 Fallback Values

If a widget property fails to evaluate:

- text → empty string  
- number → 0  
- boolean → false  

### 6.2 Placeholder Widgets

If a widget or component fails to render:

- a neutral placeholder is created  
- UI layout remains intact  

### 6.3 Action Skipping

If an action fails:

- remaining actions still run  
- UI does not freeze  

### 6.4 Modal & Navigation Safety

- invalid `pop()` does nothing  
- invalid `goto()` logs a warning  
- invalid `modal()` creates a placeholder  

---

# 7. Example Error Scenarios

### 7.1 Missing Component

```yaml
- type: WifiCardX
```

Log:

```
[WARN] component 'WifiCardX' not found
```

### 7.2 Invalid Expression

```yaml
text: "{{wifi.status = 'connected'}}"
```

Log:

```
[ERROR] invalid expression in text: '=' is not allowed
```

### 7.3 Failed Native Call

```yaml
call(wifi_connect)
```

Log:

```
[WARN] native function 'wifi_connect' not registered
```

---

# 8. Summary

The YamUI Error Handling & Diagnostics System provides:

- strict build-time validation  
- safe runtime recovery  
- structured logging  
- detailed diagnostics modes  
- fallback behavior for all failures  
- non-crashing UI guarantees  
- developer-friendly debugging tools  

This system ensures YamUI applications remain **stable, predictable, and diagnosable**, even on constrained embedded hardware.
