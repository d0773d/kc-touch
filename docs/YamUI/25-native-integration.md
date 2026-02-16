# L — YamUI Native Integration Layer  
**Bridging Declarative YAML to Native Firmware Logic**

This document defines the **YamUI Native Integration Layer**, the subsystem that connects declarative YAML actions to native C functions inside the firmware.  
This layer enables YamUI to control hardware, sensors, networking, and system services without embedding logic directly into YAML.

The integration layer provides:

- registration of native C functions  
- argument passing from YAML expressions  
- type conversion (string, number, boolean)  
- safe execution and error handling  
- integration with the Action System (`call()`)  
- support for asynchronous operations (future extension)  

This system allows YamUI to remain declarative while still controlling the full capabilities of the ESP32-P4.

---

# 1. Overview

YamUI allows YAML to call native C functions:

```yaml
on_click: call(wifi_start_scan)
```

Or with arguments:

```yaml
on_click: call(wifi_connect, {{wifi.ssid}}, {{wifi.password}})
```

These functions are implemented in firmware and registered with YamUI at startup.

---

# 2. Function Registration

Native functions must be registered before use.

### C API

```c
typedef void (*yamui_native_fn_t)(int argc, const char **argv);

void yamui_register_function(const char *name, yamui_native_fn_t fn);
```

### Example Registration

```c
void app_register_yamui_functions(void) {
    yamui_register_function("wifi_start_scan", wifi_start_scan);
    yamui_register_function("wifi_connect", wifi_connect);
    yamui_register_function("sensor_read", sensor_read);
}
```

### Naming Rules

- names must be unique  
- names must match YAML exactly  
- names should be lowercase with underscores  

---

# 3. Function Signature

Native functions receive:

- argument count  
- argument list (strings)  

### Example

```c
void wifi_connect(int argc, const char **argv) {
    const char *ssid = argv[0];
    const char *password = argv[1];
    wifi_connect_to_network(ssid, password);
}
```

Arguments are always strings; conversion is handled by the function.

---

# 4. Calling Functions from YAML

The `call()` action invokes a registered function.

### No arguments

```yaml
on_click: call(wifi_start_scan)
```

### With arguments

```yaml
on_click: call(wifi_connect, {{wifi.ssid}}, {{wifi.password}})
```

### Mixed literals and expressions

```yaml
on_click: call(log_event, "wifi_connect", {{wifi.ssid}})
```

---

# 5. Argument Evaluation

Arguments may be:

- string literals  
- numbers  
- booleans  
- expressions  
- state bindings  
- props (inside components)  

### Example

```yaml
call(sensor_set_threshold, {{sensor.pH}}, 7.0)
```

Evaluation steps:

1. Parse arguments  
2. Evaluate expressions  
3. Convert to strings  
4. Pass to native function  

---

# 6. Type Conversion

All arguments are passed as strings.

Native functions must convert types as needed:

### Convert to integer

```c
int value = atoi(argv[0]);
```

### Convert to float

```c
float f = atof(argv[1]);
```

### Convert to boolean

```c
bool enabled = strcmp(argv[0], "true") == 0;
```

This keeps the integration layer simple and predictable.

---

# 7. Error Handling

If a function is not registered:

- YamUI logs a warning  
- The action is skipped  
- The UI continues running  

If a function throws an error:

- YamUI catches it  
- Logs the error  
- Continues executing remaining actions  

Native errors never crash the UI.

---

# 8. Asynchronous Operations (Future Extension)

Some operations are asynchronous:

- Wi-Fi scanning  
- network connections  
- sensor reads  
- OTA updates  

Future support will include:

- `await(call(...))`  
- callback registration  
- promise-like state updates  

For now, asynchronous operations must update state manually:

```c
yamui_set("wifi.status", "connected");
```

---

# 9. Example: Wi-Fi Provisioning Integration

### YAML

```yaml
on_click:
  - set(ui.loading, true)
  - call(wifi_connect, {{wifi.ssid}}, {{wifi.password}})
  - goto(wifi_connecting)
```

### C Implementation

```c
void wifi_connect(int argc, const char **argv) {
    const char *ssid = argv[0];
    const char *password = argv[1];

    wifi_start_connection(ssid, password);

    // Later, when connection succeeds:
    yamui_set("wifi.status", "connected");
}
```

---

# 10. Example: Sensor Integration

### YAML

```yaml
on_click: call(sensor_read, "ph")
```

### C

```c
void sensor_read(int argc, const char **argv) {
    const char *type = argv[0];
    float value = read_sensor(type);

    char buf[16];
    snprintf(buf, sizeof(buf), "%.2f", value);

    yamui_set("sensor.pH", buf);
}
```

---

# 11. Example: Logging

### YAML

```yaml
on_click: call(log_event, "button_pressed", {{ui.timestamp}})
```

### C

```c
void log_event(int argc, const char **argv) {
    const char *event = argv[0];
    const char *timestamp = argv[1];
    log_to_flash(event, timestamp);
}
```

---

# 12. Summary

The YamUI Native Integration Layer provides:

- registration of native C functions  
- declarative invocation via `call()`  
- argument passing and expression evaluation  
- safe execution and error handling  
- integration with state and navigation  
- support for hardware, sensors, Wi-Fi, and system logic  

This layer enables YamUI to remain fully declarative while still controlling the complete capabilities of the ESP32-P4 firmware.
