# K — YamUI Modal System  
**Declarative Dialogs, Alerts, and Overlays for LVGL Applications**

This document defines the **YamUI Modal System**, the mechanism that allows YamUI applications to display dialogs, alerts, confirmations, and overlay components above the current screen.

Modals are essential for:

- Wi-Fi password prompts  
- error messages  
- confirmation dialogs  
- alerts  
- multi-step flows  
- temporary overlays  
- custom popups  

The modal system integrates with the Action System (Section J), Component System (Section A), and Navigation System (Section D).

---

# 1. Overview

A **modal** in YamUI is a component rendered above the current screen.  
Modals:

- block interaction with underlying UI  
- do not modify the navigation stack  
- can be nested  
- can contain any widgets or components  
- are dismissed via `close_modal()`  

Modals are declared as **components**, not screens.

---

# 2. Showing a Modal

A modal is shown using the `modal()` action:

```yaml
on_click: modal(AlertDialog)
```

This renders the component `AlertDialog` as a modal overlay.

---

# 3. Closing a Modal

A modal is closed using:

```yaml
on_click: close_modal()
```

If no modal is active, the action does nothing.

---

# 4. Modal Component Definition

Modals are defined like any other component:

```yaml
components:
  AlertDialog:
    layout:
      type: column
      gap: 12
    widgets:
      - type: label
        text: "{{message}}"

      - type: button
        text: "OK"
        on_click: close_modal()
```

### Requirements

- Must be a component (not a screen)  
- Should define a layout  
- Should include at least one dismissal action  

---

# 5. Modal Rendering Behavior

When a modal is shown:

1. YamUI creates a **full-screen overlay container**  
2. Applies a dimmed background (default)  
3. Centers the modal component  
4. Renders the component inside the overlay  
5. Blocks interaction with underlying widgets  

The underlying screen remains mounted and visible but inactive.

---

# 6. Modal Stacking

YamUI supports **stacked modals**.

Example:

```yaml
modal(FirstDialog)
```

Inside FirstDialog:

```yaml
on_click: modal(SecondDialog)
```

Stack behavior:

- `close_modal()` closes the topmost modal  
- underlying modals remain active  
- underlying screens remain inactive  

This enables:

- nested confirmations  
- multi-step dialogs  
- wizard-style overlays  

---

# 7. Modal Layout

Modals typically use:

- `column` layout  
- centered alignment  
- padding  
- card-style backgrounds  

Example:

```yaml
components:
  ConfirmDialog:
    layout:
      type: column
      gap: 10
      align: center
    widgets:
      - type: label
        text: "{{title}}"

      - type: row
        gap: 8
        widgets:
          - type: button
            text: "Cancel"
            on_click: close_modal()

          - type: button
            text: "OK"
            on_click: call(confirm_action)
```

---

# 8. Modal Styling

Modals can use any style:

```yaml
style: dialog_style
```

Common modal styles include:

- rounded corners  
- drop shadows  
- semi-transparent backgrounds  
- card-like surfaces  

Example style:

```yaml
styles:
  dialog_style:
    bg_color: "#2A2A2A"
    radius: 12
    pad_all: 16
```

---

# 9. Modal Background Overlay

YamUI automatically creates a dimmed background overlay:

- semi-transparent black  
- blocks input  
- closes only when `close_modal()` is called  

Future extension:  
`modal_dismiss_on_background: true`

---

# 10. Modal + State Integration

Modals can read and write state:

```yaml
text: "{{wifi.error_message}}"
```

```yaml
on_click: set(ui.modal_open, false)
```

Modals can also be shown conditionally:

```yaml
visible: "{{ui.show_error}}"
```

---

# 11. Modal + Navigation Integration

Modals do **not** affect the navigation stack.

This means:

- `push()` and `pop()` still work normally  
- screens remain mounted  
- modals overlay the current screen  

Example:

```yaml
on_click:
  - push(wifi_password)
  - modal(PasswordHintDialog)
```

---

# 12. Example: Wi-Fi Password Prompt Modal

```yaml
components:
  WifiPasswordDialog:
    layout:
      type: column
      gap: 10
    widgets:
      - type: label
        text: "Enter Password"

      - type: textarea
        id: password_input
        password_mode: true
        on_change: set(wifi.password, {{value}})

      - type: button
        text: "Connect"
        on_click:
          - call(wifi_connect, {{wifi.ssid}}, {{wifi.password}})
          - close_modal()
```

Usage:

```yaml
on_click: modal(WifiPasswordDialog)
```

---

# 13. Example: Error Dialog

```yaml
components:
  ErrorDialog:
    layout:
      type: column
      gap: 8
    widgets:
      - type: label
        text: "{{error_message}}"
        style: error_style

      - type: button
        text: "OK"
        on_click: close_modal()
```

Usage:

```yaml
on_click:
  - set(error_message, "Connection failed")
  - modal(ErrorDialog)
```

---

# 14. Summary

The YamUI Modal System provides:

- declarative modal dialogs  
- overlay rendering  
- stacked modals  
- state-driven modals  
- event-driven modals  
- full component support  
- LVGL-native behavior  
- clean separation from navigation  

This system enables YamUI to support **alerts, confirmations, prompts, and multi-step overlays** in a fully declarative, React-like manner.
