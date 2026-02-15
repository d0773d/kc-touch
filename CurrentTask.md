# Current Task Status

## Objective
- [ ] Migrate project build environment from Windows 11 to Linux VPS.

## Context & State
- **Last Action:** `idf.py build` failed due to missing `xespv` extension flag on `esp_gdbstub`.
- **Current Focus:** Fixing compilation flags for ESP32-P4.

## Next Steps
1. Add `espv` or `xespv` to the architecture flags in `CMakeLists.txt` for ESP32-P4.
2. Re-run local build.



