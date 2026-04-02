# YamUI Device Support Matrix

This document defines the current device-side YamUI contract for the ESP32-P4
firmware runtime using LVGL 9.5.0.

The goal is not to expose all of LVGL directly. The goal is to define the
subset of LVGL-backed YamUI features the device runtime supports, then align the
Companion export format to that contract.

## Resolved LVGL Version

- Device runtime LVGL version: `9.5.0`
- Evidence:
  - `dependencies.lock`
  - `managed_components/lvgl__lvgl/lv_version.h`

## Status Legend

- `working`: validated on device and considered usable
- `partial`: supported with important limits or schema mismatches
- `missing`: not implemented in the current runtime

## Device Runtime Widget Matrix

| Widget | Status | Notes |
| --- | --- | --- |
| `label` | working | Text bindings work. |
| `button` | working | Real LVGL `lv_button` path restored and stable. |
| `img` | partial | Symbol images like `symbol:wifi` work. General asset/image pipeline still needs broader validation. |
| `camera_preview` | working | Uses the board camera service with a decimated live preview path suitable for device rendering. |
| `row` | working | Flex row layout supported. |
| `column` | working | Flex column layout supported. |
| `panel` | working | Container works; titles are not auto-rendered from `props.title`. |
| `spinner` | working | LVGL spinner widget available for loading states and async feedback. |
| `textarea` | working | Bound text and keyboard integration working on device. |
| `keyboard` | working | Targets textarea by widget `id`. |
| `switch` | working | Bound boolean-like state works. |
| `slider` | working | Bound numeric state works. |
| `bar` | working | Reactive to bound numeric state. |
| `arc` | partial | Runtime support exists; schema/device coverage still needs broader validation. |
| `dropdown` | working | Sequence `options` list is supported. |
| `roller` | working | Supports option lists, visible row count, and bound selection changes. |
| `led` | working | Useful lightweight status indicator; supports bound boolean or brightness-like values. |
| `table` | working | Supports YAML-defined rows and column widths for compact data displays. |
| `chart` | working | Supports static YAML-defined series data, axis range, and basic line/bar rendering. |
| `component` | partial | Runtime component system works with current YamUI prop convention, but does not yet match the Companion's richer component schema 1:1. |
| `list` | working | Uses a real LVGL list container on device and supports nested YamUI child widgets. |
| `spacer` | working | Simple space block for layout rhythm and separation. |
| `checkbox` | working | Checked state and `{{checked}}` event binding work like other boolean inputs. |

## Layout Support

### Working

- `layout.type: row`
- `layout.type: column`
- `gap`
- basic flex alignment
- flex grow
- content-sized containers

### Partial

- Some richer generated layout metadata from the Companion is not yet mapped to
  device behavior.
- Breakpoint metadata is currently Companion-side only.

## Style Support

### Device Runtime Style Format

The device runtime currently expects direct resolved style entries like:

```yaml
styles:
  primary_button:
    bg_color: "#4C7DFF"
    text_color: "#FFFFFF"
    radius: 18
    padding: 16
```

### Currently Working Style Fields

- `bg_color`
- `text_color`
- `radius`
- `padding`
- `spacing`
- `text_font`

### Partial / Not Yet Mapped from Companion Tokens

The Companion currently exports richer style token objects:

```yaml
styles:
  cta:
    name: Primary CTA
    category: surface
    value:
      backgroundColor: "#22d3ee"
      color: "#0f172a"
      borderRadius: 999.0
```

The current device runtime does not consume that token object format directly.
It needs either:

1. a normalization/export step, or
2. runtime support for the richer token structure

Not yet fully mapped:

- `backgroundColor` -> `bg_color`
- `color` -> `text_color`
- `borderRadius` -> `radius`
- `paddingHorizontal`
- `paddingVertical`
- `shadow`
- `textTransform`
- `letterSpacing`
- broader token metadata fields

## State and Binding Support

### Working

- scalar text bindings like `{{wifi.selected_ssid}}`
- state-driven booleans
- state-driven numeric values
- widget-to-state sync for:
  - textarea
  - switch
  - slider
  - dropdown
- state persistence across navigation
- hardware brightness sync from `display.brightness`

### Partial

- Nested object state from Companion output should be flattened or normalized for
  the current runtime contract.

