# N — YamUI Runtime Architecture  
**Internal Engine Design: Parser, Renderer, State Engine, Event Dispatcher, and LVGL Builder**

This document defines the **YamUI Runtime Architecture**, the internal system that powers the declarative YAML → LVGL pipeline.  
It describes how YamUI loads YAML, parses components and screens, builds LVGL objects, manages state, dispatches events, and updates the UI reactively.

This is the authoritative reference for how YamUI works internally.

---

# 1. Overview

The YamUI runtime consists of the following subsystems:

1. **YAML Loader**  
2. **Schema Parser**  
3. **Component Registry**  
4. **Screen Registry**  
5. **State Engine**  
6. **Expression Engine**  
7. **Event Dispatcher**  
8. **Action Executor**  
9. **LVGL Builder**  
10. **Navigation Manager**  
11. **Modal Manager**

These subsystems work together to turn declarative YAML into a fully interactive LVGL application.

---

# 2. High-Level Architecture Diagram

```
+---------------------------+
|        YAML File          |
+-------------+-------------+
              |
              v
+---------------------------+
|      YAML Loader          |
+-------------+-------------+
              |
              v
+---------------------------+
|      Schema Parser        |
+-------------+-------------+
              |
              v
+---------------------------+
| Component Registry        |
| Screen Registry           |
| Style Registry            |
| Theme Engine              |
+-------------+-------------+
              |
              v
+---------------------------+
|       Navigation Manager  |
+-------------+-------------+
              |
              v
+---------------------------+
|       LVGL Builder        |
+-------------+-------------+
              |
              v
+---------------------------+
|  State Engine <-> Events  |
|  Expression Engine        |
|  Action Executor          |
+---------------------------+
```

---

# 3. YAML Loader

The loader:

- reads the YAML file from flash or filesystem  
- validates syntax  
- produces an in-memory representation (`yml_node_t`)  
- passes the tree to the Schema Parser  

The loader does **not** interpret meaning — only structure.

---

# 4. Schema Parser

The parser:

- validates the structure against the YamUI schema  
- extracts top-level sections (`app`, `state`, `styles`, `components`, `screens`)  
- builds internal registries  
- resolves inheritance (styles, components)  
- resolves theme overrides  
- pre-parses expressions into ASTs  

The parser ensures the YAML is valid before runtime.

---

# 5. Component Registry

Components are stored in a hash map:

```c
typedef struct {
    char name[32];
    yml_node_t *definition;
} yamui_component_t;
```

The registry supports:

- lookup by name  
- prop validation  
- layout inheritance  
- widget tree expansion  

Components are rendered by the LVGL Builder.

---

# 6. Screen Registry

Screens are stored similarly:

```c
typedef struct {
    char name[32];
    yml_node_t *definition;
} yamui_screen_t;
```

The registry supports:

- lookup by name  
- lifecycle events (`on_load`)  
- navigation transitions  

Screens are rendered into the LVGL root container.

---

# 7. State Engine

The state engine maintains a global key/value store:

```c
typedef struct {
    char key[64];
    char value[64];
} yamui_state_entry_t;
```

Features:

- hierarchical keys (`wifi.status`)  
- string-based storage  
- change notifications  
- reactive widget updates  
- expression re-evaluation  

State updates trigger UI updates without re-rendering screens.

---

# 8. Expression Engine

Expressions are parsed into ASTs at load time.

At runtime:

1. Identify referenced state keys  
2. Evaluate AST  
3. Produce string/number/boolean  
4. Apply to widget property  

The engine supports:

- arithmetic  
- comparisons  
- boolean logic  
- ternary  
- null coalescing  
- prop/state resolution  

Expressions are cached for performance.

---

# 9. Event Dispatcher

Each widget with event handlers registers a callback:

```c
lv_obj_add_event_cb(obj, yamui_event_dispatcher, LV_EVENT_ALL, widget_ref);
```

The dispatcher:

1. Maps LVGL event → YamUI event name  
2. Looks up YAML handler  
3. Executes actions in order  

Events are the bridge between UI and logic.

---

# 10. Action Executor

The executor runs actions such as:

- `set()`  
- `goto()`  
- `push()`  
- `pop()`  
- `modal()`  
- `close_modal()`  
- `call()`  
- `emit()`  

Execution steps:

1. Evaluate arguments  
2. Execute action  
3. Trigger state updates or navigation  
4. Re-evaluate expressions  
5. Update affected widgets  

Actions are synchronous and ordered.

---

# 11. LVGL Builder

The LVGL Builder is responsible for turning YAML widget definitions into LVGL objects.

### Responsibilities

- create LVGL objects  
- apply styles  
- apply layout rules  
- bind events  
- bind expressions  
- handle component instancing  
- handle nested containers  

### Rendering Process

1. Create root container  
2. Apply layout  
3. Render children recursively  
4. Register widget bindings  
5. Return LVGL object  

The builder is stateless — it renders based on YAML + props.

---

# 12. Navigation Manager

The Navigation Manager maintains:

- current screen  
- navigation stack  
- screen transitions  

### Operations

| Action | Behavior |
|--------|----------|
| `goto(screen)` | Replace current screen |
| `push(screen)` | Push new screen |
| `pop()` | Pop screen |

Screens are re-rendered on navigation.

---

# 13. Modal Manager

The Modal Manager maintains a stack of modal overlays.

### Responsibilities

- create overlay container  
- center modal component  
- block background input  
- close modals in LIFO order  

Modals do not affect the navigation stack.

---

# 14. Runtime Loop

The YamUI runtime loop is event-driven:

1. LVGL processes input events  
2. YamUI dispatcher handles events  
3. Actions update state or navigation  
4. State engine triggers reactive updates  
5. LVGL redraws affected widgets  

There is no virtual DOM — updates are direct and efficient.

---

# 15. Performance Considerations

YamUI is optimized for embedded systems:

- expression AST caching  
- minimal string allocations  
- no full re-renders  
- direct LVGL updates  
- static registries  
- lightweight action execution  
- no dynamic memory in hot paths  

---

# 16. Summary

The YamUI Runtime Architecture provides:

- a robust YAML → LVGL pipeline  
- declarative UI rendering  
- reactive state updates  
- event-driven logic  
- component-based composition  
- efficient LVGL object creation  
- navigation and modal management  

This architecture enables YamUI to function as a **full declarative UI framework** for embedded LVGL applications.
