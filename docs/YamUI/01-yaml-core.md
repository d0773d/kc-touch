# YAML Core (Layer 1)
**Generic YAML Parser Component**

The `yaml_core` component provides a **schema-agnostic**, **libyaml-based** parser that converts any YAML document into a **generic node tree**. This tree is then consumed by higher-level components such as the UI Schema Interpreter (Layer 2).

This layer contains **no knowledge** of:
- sensors
- templates
- widgets
- LVGL
- your application logic

It is a reusable, standalone YAML parsing engine.

---

## 1. Purpose

The purpose of `yaml_core` is to:

- Parse YAML text into a structured, navigable tree
- Provide a minimal, predictable memory model
- Offer helper functions for accessing mappings, sequences, and scalars
- Serve as the foundation for any YAML-driven system

This layer ensures that all higher layers operate on a clean, consistent data structure.

---

## 2. Node Tree Structure

The parser produces a tree of `yml_node_t` nodes.

### Node Types

```c
typedef enum {
    YML_SCALAR,
    YML_SEQUENCE,
    YML_MAPPING
} yml_node_type_t;
```

### Node Structure

```c
typedef struct yml_node {
    yml_node_type_t type;

    char *scalar; // if SCALAR

    struct yml_node **items; // if SEQUENCE
    size_t item_count;

    struct {
        char *key;
        struct yml_node *value;
    } *pairs; // if MAPPING
    size_t pair_count;

} yml_node_t;
```

### Summary of Node Behavior

| YAML Construct | Node Type | Representation |
|----------------|-----------|----------------|
| "hello" | `YML_SCALAR` | `scalar = "hello"` |
| `- item1` | `YML_SEQUENCE` | `items[]` |
| `key: value` | `YML_MAPPING` | `pairs[]` |

---

## 3. Public API

### Parse YAML into a node tree

```c
yml_node_t *yaml_parse_tree(const char *buf, size_t len);
```

- Input: YAML buffer + length
- Output: root node of the parsed tree
- Returns `NULL` on parse error

### Free the entire tree

```c
void yaml_free_tree(yml_node_t *node);
```

- Recursively frees all nodes, strings, arrays

### Mapping lookup helper

```c
const yml_node_t *yml_map_get(const yml_node_t *map, const char *key);
```

- Returns the value node for a given key
- Returns `NULL` if not found or not a mapping

### Scalar helper

```c
const char *yml_scalar(const yml_node_t *node);
```

- Returns scalar string or `NULL`

---

## 4. Memory Ownership Rules

### The parser allocates:
- All `yml_node_t` nodes
- All scalar strings
- All key strings
- All arrays (`items`, `pairs`)

### The caller must:
- Call `yaml_free_tree(root)` when done

### The caller must NOT:
- Free individual nodes
- Modify internal pointers
- Reallocate arrays

This ensures predictable, safe memory behavior.

---

## 5. Error Handling

The parser returns `NULL` if:

- YAML is malformed
- libyaml reports an error
- Memory allocation fails

Errors are not fatal to the system; the caller decides how to handle them.

---

## 6. Example: YAML → Node Tree

### Input YAML

```yaml
sensor_templates:
  temperature:
    type: card
    style: temp_style
layout:
  columns: 2
  spacing: 12
```

### Resulting Node Tree (conceptual)

```
root (mapping)
├── "sensor_templates" → (mapping)
│     └── "temperature" → (mapping)
│            ├── "type" → scalar("card")
│            └── "style" → scalar("temp_style")
└── "layout" → (mapping)
       ├── "columns" → scalar("2")
       └── "spacing" → scalar("12")
```

Layer 2 will interpret this structure into UI templates.

---

## 7. Component Structure

```
yaml_core/
├── CMakeLists.txt
├── idf_component.yml
├── include/
│   └── yaml_core.h
└── src/
    ├── yaml_core.c
    └── yaml_tree.c
```

### `idf_component.yml`

```yaml
dependencies:
  libyaml: "*"
```

### `CMakeLists.txt`

```cmake
idf_component_register(
    SRCS "src/yaml_core.c" "src/yaml_tree.c"
    INCLUDE_DIRS "include"
    REQUIRES libyaml
)
```

---

## 8. Responsibilities of Layer 1

### Layer 1 DOES:
- Parse YAML text
- Build a generic tree
- Provide helpers for tree navigation

### Layer 1 DOES NOT:
- Interpret meaning of keys
- Validate schema
- Create LVGL objects
- Know anything about sensors

Those responsibilities belong to Layers 2 and 3.

---

## 9. Why This Layer Matters

This layer gives you:

- A clean separation between parsing and interpretation
- A reusable YAML engine for any future features
- A stable foundation for declarative UI
- A structure that AI agents can reliably ingest

It ensures that the rest of the system is not tied to YAML syntax.

---

## 10. Next Layer

The next document (`02-ui-schema.md`) defines how the generic YAML tree is interpreted into:

- templates
- widgets
- styles
- layout

This is where your declarative UI language lives.
