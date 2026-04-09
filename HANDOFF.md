# YamUI Project Handoff — Session Context

## What This Document Is
This is a handoff document for continuing work on the YamUI project across Claude Code sessions. It captures all decisions made, work completed, and next steps so a new session can resume without re-exploring the codebase.

---

## Project Overview
**YamUI** is an ESP-IDF firmware for ESP32-P4 and ESP32-S3 that generates LVGL UI from YAML syntax at runtime. It has a 3-layer architecture:
1. `yaml_core` — Custom lightweight YAML parser → `yml_node_t` tree
2. `yaml_ui` — Schema interpreter → `yui_schema_t` domain model (state, styles, translations, components, screens)
3. `lvgl_yaml_gui` — LVGL builder → live widgets on display (~2000 lines)

There is also a **Web IDE** at `tools/yam-ui-generator/` (React+Vite frontend, FastAPI backend) for visually designing YAML UIs with drag-and-drop.

## Branch
All work is on: `claude/lvgl-yaml-esp32-PEXgd`

## User Decisions (Confirmed)
1. **Enhance existing Web IDE** — do NOT rebuild from scratch
2. **Firmware first** — implement UART upload, HTTPS download, LittleFS storage before Web IDE enhancements
3. **Both device connectivity methods** — WebSerial (USB/UART for dev) + HTTP Push (WiFi for production)
4. **No SPIFFS** — user explicitly rejected SPIFFS as deprecated. Use **LittleFS** only.

---

## What Has Been Completed

### 1. Web IDE Template Project Updated
**File:** `tools/yam-ui-generator/backend/yam_ui_generator/template_project.py`
- Replaced minimal 1-screen template with full 7-screen showcase matching the firmware's `home.yml`
- Screens: showcase, widgets, table_demo, chart_demo, calendar_demo, tabview_demo, menu_demo
- All widget types demonstrated: label, button, switch, checkbox, dropdown, slider, bar, arc, textarea, keyboard, roller, led, chart, table, calendar, tabview, menu, spacer
- 2 components (stat_card, routine_modal), 16 style tokens, English/Spanish translations

### 2. Backend Tests Updated
**File:** `tools/yam-ui-generator/backend/tests/test_projects.py`
- Updated all tests to reference new showcase template (screen name `showcase` not `main`)
- Updated app name, translation keys, style names
- Allow validation warnings for themed style keys (light.hero vs hero)
- 31/32 tests pass (1 pre-existing failure needs `YAMUI_ASSET_ROOT` env var)

### 3. WebSerial TypeScript Fixes
**File:** `tools/yam-ui-generator/frontend/src/utils/deviceSerial.ts`
- Added WebSerial API type declarations for TypeScript strict mode
- Fixed null safety in serial reader cleanup (reader.releaseLock in finally block)
- Frontend: 49/49 tests pass, typecheck clean, production build succeeds (343KB JS)

### 4. ESP-IDF Toolchain Partially Installed
- ESP-IDF v5.5.2 submodule checked out
- RISC-V cross-compiler installed and working (`riscv32-esp-elf-gcc 14.2.0`)
- Python venv created with all requirements
- OpenOCD installed (needed `libusb-1.0-0` apt package)
- **BLOCKED:** `idf.py set-target esp32p4` fails because it can't reach `components-file.espressif.com` to download LVGL, Waveshare BSP, and LittleFS components

### 5. Commits Pushed
```
83f3948 - Update Web IDE template to match firmware showcase demo
3392738 - Fix tests for new showcase template and WebSerial types
```

---

## What Has NOT Been Started Yet

### Firmware: `yamui_loader` Component (THE MAIN TASK)
This is the core new component that needs to be created:

**Location:** `components/yamui_loader/`

**Structure:**
```
components/yamui_loader/
  CMakeLists.txt
  Kconfig
  include/yamui_loader.h
  src/yamui_loader.c          # Init, load_best, source priority
  src/yamui_loader_uart.c     # UART framing protocol receiver
  src/yamui_loader_https.c    # HTTPS download client
  src/yamui_loader_fs.c       # LittleFS mount/save/load
  src/yamui_loader_httpd.c    # HTTP server for WiFi push from IDE
```

