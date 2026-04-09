# YamUI Project Handoff — Session Context

## What This Document Is
This is a handoff document for continuing work on the YamUI project across Claude Code sessions. It captures all decisions made, work completed, and next steps so a new session can resume without re-exploring the codebase.

---

## Project Overview
**YamUI** is an ESP-IDF firmware for ESP32-P4 and ESP32-S3 that generates LVGL UI from YAML syntax at runtime. It has a 3-layer architecture:
1. `yaml_core` — Custom lightweight YAML parser -> `yml_node_t` tree
2. `yaml_ui` — Schema interpreter -> `yui_schema_t` domain model (state, styles, translations, components, screens)
3. `lvgl_yaml_gui` — LVGL builder -> live widgets on display (~2000 lines)

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

### 4. yamui_loader Component — FULLY IMPLEMENTED
**Location:** `components/yamui_loader/`

All submodules are complete and production-ready:

| Module | File | Status |
|--------|------|--------|
| **Main coordinator** | `src/yamui_loader.c` | Complete — init, load_best, apply_yaml, config |
| **Filesystem** | `src/yamui_loader_fs.c` | Complete — LittleFS mount/save/load/delete |
| **UART listener** | `src/yamui_loader_uart.c` | Complete — [YAML][LEN][PAYLOAD][CRC32] protocol |
| **HTTP server** | `src/yamui_loader_httpd.c` | Complete — POST/GET/DELETE /api/yaml, GET /api/status |
| **HTTPS client** | `src/yamui_loader_https.c` | Complete — fetch + optional polling task |
| **Public API** | `include/yamui_loader.h` | Complete — clean API with config struct |
| **Build** | `CMakeLists.txt` | Complete — conditional compilation via Kconfig |
| **Config** | `Kconfig` | Complete — all options with defaults |

**Integration with app_main.c:** Done — `yamui_loader_init()` + `yamui_loader_load_best()` called on boot with fallback to `kc_touch_gui_show_root()`.

### 5. ESP-IDF Build — WORKING on Windows
**Build environment:** ESP-IDF v5.5.2 on Windows (MinGW64 bash shell)

**Fixes applied in this session:**
- Created `idf_run.py` helper to strip MSYSTEM env vars and set proper tool paths
- Set `PYTHONIOENCODING=utf-8` and `PYTHONUTF8=1` for Unicode output
- Copied `as-xespv2p2.exe` -> `as.exe` and `objdump-xespv2p2.exe` -> `objdump.exe` in riscv32 toolchain (20251107 build ships without these)
- Set `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=n` to target rev3+ (avoids xesppie assembler mismatch)
- Set `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y` for Waveshare board
- Set `CONFIG_PARTITION_TABLE_OFFSET=0x10000` for larger bootloader
- Made `esp_http_server` unconditional in CMakeLists.txt REQUIRES (conditional `if(CONFIG_...)` wasn't evaluated in time during first CMake pass)

**Build result:** `YamUI.bin` — 1.37MB, 30% free in app partition

### 6. Commits Pushed
```
83f3948 - Update Web IDE template to match firmware showcase demo
3392738 - Fix tests for new showcase template and WebSerial types
```

### 7. HTTPS Polling Auto-Start Fix
- Added `yamui_loader_start_https_poll()` call to `yamui_loader_init()` when HTTPS is enabled and URL is configured
- Added disabled-stub for `yamui_loader_start_https_poll()` when `CONFIG_YAMUI_LOADER_HTTPS_ENABLE=n`

---

## What Has NOT Been Started Yet

### Web IDE Enhancements (Phase 2)
- Device connection panel (WebSerial + HTTP push)
- Missing widget property editors in Canvas/PropertyInspector for newer types

### ESP32-S3 Support (Phase 3)
- `sdkconfig.defaults.esp32s3`
- SPI LCD display backend in `kc_touch_display`

---

## Environment Notes

### ESP-IDF Build on Windows (for next session)
The build uses a Python helper script to work around MinGW compatibility issues.

```bash
cd /c/Code/kc-touch
# Build (uses idf_run.py helper that strips MSYSTEM and sets tool paths)
/c/Espressif/python_env/idf5.5_py3.11_env/Scripts/python.exe idf_run.py build
```

**Key paths:**
- ESP-IDF: `C:\Users\d0773\esp\v5.5.2\esp-idf`
- Tools: `C:\Espressif\tools`
- Python env: `C:\Espressif\python_env\idf5.5_py3.11_env`
- RISCV GCC: `C:\Espressif\tools\riscv32-esp-elf\esp-14.2.0_20251107`

**If idf_run.py is missing**, recreate it — it sets:
- `IDF_PATH`, `IDF_TOOLS_PATH`, `IDF_PYTHON_ENV_PATH`
- Strips `MSYSTEM`, `MSYSTEM_CHOST`, etc.
- Adds riscv32-esp-elf, xtensa-esp-elf, cmake, ninja to PATH
- Sets `PYTHONIOENCODING=utf-8`, `PYTHONUTF8=1`

### Web IDE
```bash
# Backend
cd tools/yam-ui-generator/backend && poetry install && YAMUI_ASSET_ROOT=/tmp/yamui-assets poetry run uvicorn yam_ui_generator.api:app --host 0.0.0.0 --port 8000

# Frontend
cd tools/yam-ui-generator/frontend && npm ci && npm run dev
```

---

## Immediate Next Step for New Session
1. Verify build: `cd /c/Code/kc-touch && /c/Espressif/python_env/idf5.5_py3.11_env/Scripts/python.exe idf_run.py build`
2. If build succeeds: start on Phase 2 (Web IDE device connection panel) or Phase 3 (ESP32-S3 support)
3. Or commit the latest changes if not yet committed
