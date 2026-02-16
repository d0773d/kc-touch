# P — YamUI Build & Deployment Pipeline  
**YAML Bundling, Asset Packaging, Runtime Initialization, and OTA-Safe UI Delivery**

This document defines the **YamUI Build & Deployment Pipeline**, the complete process for turning a directory of YAML files, components, styles, themes, and assets into a fully functional LVGL UI embedded in firmware.

The pipeline covers:

- YAML bundling and validation  
- asset packaging (images, fonts, icons)  
- build system integration  
- runtime initialization  
- OTA-safe UI updates  
- versioning and caching  
- error handling and fallback behavior  

This is the authoritative reference for how YamUI is built and deployed in production firmware.

---

# 1. Overview

The YamUI build pipeline transforms:

```
/src/ui/*.yaml
/src/ui/screens/*.yaml
/src/ui/components/*.yaml
/src/ui/styles/*.yaml
/src/ui/themes/*.yaml
/src/ui/assets/*
```

into:

- a single embedded YAML blob  
- embedded assets (images, fonts)  
- a runtime-ready UI package  
- a versioned UI bundle for OTA updates  

The pipeline ensures:

- deterministic builds  
- reproducible UI behavior  
- safe updates  
- minimal flash footprint  

---

# 2. Build Pipeline Stages

The YamUI build pipeline consists of:

1. **YAML Collection**  
2. **Schema Validation**  
3. **YAML Merging & Normalization**  
4. **Expression Pre-Parsing**  
5. **Asset Packaging**  
6. **Bundle Generation**  
7. **Firmware Embedding**  
8. **Runtime Initialization**  

Each stage is described below.

---

# 3. YAML Collection

The build system gathers all YAML files from:

```
/src/ui
/src/ui/screens
/src/ui/components
/src/ui/styles
/src/ui/themes
```

Rules:

- all `.yaml` and `.yml` files are included  
- order is deterministic (alphabetical)  
- `app.yaml` is processed first  

---

# 4. Schema Validation

Each YAML file is validated against the **YamUI YAML Schema** (Section M).

Validation checks:

- required sections (`screens`)  
- required fields (`type`, `widgets`)  
- valid widget types  
- valid actions  
- valid expressions  
- valid style references  
- valid component references  

If validation fails:

- build stops  
- error message includes file + line number  

---

# 5. YAML Merging & Normalization

After validation, all YAML files are merged into a single normalized structure.

### Merging Rules

- `app`, `state`, `styles`, `themes`, `components`, `screens` are merged by key  
- later files override earlier ones  
- duplicate component/screen names cause a build error  
- style inheritance is resolved  
- theme overrides are applied  

### Normalization

- expressions are extracted and parsed  
- widget trees are flattened  
- layout presets are resolved  
- component references are validated  

The result is a **fully resolved UI tree**.

---

# 6. Expression Pre-Parsing

All expressions (`{{ ... }}`):

- are parsed into ASTs  
- are validated for syntax  
- have their referenced state keys extracted  
- are cached for runtime  

This reduces runtime overhead and ensures correctness.

---

# 7. Asset Packaging

Assets include:

- images (`.png`, `.jpg`, `.bmp`)  
- icons  
- fonts (`.bin`, `.ttf`, `.woff`)  

### Packaging Rules

- assets are embedded into flash  
- asset paths are normalized  
- unused assets are optionally pruned  
- fonts are converted to LVGL-compatible formats  

### Asset Index

A lookup table is generated:

```c
typedef struct {
    const char *name;
    const uint8_t *data;
    size_t size;
} yamui_asset_t;
```

---

# 8. Bundle Generation

All YAML + assets are packaged into a **YamUI Bundle**.

### Bundle Contents

```
/bundle
  ui.yaml          # merged + normalized YAML
  assets.bin       # packed assets
  manifest.json    # version, checksum, metadata
```

### Manifest Example

```json
{
  "version": "1.0.3",
  "checksum": "a8f3c1d2",
  "screens": 12,
  "components": 18,
  "assets": 42
}
```

The manifest is used for OTA updates and caching.

---

# 9. Firmware Embedding

The bundle is embedded into firmware via:

- CMake  
- ESP-IDF component  
- custom build scripts  

### Example CMake Integration

```cmake
add_custom_command(
  OUTPUT ui_bundle.c
  COMMAND yamui_bundle_tool ${UI_FILES} > ui_bundle.c
  DEPENDS ${UI_FILES}
)

add_library(yamui_bundle STATIC ui_bundle.c)
```

The bundle becomes a static C array:

```c
extern const uint8_t yamui_bundle[];
extern const size_t yamui_bundle_size;
```

---

# 10. Runtime Initialization

At startup, the firmware initializes YamUI:

```c
yamui_init(yamui_bundle, yamui_bundle_size);
```

Initialization steps:

1. load bundle  
2. parse YAML  
3. build registries (screens, components, styles)  
4. initialize state  
5. apply theme  
6. render initial screen  

After initialization, the UI is fully interactive.

---

# 11. OTA-Safe UI Updates

YamUI supports OTA updates of the UI bundle **without reflashing firmware**.

### OTA Update Steps

1. Download new bundle  
2. Validate manifest + checksum  
3. Replace existing bundle in flash  
4. Restart YamUI runtime  
5. Render initial screen  

### Safety Features

- versioned bundles  
- checksum verification  
- fallback to previous bundle on failure  
- atomic swap  

This allows UI updates independent of firmware releases.

---

# 12. Versioning Strategy

Recommended versioning:

```
MAJOR.MINOR.PATCH
```

- **MAJOR** — breaking UI changes  
- **MINOR** — new screens/components  
- **PATCH** — style tweaks, bug fixes  

The manifest stores the version for OTA comparison.

---

# 13. Build Errors & Diagnostics

The build pipeline reports:

- invalid YAML  
- missing components  
- missing screens  
- invalid expressions  
- invalid actions  
- missing assets  
- circular component references  

Errors include:

- file path  
- line number  
- description  

---

# 14. Example Build Output

```
[ YamUI ] Collecting YAML files...
[ YamUI ] Validating schema...
[ YamUI ] Merging components...
[ YamUI ] Parsing expressions...
[ YamUI ] Packing assets...
[ YamUI ] Generating bundle...
[ YamUI ] Build complete: ui_bundle.c (42 KB)
```

---

# 15. Summary

The YamUI Build & Deployment Pipeline provides:

- deterministic YAML merging  
- schema validation  
- expression pre-parsing  
- asset packaging  
- bundle generation  
- firmware embedding  
- runtime initialization  
- OTA-safe UI updates  

This pipeline ensures YamUI applications are **reliable, maintainable, and production-ready** on embedded hardware.
