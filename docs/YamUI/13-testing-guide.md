# Testing Guide
**Unit + Integration Coverage for YAML UI + Dynamic EZO Discovery**

The stack is split into sensor discovery plus three logical layers (YAML core, UI schema, LVGL builder). Each piece must be independently verifiable, with deterministic mocks for hardware-facing code.

---

## 1. Philosophy

```
Sensor Manager → YAML Core → UI Schema → LVGL Builder
```

- Unit tests exercise pure logic.
- Integration tests cross layer boundaries.
- Hardware-in-the-loop optional for smoke tests.
- Tests must be deterministic, isolated, and memory-safe.

---

## 2. Directory Layout

```
tests/
├── test_yaml_core/
├── test_yaml_ui/
├── test_lvgl_builder/
├── test_sensor_manager/
└── mocks/
    ├── mock_i2c.c
    ├── mock_i2c.h
    └── mock_ezo_responses.h
```

---

## 3. Sensor Manager Tests

### Mock I²C Layer

Provide fake `i2c_device_present`, `i2c_master_write_to_device`, `i2c_master_read_from_device` so discovery code can be exercised without hardware.

### Mock Responses

```c
#define RESP_PH  "1 ?i,pH,1.98 0"
mock_ezo_map[0x63] = RESP_PH;
```

### Test Cases

- Single pH sensor.
- Multiple sensors of same type.
- Mixed types (pH, EC, RTD).
- Invalid response ignored.
- Missing `?i` token ignored.
- Timeout/slow response ignored.
- `tests/test_sensor_manager`: enables `CONFIG_SENSOR_MANAGER_FORCE_FAKE_ONLY` so Unity can validate the simulated dataset and tick logic without physical hardware. Run with `idf.py -T test_sensor_manager`.

---

## 4. YAML Core Tests (Layer 1)

- Scalars, sequences, nested mappings.
- Invalid YAML returns `NULL`.
- Memcheck to ensure `yaml_free_tree()` cleans everything.

---

## 5. UI Schema Tests (Layer 2)

- Template extraction with widgets.
- Missing required fields cause failure.
- Layout parsing, default layout when absent.

---

## 6. LVGL Builder Tests (Layer 3)

Mock LVGL object creation to count widgets, capture text/style.

- Label widget renders substituted text.
- Card template adds style.
- Layout positions computed correctly for N sensors / C columns.

---

## 7. End-to-End Tests

Tie mocks together: fake I²C → YAML → schema → builder. Validate that sensors map to correct templates and produce expected UI tree.

---

## 8. Stress + Regression

- 20+ sensors to ensure stability.
- Random garbage responses to confirm resilience.
- Add regression tests whenever bugs are fixed (parsing edge cases, style lookup, layout bugs, etc.).

---

## 9. Summary

A disciplined test matrix keeps the dynamic, data-driven UI reliable despite variable hardware. Comprehensive mocks and layered tests ensure every stage stays deterministic and safe.
