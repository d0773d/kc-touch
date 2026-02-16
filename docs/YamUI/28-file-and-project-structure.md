# O — YamUI File & Project Structure  
**Recommended Directory Layout for YamUI-Powered Firmware Projects**

This document defines the **recommended file and directory structure** for projects using YamUI.  
A consistent structure ensures:

- predictable organization  
- clean separation of UI concerns  
- easy navigation for developers and AI agents  
- reproducible builds  
- scalable multi-screen applications  

This structure is designed for embedded firmware projects (e.g., ESP32-P4) but applies to any YamUI-based system.

---

# 1. High-Level Directory Layout

A typical YamUI project uses the following structure:

```
/project
  /src
    /ui
      app.yaml
      /screens
      /components
      /styles
      /themes
      /layouts
      /assets
    /yamui_runtime
    main.c
  /build
  /tools
  README.md
```

Each directory has a clear purpose.

---

# 2. `/src/ui` — Root UI Directory

This is the **home of all YamUI declarative files**.

```
/src/ui
  app.yaml
  /screens
  /components
  /styles
  /themes
  /assets
```

### Contents

| File/Folder | Purpose |
|-------------|---------|
| `app.yaml` | Main entry point for the UI |
| `screens/` | One YAML file per screen |
| `components/` | Reusable components |
| `styles/` | Style definitions |
| `themes/` | Theme overrides |
| `assets/` | Images, fonts, icons |
| `layouts/` | Optional shared layout presets |

---

# 3. `app.yaml` — UI Entry Point

This file defines:

- initial screen  
- global state  
- global styles  
- theme selection  
- imports for screens/components  

Example:

```yaml
app:
  initial_screen: home
  theme: dark

import:
  - screens/home.yaml
  - screens/wifi_setup.yaml
  - components/WifiCard.yaml
  - styles/base.yaml
  - themes/dark.yaml
```

`app.yaml` is the root of the UI tree.

---

# 4. `/screens` — Screen Definitions

Each screen gets its own file:

```
/src/ui/screens
  home.yaml
  wifi_setup.yaml
  wifi_scanning.yaml
  wifi_list.yaml
  wifi_password.yaml
```

### Benefits

- screens remain small and focused  
- easy to navigate  
- easy to diff and version control  
- ideal for multi-screen apps  

### Example file: `wifi_setup.yaml`

```yaml
screens:
  wifi_setup:
    layout:
      type: column
      gap: 12
    widgets:
      - type: label
        text: "WiFi Setup"
      - type: button
        text: "Scan"
        on_click: goto(wifi_scanning)
```

---

# 5. `/components` — Reusable Components

Reusable UI blocks live here:

```
/src/ui/components
  WifiCard.yaml
  WifiListItem.yaml
  SensorCard.yaml
  ButtonPrimary.yaml
```

### Example: `WifiCard.yaml`

```yaml
components:
  WifiCard:
    props: [ssid, rssi]
    layout:
      type: row
      gap: 8
    widgets:
      - type: label
        text: "{{ssid}}"
      - type: label
        text: "{{rssi}} dBm"
```

Components keep screens clean and DRY.

---

# 6. `/styles` — Style Definitions

Styles are grouped into logical files:

```
/src/ui/styles
  base.yaml
  typography.yaml
  cards.yaml
  buttons.yaml
```

### Example: `typography.yaml`

```yaml
styles:
  title_style:
    text_color: "#FFFFFF"
    text_font: "montserrat_22"
```

---

# 7. `/themes` — Theme Overrides

Themes override styles globally:

```
/src/ui/themes
  dark.yaml
  light.yaml
```

### Example: `dark.yaml`

```yaml
themes:
  dark:
    overrides:
      card_style.bg_color: "#1A1A1A"
      title_style.text_color: "#FFFFFF"
```

---

# 8. `/layouts` — Shared Layout Presets (Optional)

Useful for repeated layout patterns:

```
/src/ui/layouts
  card_layout.yaml
  list_layout.yaml
```

### Example

```yaml
layout_presets:
  card:
    type: column
    gap: 8
    padding: 12
```

Components can import these presets.

---

# 9. `/assets` — Images, Icons, Fonts

All UI assets live here:

```
/src/ui/assets
  /images
  /icons
  /fonts
```

### Example

```
/src/ui/assets/images/wifi.png
/src/ui/assets/fonts/montserrat_22.bin
```

Assets are referenced in YAML:

```yaml
- type: img
  src: "wifi.png"
```

---

# 10. `/yamui_runtime` — C Runtime Layer

Contains the YamUI engine:

```
/src/yamui_runtime
  yamui_parser.c
  yamui_state.c
  yamui_events.c
  yamui_actions.c
  yamui_lvgl_builder.c
  yamui_navigation.c
  yamui_modals.c
  yamui_native.c
```

This layer:

- loads YAML  
- builds LVGL objects  
- manages state  
- dispatches events  
- executes actions  
- integrates with native firmware  

---

# 11. Build System Integration

The build system should:

1. **Embed YAML files** into firmware (e.g., via `idf.py` or CMake)  
2. **Embed assets** (images, fonts)  
3. **Compile YamUI runtime**  
4. **Expose native functions** via registration  

Example CMake snippet:

```cmake
file(GLOB UI_FILES "src/ui/**/*.yaml")
idf_component_register(
  SRCS ${UI_FILES} src/yamui_runtime/*.c
  INCLUDE_DIRS src/yamui_runtime
)
```

---

# 12. Example Full Project Structure

```
/project
  /src
    main.c
    /yamui_runtime
      yamui_parser.c
      yamui_state.c
      yamui_events.c
      yamui_actions.c
      yamui_lvgl_builder.c
      yamui_navigation.c
      yamui_modals.c
      yamui_native.c
    /ui
      app.yaml
      /screens
        home.yaml
        wifi_setup.yaml
        wifi_scanning.yaml
        wifi_list.yaml
        wifi_password.yaml
      /components
        WifiCard.yaml
        WifiListItem.yaml
        SensorCard.yaml
      /styles
        base.yaml
        typography.yaml
        cards.yaml
      /themes
        dark.yaml
        light.yaml
      /assets
        /images
        /icons
        /fonts
  /build
  README.md
```

---

# 13. Summary

The YamUI File & Project Structure provides:

- a clean, scalable directory layout  
- separation of screens, components, styles, themes, and assets  
- a predictable structure for AI agents and developers  
- easy integration with embedded build systems  
- a foundation for large, multi-screen LVGL applications  

This structure ensures YamUI projects remain maintainable, modular, and production-ready.
