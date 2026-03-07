# W — YamUI Font System  
**Font Discovery, Conversion, Packaging, Style Integration, and Embedded Optimization**

This document defines the **YamUI Font System**, the subsystem responsible for managing fonts in YamUI applications.  
Fonts are a critical part of UI design, affecting readability, aesthetics, and performance — especially on embedded hardware.

The YamUI Font System provides:

- font discovery  
- LVGL-compatible font conversion  
- font packaging  
- glyph range optimization  
- style integration  
- runtime lookup  
- performance-aware font usage  

---

# 1. Overview

YamUI supports LVGL-compatible fonts, which are:

- compact  
- efficient  
- pre-rasterized  
- embedded directly in flash  

Fonts are referenced in YAML styles and loaded at runtime via the Asset Pipeline (Section V).

---

# 2. Font Directory Structure

Fonts live under:

```
/src/ui/assets/fonts
```

### Example

```
assets/fonts/montserrat_16.bin
assets/fonts/montserrat_22.bin
assets/fonts/roboto_14.bin
```

---

# 3. Supported Font Formats

YamUI supports:

| Format | Description |
|--------|-------------|
| `.bin` | LVGL-native binary font (preferred) |
| `.ttf` | TrueType font (converted at build time) |
| `.woff` | Web Open Font Format (converted at build time) |

All non-`.bin` formats are converted during the build pipeline.

---

# 4. Font Conversion Pipeline

When `.ttf` or `.woff` files are detected, YamUI automatically converts them to LVGL fonts.

### Conversion Steps

1. Parse TTF/WOFF  
2. Select glyph ranges  
3. Rasterize glyphs  
4. Generate LVGL `.bin` font  
5. Store metadata in manifest  

### Example Conversion Command (internal)

```
lv_font_conv --size 22 --font montserrat.ttf --range 32-126 --format bin
```

---

# 5. Glyph Range Optimization

To reduce flash usage, YamUI supports **glyph range selection**.

### Default Range

```
32–126 (ASCII printable)
```

### Custom Ranges (optional)

```yaml
fonts:
  montserrat_22:
    file: "montserrat.ttf"
    size: 22
    ranges:
      - "32-126"
      - "160-255"
```

Glyph ranges dramatically affect font size:

| Range | Approx Size |
|--------|-------------|
| ASCII only | 6–12 KB |
| Latin-1 | 12–20 KB |
| Full Unicode | 100–500 KB |

Embedded UIs should avoid full Unicode unless required.

---

# 6. Font Naming Convention

YamUI uses a simple naming scheme:

```
<family>_<size>.bin
```

Examples:

- `montserrat_16.bin`
- `montserrat_22.bin`
- `roboto_14.bin`

This ensures predictable references in YAML.

---

# 7. Font Referencing in YAML

Fonts are referenced in styles:

```yaml
styles:
  title_style:
    text_font: "montserrat_22.bin"
```

Or directly on widgets:

```yaml
- type: label
  text: "Hello"
  style:
    text_font: "roboto_14.bin"
```

---

# 8. Font Loading at Runtime

Fonts are loaded via the Asset Pipeline (Section V).

### Lookup

```c
const yamui_asset_t *font = yamui_get_asset("montserrat_22.bin");
```

### Applying to LVGL

```c
lv_style_set_text_font(style, font->data);
```

If a font is missing:

- a fallback font is used  
- a warning is logged  

---

# 9. Font Manifest

Each font entry includes:

```json
{
  "name": "montserrat_22.bin",
  "size": 8192,
  "glyphs": "32-126",
  "checksum": "d1f3a2c4"
}
```

Used for:

- OTA updates  
- debugging  
- validation  

---

# 10. Font Families & Weights (Optional Extension)

YamUI supports multiple weights:

```
montserrat_16_regular.bin
montserrat_16_bold.bin
montserrat_16_semibold.bin
```

Styles can reference them:

```yaml
styles:
  header_style:
    text_font: "montserrat_16_bold.bin"
```

---

# 11. Performance Considerations

Fonts affect:

- flash usage  
- RAM usage (minimal)  
- rendering speed  
- readability  

### 11.1 Recommended Practices

- use as few font sizes as possible  
- avoid large Unicode ranges  
- prefer 14–22 px sizes for readability  
- avoid embedding multiple families unless needed  

### 11.2 Rendering Cost

LVGL renders pre-rasterized glyphs:

- extremely fast  
- minimal CPU usage  
- no dynamic rasterization  

---

# 12. Example: Full Font Configuration

```
assets/fonts/montserrat.ttf
assets/fonts/roboto.ttf
```

### YAML

```yaml
fonts:
  montserrat_22:
    file: "montserrat.ttf"
    size: 22
    ranges:
      - "32-126"

  roboto_14:
    file: "roboto.ttf"
    size: 14
    ranges:
      - "32-126"
```

### Resulting Files

```
montserrat_22.bin
roboto_14.bin
```

---

# 13. Summary

The YamUI Font System provides:

- LVGL-compatible font conversion  
- glyph range optimization  
- deterministic naming  
- asset packaging  
- runtime lookup  
- style integration  
- performance-aware font usage  

This system ensures YamUI applications have **efficient, readable, and embedded-friendly typography**, suitable for production firmware.
