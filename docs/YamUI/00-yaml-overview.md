# YAML-Driven UI Overview

This document explains how the declarative UI stack is layered across the project and how each layer cooperates to deliver dynamic LVGL layouts from YAML definitions.

---

## Layer Stack

1. **Layer 1 – YAML Core**
   - Location: `components/yaml_core`
   - Responsibility: Parse raw YAML buffers into a generic node tree using libyaml.
   - Output: `yml_node_t` graph that is agnostic of UI concepts.

2. **Layer 2 – UI Schema Interpreter**
   - Location: `components/yaml_ui`
   - Responsibility: Interpret the generic tree into domain objects (templates, layouts, widgets, styles).
   - Output: `yui_document_t` structure that describes what should be rendered, but not how.

3. **Layer 3 – LVGL Builder**
   - Location: `components/lvgl_yaml_gui`
   - Responsibility: Convert the interpreted schema and sensor data into live LVGL objects.
   - Output: Real screens/cards/widgets mounted into the LVGL display tree.

Supporting pieces:
- **Sensor Manager (`components/sensor_manager`)** supplies live data snapshots used when instantiating templates.
- **Schema Assets (`components/ui_schemas`)** embed YAML files (e.g., `home.yml`) into firmware for offline availability.

---

## Data Flow

```
home.yml (embedded) ──▶ yaml_core ──▶ yaml_ui ──▶ lvgl_yaml_gui ──▶ LVGL display
                                      │                   │
                                      └──── sensor_manager ┘
```

1. `ui_schemas_get_home()` exposes the raw YAML buffer.
2. `yaml_parse_tree()` builds the generic tree.
3. `yaml_ui_load_document()` validates/interprets semantics.
4. `lvgl_yaml_gui_build()` walks the schema, pulls sensor snapshots, and instantiates LVGL objects.

If any step fails, `kc_touch_gui` falls back to the legacy UI so provisioning remains functional.

---

## Design Goals

- **Separation of concerns** – parsing, interpretation, and rendering live in distinct components.
- **Declarative authoring** – UI designers edit YAML instead of touching C code.
- **AI-friendly** – structured docs (like this set) expose the system to automated agents.
- **Embedded ready** – assets are compiled into the binary and require no filesystem access.

---

## Artifact Checklist

- [x] `yaml_core` component & documentation (`01-yaml-core.md`).
- [x] YAML schema interpreter (`yaml_ui`).
- [x] LVGL builder (`lvgl_yaml_gui`).
- [x] Sensor simulation/data source (`sensor_manager`).
- [x] Embedded schema assets (`ui_schemas`).
- [x] Runtime integration in `kc_touch_gui` + `app_main`.

Next documentation: `02-ui-schema.md` (Layer 2 deep dive) and `03-lvgl-builder.md` (Layer 3 specifics).
