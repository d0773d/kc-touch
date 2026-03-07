# Project Structure
**Directory Layout for YAML-Driven LVGL UI Engine (ESP32-P4)**

This document captures the recommended directory structure for the full YAML-driven LVGL UI system. Each ESP-IDF component maps directly to the layered architecture:

```
Layer 1 → yaml_core
Layer 2 → yaml_ui
Layer 3 → lvgl_yaml_gui
Runtime → sensor_manager, lvgl_port
App → main/
```

---

## 1. Top-Level Layout

```
project/
├── CMakeLists.txt
├── sdkconfig
├── partitions.csv
├── main/
├── components/
│   ├── yaml_core/
│   ├── yaml_ui/
│   ├── lvgl_yaml_gui/
│   ├── sensor_manager/
│   └── lvgl_port/
└── littlefs/        (optional)
```

Each component stays isolated, making ownership clear and testing easy.

---

## 2. `main/` — Application Entry Point

```
main/
├── CMakeLists.txt
├── app_main.c
├── ui_templates.yml
└── app_config.h
```

Responsibilities:
- Boot sequence
- Initialize LVGL and I²C
- Load YAML (embedded or LittleFS)
- Invoke Layer 1 → Layer 2 → Layer 3
- Load LVGL screen and start update loop

---

## 3. `components/yaml_core/` — Layer 1

```
yaml_core/
├── CMakeLists.txt
├── idf_component.yml
├── include/
│   └── yaml_core.h
└── src/
    ├── yaml_core.c
    └── yaml_tree.c
```

Responsibilities:
- Wrap libyaml
- Parse text into generic node tree
- Provide helpers for mappings, sequences, scalars
- Remains schema/UI agnostic

---

## 4. `components/yaml_ui/` — Layer 2

```
yaml_ui/
├── CMakeLists.txt
├── include/
│   └── yaml_ui.h
└── src/
    └── yaml_ui.c
```

Responsibilities:
- Interpret node tree into UI schema
- Validate templates, widgets, layout
- Emit `yml_ui_schema_t`
- No LVGL code

---

## 5. `components/lvgl_yaml_gui/` — Layer 3

```
lvgl_yaml_gui/
├── CMakeLists.txt
├── include/
│   └── lvgl_yaml_gui.h
└── src/
    ├── lvgl_yaml_builder.c
    ├── lvgl_yaml_subst.c
    └── lvgl_yaml_layout.c
```

Responsibilities:
- Convert schema to LVGL objects
- Instantiate cards/widgets
- Apply styles
- Substitute variables (e.g., `{{value}}`)
- Arrange layout (grid/flex)
- No YAML parsing

---

## 6. `components/sensor_manager/`

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

Responsibilities:
- Initialize and scan I²C
- Probe known devices
- Read values periodically
- Normalize to `sensor_list_t`
- No UI or YAML knowledge

---

## 7. `components/lvgl_port/`

```
lvgl_port/
├── CMakeLists.txt
├── include/
│   └── lvgl_port.h
└── src/
    ├── lvgl_port_disp.c
    └── lvgl_port_indev.c
```

Responsibilities:
- Display driver init + flush callbacks
- Touch driver init + input callbacks
- Abstract hardware specifics away from LVGL builder

---

## 8. Optional `littlefs/`

```
littlefs/
└── ui.yml
```

Responsibilities:
- Store YAML templates that can be edited without reflashing
- Mounted at `/littlefs`
- Fallback to embedded YAML if unavailable

---

## 9. Partition Table Example

```
nvs,      data, nvs,     0x9000,   0x5000,
otadata,  data, ota,     0xE000,   0x2000,
app0,     app,  ota_0,   0x10000,  0x180000,
app1,     app,  ota_1,   0x190000, 0x180000,
littlefs, data, littlefs,0x310000, 0x0F0000,
```

---

## 10. Build Flow Recap

```
main/app_main.c
    ↓
Load YAML (embedded/LittleFS)
    ↓
yaml_core (Layer 1)
    ↓    node tree
yaml_ui (Layer 2)
    ↓    UI schema
lvgl_yaml_gui (Layer 3)
    ↓    LVGL screen
lvgl_port → display/touch
```

---

## 11. Benefits

- Strong separation of concerns
- Layer-specific testing
- Declarative UI pipeline that scales
- Sensor logic decoupled from rendering
- AI-friendly organization for future automation

---

## 12. Next Document

`08-example-yaml.md` will showcase a complete YAML template (templates, widgets, styles, layout) that flows through this structure.
