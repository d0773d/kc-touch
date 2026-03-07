# Example Code
**Full Working Example: `app_main.c`**

This reference shows how to wire LVGL initialization, sensor discovery, YAML parsing, schema interpretation, and LVGL building inside `app_main.c`.

---

## 1. `app_main.c` Example

```c
#include <stdio.h>
#include "lvgl.h"
#include "lvgl_port.h"
#include "sensor_manager.h"
#include "yaml_core.h"
#include "yaml_ui.h"
#include "lvgl_yaml_gui.h"

extern const uint8_t ui_templates_yml_start[] asm("_binary_ui_templates_yml_start");
extern const uint8_t ui_templates_yml_end[]   asm("_binary_ui_templates_yml_end");

void app_main(void)
{
    printf("Booting YAML-driven LVGL UI...\n");

    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();

    i2c_master_init();

    printf("Scanning I2C bus...\n");
    sensor_list_t sensors = sensor_manager_scan();
    printf("Found %d sensors\n", (int)sensors.count);

    const char *yaml_buf = (const char *)ui_templates_yml_start;
    size_t yaml_len = ui_templates_yml_end - ui_templates_yml_start;

    yml_node_t *root = yaml_parse_tree(yaml_buf, yaml_len);
    if (!root) {
        printf("YAML parse error!\n");
        return;
    }

    yml_ui_schema_t *schema = yaml_ui_from_tree(root);
    if (!schema) {
        printf("UI schema error!\n");
        yaml_free_tree(root);
        return;
    }

    lv_obj_t *screen = lvgl_yaml_build_screen(schema, &sensors);
    lv_scr_load(screen);

    while (1) {
        sensor_manager_update(&sensors);
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    yaml_ui_free(schema);
    yaml_free_tree(root);
}
```

---

## 2. Key Steps

1. **LVGL init**: `lv_init()`, `lv_port_*_init()`
2. **Sensor discovery**: `sensor_manager_scan()`
3. **YAML load**: use embedded linker symbols
4. **Layer 1**: `yaml_parse_tree()` → node tree
5. **Layer 2**: `yaml_ui_from_tree()` → schema
6. **Layer 3**: `lvgl_yaml_build_screen()` → screen
7. **Loop**: update sensors + call `lv_timer_handler()`

---

## 3. Minimal YAML Sample

```yaml
sensor_templates:
  temperature:
    type: card
    widgets:
      - type: label
        text: "{{name}}"
      - type: label
        text: "{{value}} °C"

layout:
  columns: 1
  spacing: 8
```

---

## 4. Flow Recap

```
Boot → LVGL init → I²C init → Sensor scan → Load YAML → Layer 1 parse → Layer 2 interpret → Layer 3 build → Update loop
```

Use this as the canonical integration reference.
