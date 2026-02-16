# S — YamUI Testing & Validation Framework  
**Unit Tests, Snapshot Tests, State Tests, Event Simulation, and Hardware-in-the-Loop Validation**

This document defines the **YamUI Testing & Validation Framework**, the system used to ensure YamUI applications behave correctly, consistently, and predictably across firmware builds, UI updates, and hardware environments.

YamUI supports multiple layers of testing:

- YAML schema validation  
- component unit tests  
- screen snapshot tests  
- state & expression tests  
- event/action simulation  
- navigation tests  
- modal tests  
- hardware-in-the-loop (HIL) tests  
- performance regression tests  

This framework ensures YamUI UIs remain stable and reliable in production.

---

# 1. Overview

YamUI testing is built around three principles:

1. **Determinism** — same YAML → same UI  
2. **Isolation** — components/screens can be tested independently  
3. **Simulation** — events, actions, and state updates can be simulated without hardware  

The framework is designed for embedded systems but is fully testable on desktop CI environments.

---

# 2. YAML Schema Validation (Build-Time)

Before any runtime tests, YamUI performs strict schema validation (Section Q).

Validation checks:

- required fields  
- valid widget types  
- valid actions  
- valid expressions  
- valid component/screen references  
- valid style/theme references  

If validation fails, the build stops.

This is the **first line of defense**.

---

# 3. Component Unit Tests

Components can be tested in isolation.

### 3.1 Test Goals

- validate props  
- validate layout  
- validate widget tree  
- validate expression bindings  
- validate event handlers  

### 3.2 Example Test

```c
yamui_test_component("WifiCard", {
    .props = { {"ssid", "MyWiFi"}, {"rssi", "-62"} },
    .expect_text = {
        {"label_1", "MyWiFi"},
        {"label_2", "-62 dBm"}
    }
});
```

### 3.3 What’s Verified

- component renders without errors  
- all widgets exist  
- expressions evaluate correctly  
- props propagate correctly  

---

# 4. Screen Snapshot Tests

Screens can be rendered off-device and compared to known snapshots.

### 4.1 Snapshot Types

- **widget tree snapshot** (structural)  
- **expression snapshot** (evaluated values)  
- **layout snapshot** (bounding boxes)  

### 4.2 Example

```c
yamui_snapshot_screen("wifi_setup");
```

The snapshot is compared to a stored reference.

### 4.3 Snapshot Failures

Triggered by:

- layout changes  
- widget additions/removals  
- style changes  
- expression changes  

Snapshots ensure UI stability across updates.

---

# 5. State & Expression Tests

Expressions are evaluated in isolation.

### 5.1 Example

```c
yamui_test_expression("wifi.status == 'connected'", {
    .state = { {"wifi.status", "connected"} },
    .expect_bool = true
});
```

### 5.2 What’s Verified

- expression syntax  
- identifier resolution  
- arithmetic correctness  
- boolean logic correctness  
- ternary/null-coalescing correctness  

This ensures the Expression Engine behaves predictably.

---

# 6. Event & Action Simulation

Events can be simulated without LVGL hardware.

### 6.1 Example

```c
yamui_simulate_event("wifi_setup", "scan_button", "on_click");
```

### 6.2 What’s Verified

- event handler exists  
- actions execute in order  
- state updates occur  
- navigation triggers  
- native calls are invoked (mocked)  

### 6.3 Action Assertions

```c
yamui_expect_state("ui.scanning", "true");
yamui_expect_call("wifi_start_scan");
yamui_expect_navigation("wifi_scanning");
```

This ensures event → action → state flow is correct.

---

# 7. Navigation Tests

Navigation can be simulated:

### 7.1 Example

```c
yamui_test_navigation({
    .start = "home",
    .actions = {
        { "button_setup", "on_click" }
    },
    .expect_screen = "wifi_setup"
});
```

### 7.2 What’s Verified

- `goto()`  
- `push()`  
- `pop()`  
- navigation stack behavior  

---

# 8. Modal Tests

Modals are tested via simulation.

### 8.1 Example

```c
yamui_simulate_event("wifi_setup", "error_button", "on_click");
yamui_expect_modal("ErrorDialog");
```

### 8.2 Modal Assertions

- modal is shown  
- modal stack depth  
- modal contents  
- modal dismissal  

---

# 9. Hardware-in-the-Loop (HIL) Tests

HIL tests validate YamUI on real hardware.

### 9.1 What’s Tested

- LVGL rendering  
- touch input  
- performance  
- memory usage  
- native function integration  
- Wi-Fi provisioning flows  
- sensor dashboards  

### 9.2 Example HIL Test

```c
hil_press("scan_button");
hil_expect_state("ui.scanning", "true");
hil_expect_screen("wifi_scanning");
```

HIL tests ensure real-world correctness.

---

# 10. Performance Regression Tests

Performance is measured across builds.

### 10.1 Metrics

- screen render time  
- modal show/hide time  
- expression evaluation time  
- state update time  
- LVGL object creation count  
- memory usage  

### 10.2 Example

```
[perf] render wifi_setup: 3.2 ms (baseline: 3.1 ms)
```

If performance regresses beyond a threshold, tests fail.

---

# 11. Continuous Integration (CI) Integration

YamUI tests run in CI:

- schema validation  
- component tests  
- snapshot tests  
- navigation tests  
- modal tests  
- performance tests  

CI ensures UI stability across commits.

---

# 12. Example CI Pipeline

```
[1] Validate YAML schema
[2] Merge & normalize YAML
[3] Run component unit tests
[4] Run screen snapshot tests
[5] Run expression tests
[6] Run event/action simulation tests
[7] Run navigation tests
[8] Run modal tests
[9] Run performance regression tests
```

---

# 13. Summary

The YamUI Testing & Validation Framework provides:

- schema validation  
- component unit tests  
- screen snapshot tests  
- expression tests  
- event/action simulation  
- navigation tests  
- modal tests  
- hardware-in-the-loop validation  
- performance regression tests  
- CI integration  

This framework ensures YamUI applications are **stable, correct, performant, and production-ready** across firmware updates and UI changes.
