# U — YamUI Developer Tooling & CLI  
**Command-Line Tools, Development Utilities, Debugging Aids, and Workflow Automation**

This document defines the **YamUI Developer Tooling & CLI**, a suite of tools designed to support developers building YamUI applications.  
These tools streamline:

- YAML validation  
- bundle generation  
- asset processing  
- expression debugging  
- snapshot testing  
- performance profiling  
- live reload (optional)  
- OTA UI bundle packaging  

The CLI is optional but highly recommended for professional YamUI development.

---

# 1. Overview

The YamUI CLI provides:

- **yamui validate** — schema validation  
- **yamui build** — bundle generation  
- **yamui watch** — live rebuild on file changes  
- **yamui assets** — asset conversion & optimization  
- **yamui snapshot** — snapshot generation & comparison  
- **yamui perf** — performance profiling  
- **yamui doctor** — environment diagnostics  
- **yamui ota** — OTA bundle packaging  

The CLI is designed to run on:

- macOS  
- Linux  
- Windows  
- CI environments  

---

# 2. Installation

The CLI is distributed as a standalone binary:

```
yamui --version
```

Or installed via package manager:

```
pip install yamui-cli
```

Or included as part of the firmware SDK.

---

# 3. `yamui validate` — Schema Validation

Validates all YAML files in the project.

### Usage

```
yamui validate src/ui
```

### Checks Performed

- YAML syntax  
- schema correctness  
- widget types  
- component references  
- screen references  
- style references  
- theme overrides  
- expression syntax  
- action syntax  

### Output Example

```
[OK] 42 YAML files validated
```

### Error Example

```
[ERROR] screens/home.yaml:12 Unknown widget type 'labell'
```

---

# 4. `yamui build` — Bundle Generation

Builds the full YamUI bundle:

```
yamui build src/ui --out ui_bundle
```

### Outputs

```
ui_bundle/
  ui.yaml
  assets.bin
  manifest.json
```

### Options

| Option | Description |
|--------|-------------|
| `--minify` | Remove whitespace & comments |
| `--no-assets` | Skip asset bundling |
| `--debug` | Include debug metadata |
| `--pretty` | Pretty-print merged YAML |

---

# 5. `yamui watch` — Live Rebuild

Watches the UI directory and rebuilds on changes.

### Usage

```
yamui watch src/ui --out ui_bundle
```

### Features

- incremental rebuilds  
- fast expression re-parsing  
- optional auto-flash to device  
- optional auto-reload (future extension)  

---

# 6. `yamui assets` — Asset Processing

Processes images, icons, and fonts.

### Usage

```
yamui assets src/ui/assets --out ui_bundle/assets.bin
```

### Features

- PNG → LVGL-optimized format  
- font conversion  
- asset deduplication  
- asset manifest generation  
- unused asset pruning  

### Example Output

```
[assets] packed 42 assets (112 KB)
```

---

# 7. `yamui snapshot` — Snapshot Testing

Generates and compares UI snapshots.

### Usage

```
yamui snapshot screens/wifi_setup.yaml
```

### Snapshot Types

- widget tree  
- expression evaluation  
- layout bounding boxes  

### Example Output

```
[snapshot] wifi_setup: OK
```

### Failure Example

```
[snapshot] wifi_setup: DIFF (layout changed)
```

---

# 8. `yamui perf` — Performance Profiling

Profiles:

- screen render time  
- modal show/hide time  
- expression evaluation time  
- state update time  

### Usage

```
yamui perf screens/wifi_setup.yaml
```

### Example Output

```
[perf] render wifi_setup: 3.2 ms
[perf] update widget 'ssid_label': 42 µs
```

---

# 9. `yamui doctor` — Environment Diagnostics

Checks:

- YAML parser availability  
- asset tools  
- LVGL font converter  
- permissions  
- environment variables  
- project structure  

### Usage

```
yamui doctor
```

### Example Output

```
[doctor] All systems operational
```

---

# 10. `yamui ota` — OTA Bundle Packaging

Packages a YamUI bundle for OTA delivery.

### Usage

```
yamui ota ui_bundle --version 1.0.3 --out ota/ui_1.0.3.bin
```

### Features

- manifest generation  
- checksum generation  
- version stamping  
- atomic update packaging  

---

# 11. CLI Configuration File

Developers can define defaults in:

```
yamui.config.yaml
```

### Example

```yaml
paths:
  ui: src/ui
  out: ui_bundle

options:
  minify: true
  debug: false
```

---

# 12. Editor Integration

YamUI provides:

- VS Code extension  
- syntax highlighting  
- schema-aware autocomplete  
- expression linting  
- inline error reporting  
- component/screen navigation  

This dramatically improves developer productivity.

---

# 13. CI Integration

Typical CI pipeline:

```
yamui validate src/ui
yamui build src/ui --out ui_bundle
yamui snapshot --verify
yamui perf --threshold 10%
```

CI ensures:

- no schema regressions  
- no snapshot regressions  
- no performance regressions  

---

# 14. Example Developer Workflow

### 1. Edit YAML  
### 2. Run `yamui watch`  
### 3. Device auto-reloads UI  
### 4. Run snapshot tests  
### 5. Run performance tests  
### 6. Commit changes  
### 7. CI validates everything  
### 8. `yamui ota` packages new UI  

This workflow is fast, reliable, and scalable.

---

# 15. Summary

The YamUI Developer Tooling & CLI provides:

- schema validation  
- bundle generation  
- asset processing  
- live rebuild  
- snapshot testing  
- performance profiling  
- environment diagnostics  
- OTA packaging  
- editor integration  
- CI support  

This tooling makes YamUI development **fast, reliable, and professional-grade**, enabling teams to build complex embedded UIs with confidence.
