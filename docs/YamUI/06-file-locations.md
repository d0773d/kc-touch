# File Locations
**Where YAML Templates Live and How They Are Loaded**

This document defines the storage strategy for YAML UI templates used by the YAML-driven LVGL UI engine. The system supports two primary storage mechanisms:

1. Embedded YAML (recommended)
2. LittleFS filesystem (optional)

Both approaches feed into the same runtime pipeline:

```
YAML file → buffer → yaml_core → yaml_ui → lvgl_yaml_gui
```

---

## 1. Embedded YAML (Recommended)

Embedded YAML uses ESP-IDF's `EMBED_FILES` to compile templates directly into the firmware image.

### Advantages

- No filesystem required
- Zero runtime I/O
- Immutable and reliable
- Ideal for production firmware

### Minimal Setup

```
main/
└── ui_templates.yml
```

```cmake
idf_component_register(
    SRCS "app_main.c"
    EMBED_FILES "ui_templates.yml"
)
```

```c
extern const uint8_t ui_templates_yml_start[] asm("_binary_ui_templates_yml_start");
extern const uint8_t ui_templates_yml_end[]   asm("_binary_ui_templates_yml_end");

const char *yaml_buf = (const char *)ui_templates_yml_start;
size_t yaml_len = ui_templates_yml_end - ui_templates_yml_start;
```

Pass the buffer to `yaml_parse_tree()` and continue through the pipeline.

---

## 2. LittleFS (Optional)

LittleFS allows updating YAML without reflashing firmware, useful for development or OTA-driven UI tweaks.

### Minimal Setup

```
littlefs/
└── ui.yml
```

Partition entry:

```
littlefs, data, littlefs, 0x310000, 0x0F0000,
```

Mount and load:

```c
esp_vfs_littlefs_conf_t conf = {
    .base_path = "/littlefs",
    .partition_label = "littlefs",
    .format_if_mount_failed = true
};

esp_vfs_littlefs_register(&conf);

size_t yaml_len = 0;
char *yaml_buf = load_file("/littlefs/ui.yml", &yaml_len);
```

---

## 3. Choosing a Strategy

| Feature | Embedded | LittleFS |
|---------|----------|----------|
| Filesystem needed | No | Yes |
| Update without flash | No | Yes |
| Fastest load | Yes | No |
| Production ready | Yes | Optional |
| Development flexibility | Good | Excellent |

Recommended: Embedded for production, LittleFS for rapid iteration.

---

## 4. Hybrid Loader

Load from LittleFS first, fall back to embedded asset:

```c
size_t yaml_len = 0;
char *yaml_buf = load_file("/littlefs/ui.yml", &yaml_len);

if (!yaml_buf) {
    yaml_buf = (char *)ui_templates_yml_start;
    yaml_len = ui_templates_yml_end - ui_templates_yml_start;
}
```

---

## 5. Naming Conventions

Use stable `.yml` filenames such as `ui_templates.yml`, `dashboard.yml`, or `sensors.yml`. Avoid spaces to keep OTA updates simple.

---

## 6. YAML Structure Reminder

Files must contain the schema expected by Layer 2:

```yaml
sensor_templates:
  <template_name>:
    type: <card_type>
    style: <style_name>
    widgets:
      - type: <widget_type>
        text: <text>
        style: <style_name>

layout:
  columns: <int>
  spacing: <int>
```

---

## 7. Summary

- Embedded YAML: fastest, immutable, best for production
- LittleFS: editable, OTA-friendly, best for development
- Hybrid: attempt LittleFS, fallback to embedded asset

The downstream layers stay agnostic to the file location.

---

## 8. Next Document

`07-project-structure.md` will describe the full ESP32-P4 project layout and how each component fits into the build.
