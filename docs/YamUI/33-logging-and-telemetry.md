# T — YamUI Logging & Telemetry System  
**Structured Logging, Telemetry Events, Debug Channels, and Runtime Instrumentation**

This document defines the **YamUI Logging & Telemetry System**, the subsystem responsible for:

- structured runtime logging  
- UI-level telemetry events  
- performance instrumentation  
- error reporting  
- developer diagnostics  
- integration with firmware logging backends  

This system ensures YamUI applications are observable, debuggable, and diagnosable on embedded hardware.

---

# 1. Overview

YamUI provides a unified logging and telemetry layer that supports:

- **structured logs** (subsystem, level, message)  
- **runtime diagnostics** (state changes, events, actions)  
- **performance metrics** (render times, update times)  
- **telemetry events** (screen loads, button presses, errors)  
- **firmware integration** (UART, flash logs, remote logging)  

The system is lightweight and embedded-friendly.

---

# 2. Log Levels

YamUI uses five log levels:

| Level | Description |
|--------|-------------|
| `ERROR` | Critical issue; operation failed |
| `WARN` | Non-fatal issue; UI continues |
| `INFO` | High-level runtime events |
| `DEBUG` | Detailed internal behavior |
| `TRACE` | Extremely verbose (optional) |

Developers can configure the minimum log level at runtime.

---

# 3. Log Categories

Logs are grouped by subsystem:

| Category | Description |
|----------|-------------|
| `parser` | YAML parsing & schema validation |
| `state` | state updates & missing keys |
| `expr` | expression evaluation |
| `event` | event dispatching |
| `action` | action execution |
| `lvgl` | LVGL object creation |
| `nav` | navigation transitions |
| `modal` | modal stack behavior |
| `perf` | performance metrics |
| `native` | native function calls |

This allows fine-grained filtering.

---

# 4. Log Format

Logs follow a structured format:

```
[level] [category] message
```

### Example

```
[INFO] [nav] goto(wifi_setup)
[DEBUG] [expr] "{{wifi.status}}" -> "connected"
[WARN] [native] function 'wifi_connect' not registered
```

---

# 5. Logging API (Firmware Side)

YamUI logs through a simple C API:

```c
void yamui_log(yamui_log_level_t level,
               const char *category,
               const char *fmt, ...);
```

Firmware can override the backend:

```c
void yamui_set_log_sink(yamui_log_sink_t sink);
```

### Supported sinks

- UART console  
- ring buffer  
- flash log  
- remote logging (MQTT/HTTP)  
- silent (disabled)  

---

# 6. Telemetry Events

YamUI emits structured telemetry events for:

- screen loads  
- modal opens/closes  
- button presses  
- state changes  
- errors  
- performance metrics  

### Telemetry Event Format

```json
{
  "type": "screen_load",
  "screen": "wifi_setup",
  "timestamp": 12345678
}
```

### Firmware Callback

```c
void yamui_set_telemetry_callback(yamui_telemetry_fn fn);
```

---

# 7. Screen Load Telemetry

Every time a screen becomes active:

```json
{
  "type": "screen_load",
  "screen": "wifi_scanning"
}
```

Useful for:

- analytics  
- usage tracking  
- debugging navigation flows  

---

# 8. Event Telemetry

Every event handler invocation emits telemetry:

```json
{
  "type": "event",
  "widget": "scan_button",
  "event": "on_click"
}
```

---

# 9. Action Telemetry

Each action in an event handler emits telemetry:

```json
{
  "type": "action",
  "action": "set",
  "args": ["wifi.status", "connecting"]
}
```

This allows tracing full event → action → state flows.

---

# 10. State Change Telemetry

State changes emit telemetry:

```json
{
  "type": "state_change",
  "key": "wifi.status",
  "value": "connected"
}
```

Useful for:

- debugging  
- analytics  
- performance tuning  

---

# 11. Error Telemetry

Errors emit structured telemetry:

```json
{
  "type": "error",
  "category": "expr",
  "message": "invalid operator '='"
}
```

This is essential for OTA-updated UIs.

---

# 12. Performance Telemetry

YamUI emits performance metrics:

### Screen render time

```json
{
  "type": "perf",
  "metric": "render_screen",
  "screen": "wifi_setup",
  "ms": 3.2
}
```

### Widget update time

```json
{
  "type": "perf",
  "metric": "update_widget",
  "widget": "ssid_label",
  "us": 42
}
```

---

# 13. Debug Channels

YamUI supports optional debug channels:

### 13.1 Expression Debug Mode

Logs:

- expression source  
- evaluated result  
- referenced state keys  

### 13.2 Layout Debug Mode

Draws bounding boxes around widgets.

### 13.3 Event Trace Mode

Logs all events and actions.

### 13.4 Modal Debug Mode

Logs modal stack operations.

---

# 14. Telemetry Storage & Export

Telemetry can be:

- streamed live (UART, USB, BLE, Wi-Fi)  
- stored in flash (ring buffer)  
- exported on request  
- uploaded to cloud services  

Firmware decides the backend.

---

# 15. Example: Full Telemetry Flow

User taps “Scan Networks”:

1. Event telemetry  
2. Action telemetry (`set`, `call`, `goto`)  
3. State change telemetry  
4. Screen load telemetry  
5. Performance telemetry  

This produces a complete trace of the interaction.

---

# 16. Summary

The YamUI Logging & Telemetry System provides:

- structured logging  
- subsystem-level categories  
- configurable log levels  
- telemetry events for screens, actions, state, and errors  
- performance instrumentation  
- firmware-level integration  
- developer debug channels  

This system ensures YamUI applications are **observable, diagnosable, and production-ready**, even on constrained embedded hardware.
