# R — YamUI Performance Model  
**Memory Usage, Rendering Cost, Expression Evaluation, and Embedded Optimization Strategies**

This document defines the **YamUI Performance Model**, the set of principles, constraints, and optimizations that allow YamUI to run efficiently on embedded hardware such as the ESP32-P4.

YamUI is designed to be:

- **fast** (minimal CPU overhead)  
- **lightweight** (small RAM footprint)  
- **predictable** (deterministic behavior)  
- **reactive** (updates only what changes)  
- **embedded-friendly** (no dynamic allocations in hot paths)  

This section explains how YamUI achieves these goals and how developers can design UIs that remain smooth and responsive.

---

# 1. Overview

YamUI’s performance model is built on four pillars:

1. **Static YAML → LVGL compilation**  
2. **Reactive state updates**  
3. **Expression caching**  
4. **Minimal LVGL object churn**

The result is a UI engine that behaves like React, but with the efficiency of hand-written LVGL code.

---

# 2. Memory Model

YamUI uses a predictable, low-overhead memory model.

## 2.1 Static Memory Allocation

Where possible:

- style objects  
- component definitions  
- screen definitions  
- expression ASTs  
- registries  

are allocated **once** at startup.

## 2.2 Dynamic Memory (Controlled)

Dynamic allocation is used only for:

- LVGL object creation  
- modal overlays  
- navigation stack entries  
- string buffers for evaluated expressions  

All allocations are bounded and predictable.

## 2.3 Memory Footprint Targets

Typical YamUI memory usage:

| Subsystem | Typical RAM |
|-----------|-------------|
| State store | 1–4 KB |
| Expression ASTs | 2–8 KB |
| Screen widget trees | 10–40 KB |
| Styles | 1–2 KB |
| Navigation stack | <1 KB |
| Modal stack | <1 KB |

Actual usage depends on screen complexity and asset sizes.

---

# 3. Rendering Cost Model

YamUI minimizes rendering cost by:

- rendering screens **only on navigation**  
- updating widgets **only when state changes**  
- avoiding full re-renders  
- using LVGL’s efficient object model  

## 3.1 Full Screen Render

A full screen render occurs when:

- navigating to a new screen  
- pushing/popping screens  
- showing a modal  
- closing a modal  

Cost depends on widget count.

### Typical render time (ESP32-P4):

| Widget Count | Render Time |
|--------------|-------------|
| 20 widgets | ~1–2 ms |
| 50 widgets | ~3–5 ms |
| 100 widgets | ~6–10 ms |

## 3.2 Incremental Updates

When state changes:

- only widgets referencing that state key update  
- only the affected LVGL properties are changed  
- no layout recalculation unless needed  

This keeps updates extremely fast.

---

# 4. Expression Evaluation Cost

Expressions are parsed into ASTs at build time.

At runtime:

- AST evaluation is O(n) where n = number of nodes  
- typical expressions have <10 nodes  
- evaluation cost is microseconds  

### Example

```
{{wifi.status == 'connected' ? 'Online' : 'Offline'}}
```

AST nodes: ~7  
Evaluation time: <5 µs

### Expression Caching

YamUI caches:

- parsed AST  
- referenced state keys  
- last evaluated value (optional future optimization)  

This ensures expressions are cheap to evaluate.

---

# 5. State Update Cost

State updates are O(k) where k = number of widgets bound to that key.

Typical k values:

- 1–3 for most keys  
- 10–20 for global UI flags (e.g., theme)  

### Update Steps

1. Update state store  
2. Identify affected widgets  
3. Re-evaluate expressions  
4. Apply LVGL updates  

Total cost: microseconds to low milliseconds.

---

# 6. LVGL Object Churn

YamUI avoids unnecessary LVGL object creation.

### Objects are created only when:

- rendering a new screen  
- instantiating a component  
- showing a modal  

### Objects are **not** recreated when:

- state changes  
- expressions update  
- styles change  
- visibility toggles  

This dramatically reduces CPU and memory churn.

---

# 7. Navigation Performance

Navigation operations:

- `goto()`  
- `push()`  
- `pop()`  

trigger full screen renders.

### Typical navigation cost:

- 2–10 ms depending on widget count  
- modal operations are even cheaper (1–3 ms)  

Navigation is fast enough for:

- multi-step wizards  
- onboarding flows  
- dynamic dashboards  

---

# 8. Modal Performance

Modals are lightweight:

- overlay container + component  
- no screen teardown  
- no navigation stack changes  

Typical modal show/hide cost:

- 1–3 ms  

---

# 9. Asset Performance

Assets (images, fonts) are:

- loaded from flash  
- cached by LVGL  
- not duplicated in RAM  

### Performance Tips

- prefer indexed PNGs over full-color PNGs  
- use LVGL font converters for optimal size  
- avoid large images on low-RAM devices  

---

# 10. Performance Best Practices

## 10.1 Keep Screens Under ~100 Widgets

More is fine, but:

- layout cost increases  
- render time increases  
- memory usage increases  

## 10.2 Use Components to Reduce Duplication

Components:

- reduce YAML size  
- reduce parsing cost  
- improve maintainability  

## 10.3 Avoid Deeply Nested Layouts

Prefer:

```
column → row → widgets
```

Avoid:

```
column → row → column → row → column → ...
```

## 10.4 Keep Expressions Simple

Prefer:

```
{{wifi.status == 'connected'}}
```

Avoid:

```
{{wifi.status == 'connected' && (sensor.pH > 7 || (ui.mode == 'debug' && wifi.rssi < -70))}}
```

## 10.5 Use State Keys Strategically

Avoid:

- one giant state object  
- deeply nested keys  

Prefer:

```
wifi.status
wifi.ssid
ui.loading
sensor.pH
```

---

# 11. Performance Debugging Tools

YamUI provides optional performance diagnostics:

- expression evaluation logs  
- widget update logs  
- navigation timing logs  
- modal timing logs  
- LVGL object creation logs  

Example:

```
[perf] render screen 'wifi_setup' in 3.2 ms
[perf] update widget 'ssid_label' in 0.04 ms
```

---

# 12. Summary

The YamUI Performance Model provides:

- efficient static rendering  
- reactive incremental updates  
- cached expression evaluation  
- minimal LVGL object churn  
- predictable memory usage  
- fast navigation and modal operations  
- embedded-friendly performance guarantees  

This model ensures YamUI applications remain **smooth, responsive, and efficient**, even on constrained embedded hardware.
