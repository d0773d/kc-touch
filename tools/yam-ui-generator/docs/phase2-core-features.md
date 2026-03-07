# YamUI Companion App — Phase 2 Core Features

Phase 2 expands the Companion App beyond basic drag-and-drop editing into a full YamUI design studio. It adds style management, asset workflows, translation tooling, and project-level configuration that integrate tightly with the Phase 1 canvas, components, and YAML pipelines.

## 1. Overview

New capability areas:

- **Style Editor**: visual creation and editing of YamUI styles
- **Asset Manager**: upload, preview, and organize images, icons, and fonts
- **i18n Editor**: manage translation keys and values
- **Project Settings Panel**: configure app-level metadata and defaults

## 2. Style Editor

### 2.1 Capabilities

- Create, edit, rename, and delete styles
- Update references automatically when renaming
- Real-time preview on the canvas

### 2.2 Editable Properties

- Colors (text, background, border)
- Padding and margin
- Radius, alignment, shadow
- Font selection
- Size constraints (width, height)

### 2.3 Style Categories

- Text styles (fonts, colors)
- Container styles (padding, radius, background)
- Interactive styles (hover, pressed — future)

### 2.4 Integration

- Styles selectable in the Property Inspector
- Canvas updates immediately on changes
- YAML generator outputs all entries under `styles:`

## 3. Asset Manager

### 3.1 Supported Asset Types

- Images: PNG, JPG
- Icons: SVG (with optional PNG conversion)
- Fonts: LVGL-compatible binaries

### 3.2 Features

- Upload, preview, rename, delete
- Organize into folders
- Auto-generate asset references for YAML

### 3.3 Validation

- File size caps
- Format enforcement
- Duplicate name detection

### 3.4 Integration

- Assets surfaced for `img` widgets and style backgrounds
- YAML bundle includes references and manifests

## 4. i18n Editor

### 4.1 Features

- Add/edit/delete translation keys
- Auto-generate keys from text selections
- Import/export translation files

### 4.2 Locales

- English by default
- User-defined additional locales

### 4.3 Key Management

- Detect missing or unused keys
- Suggest naming patterns

### 4.4 Integration

- Property Inspector text fields can insert `t('key')`
- YAML generator emits translation bundles

## 5. Project Settings Panel

### 5.1 Editable Settings

- App name
- Initial screen selection
- Default and supported locales
- Theme selection (future)
- Bundle output settings

### 5.2 Integration

- Writes to the `app:` section in YAML
- Influences preview defaults

## 6. Enhanced YAML Generator

- Includes styles, assets, i18n bundles, and app metadata
- Removes unused styles
- Generates asset manifest and validates references

## 7. Enhanced YAML Validator

- Flags missing/invalid styles and circular references
- Detects missing assets or bad references
- Validates translation keys and locale definitions

## 8. UI Enhancements

- Canvas overlays for styles, assets, and i18n keys
- Inspector pickers for styles, assets, and i18n keys
- Palette sections for assets/components/styles (read-only)

## 9. Deliverables Summary

| Feature                  | Included |
| ------------------------ | -------- |
| Style Editor             | Yes      |
| Asset Manager            | Yes      |
| i18n Editor              | Yes      |
| Project Settings Panel   | Yes      |
| Enhanced YAML Generator  | Yes      |
| Enhanced YAML Validator  | Yes      |
| Live Preview             | Phase 3  |
| AI-Assisted Layout       | Phase 3  |
| Template Library         | Phase 3  |

## 10. Summary

Phase 2 turns the Companion App into a complete UI authoring environment. Designers can now craft production-ready YamUI interfaces with visual styling, asset management, translation workflows, and project-wide configuration—all without leaving the GUI. These capabilities set the stage for Phase 3 additions like live preview, AI-guided layout, and template libraries.