**Key design:**
- `yamui_loader_load_best()`: Check /storage/ui.yml on LittleFS → fall back to embedded via `ui_schemas_get_home()`
- UART protocol: `[YAML 4B][LENGTH 4B LE][PAYLOAD][CRC32 4B LE]`, response `YACK`/`YNAK`
- HTTPS: `esp_http_client` GET → save to LittleFS → reload UI
- HTTP server: `esp_http_server` POST `/api/yaml` for WiFi push from Web IDE
- All reloads go through `kc_touch_gui_dispatch()` for thread safety

**Partition table change needed:**
```csv
# partitions.csv — add LittleFS storage partition
nvs,      data, nvs,     ,      0x6000,
phy_init, data, phy,     ,      0x1000,
factory,  app,  factory, ,      0x1E0000,
storage,  data, 0x82,    ,      0x100000,
```

**Existing APIs to reuse:**
- `kc_touch_gui_dispatch()` — thread-safe UI work queue
- `lvgl_yaml_gui_load_from_file(path)` — already declared in header, loads YAML from filesystem
- `lvgl_yaml_gui_load_default()` — loads embedded schema
- `yaml_core_parse_buffer()` / `yaml_core_parse_file()` — YAML parsing
- `ui_schemas_get_home()` — embedded YAML access

**Integration point:** `main/app_main.c` — replace `kc_touch_gui_show_root()` with `yamui_loader_init()` + `yamui_loader_load_best()`

### Web IDE Enhancements (Phase 2, after firmware)
- Device connection panel (WebSerial + HTTP push)
- Missing widget property editors in Canvas/PropertyInspector for newer types

### ESP32-S3 Support (Phase 3)
- `sdkconfig.defaults.esp32s3`
- SPI LCD display backend in `kc_touch_display`

---

## Environment Notes

### ESP-IDF Build Setup (for next session)
The toolchain is installed at `/root/.espressif/`. To activate:
```bash
export IDF_PATH=/home/user/kc-touch/esp-idf-v5.5.2
export IDF_PYTHON_ENV_PATH=/root/.espressif/python_env/idf5.5_py3.11_env
export PATH="/root/.espressif/tools/riscv32-esp-elf/esp-14.2.0_20251107/riscv32-esp-elf/bin:/root/.espressif/tools/riscv32-esp-elf-gdb/16.3_20250913/riscv32-esp-elf-gdb/bin:/root/.espressif/tools/openocd-esp32/v0.12.0-esp32-20250707/openocd-esp32/bin:/root/.espressif/tools/esp-rom-elfs/20241011:$IDF_PATH/tools:$IDF_PYTHON_ENV_PATH/bin:$PATH"
```

**If the toolchain is gone** (new container), re-run:
```bash
cd esp-idf-v5.5.2
git submodule update --init --depth 1
./install.sh esp32p4
source export.sh
```

### Web IDE
```bash
# Backend
cd tools/yam-ui-generator/backend && poetry install && YAMUI_ASSET_ROOT=/tmp/yamui-assets poetry run uvicorn yam_ui_generator.api:app --host 0.0.0.0 --port 8000

# Frontend
cd tools/yam-ui-generator/frontend && npm ci && npm run dev
```

### Why network was blocked
The proxy JWT token had a hardcoded `allowed_hosts` that didn't include `*.espressif.com`. User set "All domains" in the Claude Code settings but the existing container's token wasn't refreshed. A new session should get a fresh token with unrestricted access.

---

## Full Plan File
The detailed implementation plan is at: `/root/.claude/plans/quizzical-forging-feather.md`
(This may not persist across sessions — the content above is self-sufficient.)

---

## Immediate Next Step for New Session
1. Verify network: `curl -s -o /dev/null -w "%{http_code}" https://components-file.espressif.com/`
2. If 200: `cd /home/user/kc-touch && source esp-idf-v5.5.2/export.sh && idf.py set-target esp32p4 && idf.py build`
3. If build succeeds: start implementing `yamui_loader` component
