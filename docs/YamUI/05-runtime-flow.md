# Runtime Flow
**Boot → Sensor Discovery → YAML → UI Schema → LVGL Screen**

This document defines the end-to-end runtime execution flow for the YAML-driven LVGL UI engine on ESP32-P4. It describes how the system initializes, loads YAML templates, discovers sensors, builds the UI, and updates values during operation.

```
Hardware → Sensor Manager → YAML Core → UI Schema → LVGL Builder → Screen
```

---

## 1. Overview of Runtime Stages

1. System Initialization
2. I²C + Sensor Discovery
3. YAML Template Loading
4. YAML Parsing (Layer 1)
5. UI Schema Interpretation (Layer 2)
6. LVGL Screen Construction (Layer 3)
7. Periodic Sensor Updates

---

## 2. Stage 1 — System Initialization

Performed in `app_main()`:

```c
lv_init();
lv_port_disp_init();
lv_port_indev_init();
i2c_master_init();
```

---

## 3. Stage 2 — Sensor Discovery

```c
sensor_list_t sensors = sensor_manager_scan();
```

Generates a list of sensors: type, name, value, unit, address.

---

## 4. Stage 3 — YAML Template Loading

Embedded example:

```c
extern const uint8_t ui_templates_yml_start[] asm("_binary_ui_templates_yml_start");
extern const uint8_t ui_templates_yml_end[]   asm("_binary_ui_templates_yml_end");

size_t len = ui_templates_yml_end - ui_templates_yml_start;
const char *yaml_buf = (const char *)ui_templates_yml_start;
```

---

## 5. Stage 4 — YAML Parsing (Layer 1)

```c
yml_node_t *root = yaml_parse_tree(yaml_buf, len);
```

Produces a schema-agnostic mapping/sequence/scalar tree.

---

## 6. Stage 5 — UI Schema Interpretation (Layer 2)

```c
yml_ui_schema_t *schema = yaml_ui_from_tree(root);
```

Returns templates, widgets, layout struct.

---

## 7. Stage 6 — LVGL Screen Construction (Layer 3)

```c
lv_obj_t *screen = lvgl_yaml_build_screen(schema, &sensors);
```

- For each sensor: find template → build card → substitute text → apply style → add to layout.
- Load with `lv_scr_load(screen);`

---

## 8. Stage 7 — Periodic Sensor Updates

```c
while (1) {
    sensor_manager_update(&sensors);
    lv_timer_handler();
    vTaskDelay(pdMS_TO_TICKS(100));
}
```

---

## 9. Flow Diagram

```
Boot → Init LVGL/I²C → Discover Sensors → Load YAML → Parse Tree → Interpret Schema → Build Screen → Load & Update
```

---

## 10. Why This Flow Works

- Deterministic stages
- Clear contracts between layers
- Easy debugging and extension
- Fully declarative UI driven by hardware state
