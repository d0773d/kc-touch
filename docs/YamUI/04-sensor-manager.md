# Sensor Manager
**Dynamic Sensor Discovery + Runtime Data Model**

The `sensor_manager` component is responsible for discovering sensors at runtime, reading their values, and exposing a clean, uniform data model to the LVGL Builder (Layer 3). It is the bridge between hardware reality and the declarative UI system.

This layer contains no YAML logic and no LVGL logic. It focuses entirely on hardware detection, data acquisition, and sensor abstraction.

---

## 1. Purpose

The Sensor Manager:

- Scans the I²C bus at boot
- Identifies connected sensors
- Determines sensor type and capabilities
- Reads sensor values periodically
- Normalizes all sensors into a unified data model
- Provides data to the LVGL UI builder

This enables the UI to be fully dynamic — the number and type of widgets depends on the sensors physically connected.

---

## 2. Responsibilities

### Sensor Manager DOES:
- Initialize I²C
- Scan for known sensor addresses
- Probe sensors to confirm identity
- Read sensor values
- Store sensor metadata
- Provide a list of sensors to the UI

### Sensor Manager DOES NOT:
- Parse YAML
- Interpret templates
- Build LVGL objects
- Apply styles
- Perform layout

Those belong to Layers 1–3.

---

## 3. Sensor Data Model

All sensors are normalized into a common structure:

```c
typedef struct {
    char type_name[32];   // "temperature", "humidity", etc.
    char name[32];        // "SHT40", "BH1750", etc.
    float value;          // primary reading
    char unit[8];         // "°C", "%", "lux", etc.
    uint8_t address;      // I²C address
} sensor_t;
```

A list of sensors is represented as:

```c
typedef struct {
    sensor_t *items;
    size_t count;
} sensor_list_t;
```

This list is passed directly to the LVGL Builder.

---

## 4. Supported Sensor Types

The system supports any I²C sensor, but the initial built-in set includes:

| Sensor | Type | Address | Values |
|--------|------|---------|--------|
| SHT40 | temperature, humidity | 0x44 | °C, %RH |
| BH1750 | light | 0x23 | lux |
| BMP280 | temperature, pressure | 0x76 | °C, hPa |
| ADS1115 | analog multiplexer | 0x48 | voltage |

---

## 5. Sensor Registry

Each sensor type is defined by a probe + read function pair:

```c
typedef bool (*sensor_probe_fn)(uint8_t addr);
typedef float (*sensor_read_fn)(uint8_t addr);

typedef struct {
    const char *type_name;
    const char *display_name;
    uint8_t i2c_addr;
    sensor_probe_fn probe;
    sensor_read_fn read;
    const char *unit;
} sensor_driver_t;
```

Example entry:

```c
{
    .type_name = "temperature",
    .display_name = "SHT40",
    .i2c_addr = 0x44,
    .probe = sht40_probe,
    .read = sht40_read_temp,
    .unit = "°C"
}
```

---

## 6. I²C Scanning

```c
for (uint8_t addr = 1; addr < 127; addr++) {
    if (i2c_device_present(addr)) {
        check_against_sensor_registry(addr);
    }
}
```

If a device matches a known sensor driver:

- Probe function runs
- On success, the sensor is added to the list

---

## 7. Adding a Sensor

```c
static void add_sensor(sensor_list_t *list,
                       const sensor_driver_t *drv,
                       float value)
{
    list->items = realloc(list->items,
        sizeof(sensor_t) * (list->count + 1));

    sensor_t *s = &list->items[list->count++];

    strncpy(s->type_name, drv->type_name, sizeof(s->type_name));
    strncpy(s->name, drv->display_name, sizeof(s->name));
    s->value = value;
    strncpy(s->unit, drv->unit, sizeof(s->unit));
    s->address = drv->i2c_addr;
}
```

---

## 8. Full Scan Function

```c
sensor_list_t sensor_manager_scan(void)
{
    sensor_list_t list = {0};

    for (size_t i = 0; i < SENSOR_DRIVER_COUNT; i++) {
        const sensor_driver_t *drv = &sensor_drivers[i];

        if (i2c_device_present(drv->i2c_addr)) {
            if (drv->probe(drv->i2c_addr)) {
                float value = drv->read(drv->i2c_addr);
                add_sensor(&list, drv, value);
            }
        }
    }

    return list;
}
```

---

## 9. Periodic Updates

```c
void sensor_manager_update(sensor_list_t *list)
{
    for (size_t i = 0; i < list->count; i++) {
        const sensor_driver_t *drv =
            find_driver_by_name(list->items[i].name);

        if (drv) {
            list->items[i].value =
                drv->read(list->items[i].address);
        }
    }
}
```

The LVGL Builder can poll or subscribe to updates.

---

## 10. Integration with LVGL Builder

```c
lv_obj_t *screen =
    lvgl_yaml_build_screen(schema, &sensor_list);
```

For each sensor:

- Find matching template via `sensor.type_name`
- Build card widget
- Substitute variables
- Add to layout

The Sensor Manager does not know anything about templates or LVGL.

---

## 11. Component Structure

```
sensor_manager/
├── CMakeLists.txt
├── include/
│   └── sensor_manager.h
└── src/
    ├── sensor_manager.c
    ├── sht40.c
    ├── bh1750.c
    ├── bmp280.c
    └── ads1115.c
```

---

## 12. Responsibilities Summary

- Sensor Manager = hardware → data model
- UI Schema = YAML → template model
- LVGL Builder = template model → LVGL objects

Each layer maintains a clean separation of concerns.

---

## 13. Why This Layer Matters

The Sensor Manager enables:

- Dynamic UI generation
- Plug-and-play sensors
- Zero hardcoded UI logic
- Scalable hardware support
- Clean separation of concerns

It is the foundation for a data-driven UI that adapts to the hardware environment.
