# V — YamUI Asset Pipeline  
**Image Processing, Font Conversion, Packaging, Lookup Tables, and Embedded Delivery**

This document defines the **YamUI Asset Pipeline**, the subsystem responsible for discovering, processing, optimizing, packaging, and delivering assets to the YamUI runtime.

Assets include:

- images (PNG, JPG, BMP)  
- icons  
- fonts (LVGL-compatible binaries)  
- binary resources (future extension)  

The pipeline ensures assets are:

- efficiently stored  
- consistently referenced  
- optimized for embedded hardware  
- packaged deterministically  
- accessible at runtime with minimal overhead  

---

# 1. Overview

The YamUI Asset Pipeline performs:

1. **Asset discovery**  
2. **Format validation**  
3. **Image optimization**  
4. **Font conversion**  
5. **Deduplication**  
6. **Binary packaging**  
7. **Manifest generation**  
8. **Runtime lookup table creation**

Assets are bundled into a single binary blob and embedded into firmware or delivered via OTA.

---

# 2. Asset Directory Structure

Assets live under:

```
/src/ui/assets
  /images
  /icons
  /fonts
```

### Example

```
assets/images/wifi.png
assets/icons/arrow_left.png
assets/fonts/montserrat_22.bin
```

---

# 3. Asset Discovery

The build system recursively scans:

```
assets/images/**
assets/icons/**
assets/fonts/**
```

Rules:

- only files with supported extensions are included  
- unsupported files trigger warnings  
- duplicate filenames trigger errors  

---

# 4. Image Processing

YamUI supports:

- PNG (preferred)  
- JPG  
- BMP  

### 4.1 PNG Optimization

PNG files are:

- palette-optimized (if possible)  
- compressed  
- stripped of metadata  
- validated for LVGL compatibility  

### 4.2 JPG Handling

JPGs are:

- recompressed to optimal quality  
- converted to RGB565 (optional)  

### 4.3 BMP Handling

BMPs are:

- converted to LVGL-friendly formats  
- optionally compressed  

---

# 5. Font Conversion

Fonts must be converted to LVGL-compatible binaries.

Supported formats:

- `.bin` (LVGL font binary)  
- `.ttf` (converted at build time)  
- `.woff` (converted at build time)

### 5.1 Conversion Pipeline

```
TTF/WOFF → LVGL Font Converter → .bin
```

### 5.2 Font Manifest

Each font includes:

- name  
- size  
- glyph ranges  
- checksum  

---

# 6. Asset Deduplication

Assets with identical binary content are deduplicated.

### Example

```
wifi.png (in images/)
wifi_icon.png (in icons/)
```

If identical:

- only one copy is stored  
- both names map to the same binary offset  

---

# 7. Asset Packaging

All assets are packed into a single binary file:

```
assets.bin
```

### 7.1 Binary Layout

```
+-------------------+
| Asset Header      |
+-------------------+
| Asset Index Table |
+-------------------+
| Asset Data Blob   |
+-------------------+
```

### 7.2 Asset Header

Contains:

- version  
- asset count  
- checksum  

### 7.3 Asset Index Table

Each entry:

```c
typedef struct {
    char name[64];
    uint32_t offset;
    uint32_t size;
    uint8_t type;   // image, font, etc.
} yamui_asset_index_t;
```

---

# 8. Asset Manifest

Generated alongside the binary:

```
manifest.json
```

### Example

```json
{
  "assets": [
    { "name": "wifi.png", "size": 2048, "type": "image" },
    { "name": "montserrat_22.bin", "size": 8192, "type": "font" }
  ],
  "checksum": "a8f3c1d2",
  "count": 42
}
```

Used for:

- OTA updates  
- debugging  
- validation  

---

# 9. Runtime Asset Lookup

At runtime, YamUI loads the asset index into memory:

```c
const yamui_asset_index_t *yamui_get_asset(const char *name);
```

### 9.1 Image Loading

```c
lv_img_set_src(obj, yamui_get_asset("wifi.png"));
```

### 9.2 Font Loading

```c
lv_style_set_text_font(style, yamui_get_asset("montserrat_22.bin"));
```

### 9.3 Error Handling

If an asset is missing:

- placeholder icon is used  
- warning is logged  

---

# 10. Asset Referencing in YAML

Assets are referenced by filename:

```yaml
- type: img
  src: "wifi.png"
```

Fonts are referenced in styles:

```yaml
styles:
  title_style:
    text_font: "montserrat_22.bin"
```

---

# 11. OTA-Safe Asset Updates

Assets are included in the YamUI bundle (Section P).

OTA update steps:

1. Download new bundle  
2. Validate manifest  
3. Replace existing bundle  
4. Restart UI runtime  

Assets update atomically with the UI.

---

# 12. Performance Considerations

### 12.1 Asset Loading

Assets are loaded directly from flash:

- no RAM duplication  
- LVGL caches decoded images  

### 12.2 Recommended Formats

- PNG for icons  
- JPG for photos  
- LVGL `.bin` for fonts  

### 12.3 Size Optimization Tips

- use indexed PNGs  
- avoid large full-color images  
- use vector-like icons when possible  
- limit font sizes and glyph ranges  

---

# 13. Example Asset Pipeline Output

```
[assets] scanning assets...
[assets] optimizing images...
[assets] converting fonts...
[assets] deduplicating...
[assets] packing assets.bin (112 KB)
[assets] manifest generated
```

---

# 14. Summary

The YamUI Asset Pipeline provides:

- deterministic asset discovery  
- image optimization  
- font conversion  
- deduplication  
- binary packaging  
- manifest generation  
- runtime lookup tables  
- OTA-safe delivery  

This pipeline ensures YamUI applications have **efficient, reliable, and embedded-friendly asset handling**, suitable for production firmware.