Preferred current device style:

```yaml
state:
  device_status.active: "3"
  device_status.connected: "4"
```

instead of:

```yaml
state:
  device_status:
    active: 3
    connected: 4
```

## Event Support

### Device Runtime Event Keys

Current runtime event keys:

- `on_click`
- `on_press`
- `on_release`
- `on_change`
- `on_focus`
- `on_blur`

### Companion Mismatch

The Companion currently tends to emit an `events` object with entries like:

- `events.onPress`

That does not match the device runtime contract directly yet.

## Action Support

### Working

- `set(key, value)`
- `toggle(key)`
- `increment(key)`
- `increment(key, amount)`
- `decrement(key)`
- `decrement(key, amount)`
- `push(screen)`
- `pop()`
- `modal(component)`
- `close_modal()`
- `call(function, ...)`
- built-in helper natives:
  - `ui_async_reset(operation, message?)`
  - `ui_async_begin(operation, message?)`
  - `ui_async_progress(operation, progress)`
  - `ui_async_complete(operation, message?)`
  - `ui_async_fail(operation, message?)`

### Partial

- `emit(...)` exists as a runtime concept, but device-side workflows currently
  rely more on direct actions than event bus composition.
- full `await(call(...))` sequencing is not implemented yet; the device runtime
  currently models async flows through state updates and conditional UI.

## Async State Contract

YamUI now supports a simple state-driven async contract for native actions.

For an operation key like `sync_demo`, the runtime uses:

- `async.sync_demo.running`
- `async.sync_demo.progress`
- `async.sync_demo.status`
- `async.sync_demo.message`
- `async.sync_demo.error`

This pairs with existing `visible_if`, `enabled_if`, `label`, `bar`, and
`spinner` support to build loading and progress UX without forcing full screen
rerenders.

## Component System

### Working Device Contract

Current device-side components work with a simple prop list style:

```yaml
components:
  InfoCard:
    props: [title, subtitle]
    widgets:
      - type: label
        text: "{{title}}"
```

### Companion Mismatch

The Companion currently exports richer component definitions:

- `description`
- `props: {}`
- `prop_schema: [...]`

The runtime does not yet consume the full `prop_schema` model directly.

## Modal Support

### Working

- modal open/close flow
- modal component rendering
- theme-aware modal presentation

### Important Note

Modal stability depended on fixing LVGL ownership/concurrency first. The device
runtime should continue to treat the BSP-managed LVGL task as the owner and
avoid dual-task `lv_timer_handler()` access.

## Theme Support

### Working

- light/dark mode state toggle
- top-level `theme.defaults` widget-style mapping
- themed style lookup:
  - `light.<style>`
  - `dark.<style>`
  - fallback to unprefixed style
- screen re-render on theme change
- modal theme follows current mode

### Partial

- not all widgets are fully polished for theme parity yet
- theme defaults currently map widget type -> style name and do not yet cover a
  richer token-driven project theme model

## Companion Export Alignment Gaps

These are the main gaps between Companion output and the current device runtime:

1. style token object format
2. `events.onPress` style event mapping
3. richer component `prop_schema`
4. nested state object shape
5. widget vocabulary mismatch (`spacer`, `list`, future widgets)
6. translation-aware text export vs current direct text rendering
7. panel title metadata not automatically rendered

## Recommended Roadmap

### Phase 1: Stabilize the Device Contract

1. Keep extending the device runtime only for the Companion features we
   actually need.
2. Treat this matrix as the source of truth.
3. Validate each new feature on hardware.

### Phase 2: Add a Companion Firmware Export Target

Create an export path such as:

- `target: esp32_firmware`

that normalizes:

- styles
- events
- component props
- state shape

into the current device contract.

### Phase 3: Converge the Formats

As firmware support expands, reduce the normalization layer until Companion and
device formats are much closer or identical.

## Immediate Next Work

1. Add `spacer` widget support on device.
2. Decide whether `list` becomes a first-class device widget or is normalized to
   `column`.
3. Add event-name normalization rules between Companion and device:
   - `onPress` -> `on_click`
   - `onChange` -> `on_change`
4. Decide whether to:
   - support Companion style token objects directly, or
   - build a firmware-export normalization layer first
