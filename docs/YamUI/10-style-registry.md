# Style Registry
**Mapping YAML Style Names to LVGL Styles**

The Style Registry links declarative style names from YAML to concrete `lv_style_t` instances in firmware. YAML templates can simply specify `style: temp_card_style` and rely on the registry to supply pre-configured LVGL styles.

---

## 1. Purpose

- Centralizes LVGL style definitions
- Maps string identifiers to `lv_style_t *`
- Keeps YAML files declarative
- Gives the LVGL builder a simple lookup API

---

## 2. API (`style_registry.h`)

```c
#pragma once
#include "lvgl.h"

typedef struct {
    const char *name;
    const lv_style_t *style;
} style_entry_t;

void style_registry_init(void);
const lv_style_t *style_registry_get(const char *name);
```

---

## 3. Implementation (`style_registry.c`)

```c
#include "style_registry.h"
#include <string.h>

static lv_style_t temp_card_style;
static lv_style_t humidity_card_style;
static lv_style_t light_card_style;
static lv_style_t pressure_card_style;

static lv_style_t title_style;
static lv_style_t value_style;
static lv_style_t meta_style;

static style_entry_t styles[] = {
    { "temp_card_style",     &temp_card_style },
    { "humidity_card_style", &humidity_card_style },
    { "light_card_style",    &light_card_style },
    { "pressure_card_style", &pressure_card_style },
    { "title_style",         &title_style },
    { "value_style",         &value_style },
    { "meta_style",          &meta_style },
};

static const size_t style_count = sizeof(styles) / sizeof(styles[0]);

void style_registry_init(void)
{
    lv_style_init(&temp_card_style);
    lv_style_set_bg_color(&temp_card_style, lv_color_hex(0x003366));
    lv_style_set_radius(&temp_card_style, 8);
    lv_style_set_pad_all(&temp_card_style, 12);

    lv_style_init(&humidity_card_style);
    lv_style_set_bg_color(&humidity_card_style, lv_color_hex(0x003300));
    lv_style_set_radius(&humidity_card_style, 8);
    lv_style_set_pad_all(&humidity_card_style, 12);

    lv_style_init(&light_card_style);
    lv_style_set_bg_color(&light_card_style, lv_color_hex(0x333300));
    lv_style_set_radius(&light_card_style, 8);
    lv_style_set_pad_all(&light_card_style, 12);

    lv_style_init(&pressure_card_style);
    lv_style_set_bg_color(&pressure_card_style, lv_color_hex(0x330033));
    lv_style_set_radius(&pressure_card_style, 8);
    lv_style_set_pad_all(&pressure_card_style, 12);

    lv_style_init(&title_style);
    lv_style_set_text_color(&title_style, lv_color_hex(0xFFFFFF));
    lv_style_set_text_font(&title_style, &lv_font_montserrat_20);

    lv_style_init(&value_style);
    lv_style_set_text_color(&value_style, lv_color_hex(0xFFFF00));
    lv_style_set_text_font(&value_style, &lv_font_montserrat_24);

    lv_style_init(&meta_style);
    lv_style_set_text_color(&meta_style, lv_color_hex(0xAAAAAA));
    lv_style_set_text_font(&meta_style, &lv_font_montserrat_14);
}

const lv_style_t *style_registry_get(const char *name)
{
    if (!name) {
        return NULL;
    }
    for (size_t i = 0; i < style_count; i++) {
        if (strcmp(styles[i].name, name) == 0) {
            return styles[i].style;
        }
    }
    return NULL;
}
```

---

## 4. Usage in LVGL Builder

```c
const lv_style_t *st = style_registry_get(widget->style);
if (st) {
    lv_obj_add_style(obj, st, LV_PART_MAIN);
}
```

If the style name is missing, the builder simply skips styling.

---

## 5. YAML Mapping

```yaml
style: temp_card_style
```

The builder performs `style_registry_get("temp_card_style")` and applies the resulting `lv_style_t`.

---

## 6. Extending the Registry

1. Declare a new `lv_style_t` variable
2. Initialize it in `style_registry_init()`
3. Add it to `styles[]`
4. Reference it from YAML

```c
static lv_style_t warning_style;

lv_style_init(&warning_style);
lv_style_set_bg_color(&warning_style, lv_color_hex(0x660000));

styles[...] = { "warning_style", &warning_style };
```

---

## 7. Naming Conventions

Use descriptive lowercase identifiers:
- `temp_card_style`
- `value_style`
- `meta_style`
- `warning_style`

Avoid spaces or uppercase to keep YAML simple.

---

## 8. Summary

The style registry lets YAML stay declarative while keeping LVGL style definitions centralized. It is a key piece of the themed, data-driven UI pipeline.
