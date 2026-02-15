# Sensor Registry
**Dynamic Discovery of Atlas Scientific EZO Sensors**

EZO circuits identify themselves via the `?i` command, so deterministic I²C registries are no longer needed. This document explains how the firmware now discovers, parses, and stores any EZO sensor that responds on the bus.

---

## 1. Protocol Overview

Sending ASCII `"i"` yields responses like `1 ?i,pH,1.98 0`. Tokens expose success, type, and firmware version, enabling dynamic identification.

---

## 2. Data Model

```c
typedef struct {
    char type_name[16];
    char name[32];
    char firmware[16];
    uint8_t address;
} sensor_t;

typedef struct {
    sensor_t *items;
    size_t count;
} sensor_list_t;
```

---

## 3. Discovery Flow

1. Iterate I²C addresses 1–126.
2. For any device that ACKs, send `"i"`.
3. Delay 300 ms (per EZO spec).
4. Read response, parse type + firmware.
5. Append to `sensor_list_t`.

---

## 4. Command Helpers

```c
static esp_err_t ezo_send_i(uint8_t addr);
static int ezo_read_response(uint8_t addr, char *buf, size_t len);
```

Both wrap standard ESP-IDF I²C calls with the correct delays/timeouts.

---

## 5. Response Parsing

```c
static bool ezo_parse_info(const char *resp,
                           char *out_type,
                           char *out_fw);
```

Ensures the string begins with `1 ?i,`, extracts the type between commas, and captures firmware up to the next space/terminator.

---

## 6. Adding Sensors

```c
static void add_sensor(sensor_list_t *list,
                       const char *type,
                       const char *fw,
                       uint8_t addr);
```

Allocates/extends the list, storing `type`, auto-generated `name` (`EZO-<type>`), firmware string, and raw address.

---

## 7. Complete Scan Function

```c
sensor_list_t sensor_manager_scan(void)
{
    sensor_list_t list = {0};

    for (uint8_t addr = 1; addr < 127; addr++) {
        if (!i2c_device_present(addr)) {
            continue;
        }

        if (ezo_send_i(addr) != ESP_OK) {
            continue;
        }

        char resp[64] = {0};
        if (ezo_read_response(addr, resp, sizeof(resp)) <= 0) {
            continue;
        }

        char type[16] = {0};
        char fw[16] = {0};
        if (!ezo_parse_info(resp, type, fw)) {
            continue;
        }

        add_sensor(&list, type, fw, addr);
    }

    return list;
}
```

---

## 8. Supported Sensor Types

Any EZO module that answers `?i`, including pH, EC, RTD, DO, ORP, CO₂, O₂, Flow, Pump, and future releases.

---

## 9. Multiple Instances

Because addresses are fully configurable, multiple identical sensors (e.g., three pH circuits) are naturally supported; each yields a separate `sensor_t` entry.

---

## 10. UI Integration

The LVGL builder matches `sensor_t.type_name` (e.g., `"pH"`) to YAML templates under `sensor_templates:`. Firmware revisions and addresses can be surfaced via placeholders (`{{firmware}}`, `{{address}}`).

---

## 11. Summary

- No static registry table
- Unlimited sensors, any address
- Dynamic parsing of type + firmware
- Seamless feed into the YAML UI pipeline

This is the definitive discovery model for Atlas Scientific EZO deployments.
