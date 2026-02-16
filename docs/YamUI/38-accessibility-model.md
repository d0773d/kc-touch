# Y — YamUI Accessibility Model  
**Contrast, Touch Targets, Focus Management, Readability, Motion Reduction, and Assistive Hooks**

This document defines the **YamUI Accessibility Model**, the set of principles, rules, and runtime behaviors that ensure YamUI applications are accessible to a wide range of users — including those with visual, motor, or cognitive limitations.

Although embedded devices differ from desktop/mobile platforms, YamUI provides a practical, embedded-friendly accessibility layer that improves usability without requiring heavy frameworks.

The accessibility model covers:

- color contrast  
- font sizing & readability  
- touch target sizing  
- focus management  
- motion reduction  
- semantic widget roles  
- accessibility metadata  
- assistive hooks for firmware integration  

---

# 1. Overview

YamUI’s accessibility model is built on four pillars:

1. **Perceivable** — text is readable, colors are distinguishable  
2. **Operable** — touch targets are large enough, focus is predictable  
3. **Understandable** — labels, roles, and states are clear  
4. **Robust** — accessible metadata is available to assistive systems  

These principles are adapted from WCAG but optimized for embedded systems.

---

# 2. Color & Contrast Requirements

YamUI enforces minimum contrast ratios for text and UI elements.

### 2.1 Contrast Rules

- Text must have **≥ 4.5:1** contrast against background  
- Large text (≥ 18 px) must have **≥ 3:1** contrast  
- Icons must have **≥ 3:1** contrast  
- Disabled elements may be lower contrast but must remain distinguishable  

### 2.2 Automatic Contrast Checking

During build:

- styles are analyzed  
- contrast issues generate warnings  

Example warning:

```
[accessibility] title_style text_color (#AAAAAA) too low contrast on bg (#FFFFFF)
```

---

# 3. Font Size & Readability

### 3.1 Minimum Font Size

- Minimum recommended: **14 px**  
- Titles: **18–24 px**  
- Body text: **14–18 px**  

### 3.2 Dynamic Font Scaling (Optional)

YamUI supports a global scale factor:

```yaml
app:
  font_scale: 1.2
```

All font sizes scale proportionally.

---

# 4. Touch Target Requirements

Touch targets must be large enough for reliable interaction.

### 4.1 Minimum Touch Target Size

- Minimum: **44×44 px**  
- Recommended: **48×48 px**  

YamUI checks widget sizes during build and logs warnings.

### 4.2 Padding Enforcement

If a widget is too small, YamUI can automatically add padding:

```yaml
accessibility:
  auto_pad_touch_targets: true
```

---

# 5. Focus Management

For devices with:

- hardware buttons  
- D-pads  
- rotary encoders  
- keyboard input  

YamUI provides a predictable focus model.

### 5.1 Focus Rules

- focus order follows widget order  
- focusable widgets include: buttons, switches, sliders, textareas  
- non-interactive widgets are skipped  
- modals trap focus until closed  

### 5.2 Focus Overrides

```yaml
focus_order: [ssid_input, password_input, connect_button]
```

---

# 6. Motion Reduction

Some users are sensitive to animations.

### 6.1 Global Motion Toggle

```yaml
app:
  reduce_motion: true
```

### 6.2 Effects of Reduce Motion

- disables screen transition animations  
- disables modal fade-ins  
- disables spinner rotation (optional)  
- replaces animations with instant state changes  

---

# 7. Semantic Roles

Widgets can declare semantic roles to improve clarity.

### Examples

```yaml
role: "button"
role: "header"
role: "status"
role: "input"
role: "progress"
```

Roles help:

- assistive systems  
- automated testing  
- analytics  
- accessibility tooling  

---

# 8. Accessibility Metadata

Widgets may include metadata:

```yaml
accessibility:
  label: "WiFi password input"
  hint: "Enter your WiFi password"
  value_text: "{{wifi.password.length}} characters"
```

### Metadata Types

| Field | Description |
|--------|-------------|
| `label` | Human-readable name |
| `hint` | Additional context |
| `value_text` | Dynamic value description |
| `role` | Semantic role |

Metadata is exposed to:

- assistive firmware  
- automated testing tools  
- telemetry systems  

---

# 9. Error & Validation Feedback

YamUI supports accessible error messaging.

### Example

```yaml
- type: label
  text: "{{ui.error_message}}"
  style: error_style
  visible: "{{ui.error_message != ''}}"
  accessibility:
    role: "alert"
```

Alerts are:

- high contrast  
- persistent until resolved  
- announced via metadata  

---

# 10. Accessibility in Components

Components can define accessibility metadata:

```yaml
components:
  WifiCard:
    widgets:
      - type: label
        text: "{{ssid}}"
        accessibility:
          label: "Network name"
```

Props can be used in metadata:

```yaml
value_text: "{{rssi}} dBm"
```

---

# 11. Accessibility in Screens

Screens may define:

- focus order  
- default focus widget  
- motion preferences  

### Example

```yaml
screens:
  wifi_password:
    default_focus: password_input
```

---

# 12. Build-Time Accessibility Checks

The YamUI build pipeline performs:

- contrast checks  
- touch target size checks  
- missing label/hint checks  
- missing role checks  
- font size warnings  
- motion preference validation  

Example output:

```
[accessibility] button 'connect_button' is only 32x32 px (min 44x44)
```

---

# 13. Runtime Accessibility Hooks

Firmware can register callbacks:

```c
void yamui_set_accessibility_callback(yamui_accessibility_fn fn);
```

Callbacks receive:

- focus changes  
- alerts  
- value changes  
- metadata updates  

This enables:

- screen readers (future)  
- haptic feedback  
- audio cues  
- external accessibility devices  

---

# 14. Example: Fully Accessible Wi-Fi Password Screen

```yaml
screens:
  wifi_password:
    widgets:
      - type: label
        text: "{{ t('wifi.password') }}"
        role: "header"

      - type: textarea
        id: password_input
        password_mode: true
        on_change: set(wifi.password, {{value}})
        accessibility:
          label: "WiFi password"
          hint: "Enter the password for {{wifi.ssid}}"

      - type: button
        text: "{{ t('wifi.connect') }}"
        on_click: call(wifi_connect, {{wifi.ssid}}, {{wifi.password}})
        accessibility:
          label: "Connect to WiFi"
```

---

# 15. Summary

The YamUI Accessibility Model provides:

- color contrast enforcement  
- readable typography  
- touch target sizing  
- focus management  
- motion reduction  
- semantic roles  
- accessibility metadata  
- build-time checks  
- runtime assistive hooks  

This system ensures YamUI applications are **usable, inclusive, and accessible**, even on constrained embedded devices.
