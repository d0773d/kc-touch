# YamUI Companion App — Phase 1 Core Features

This document captures the initial feature set for the YamUI Companion App, a drag-and-drop GUI builder that outputs YamUI-compliant YAML.

## 1. Overview

Phase 1 delivers a screen editor that supports:

- Drag-and-drop layout construction
- Widget placement and configuration
- Component creation and reuse
- Screen management
- Schema-aware YAML generation and validation
- Project import/export workflows

## 2. Drag-and-Drop Canvas

The canvas is the primary workspace for assembling screens.

### 2.1 Capabilities

- Add widgets via drag-and-drop
- Nest widgets inside layout containers
- Reorder widgets with drag handles
- Select widgets for editing
- Delete widgets
- Optional zoom and pan controls

### 2.2 Supported Widget Types

**Layout widgets**: `row`, `column`, `panel`, `spacer`, `list`

**UI widgets**: `label`, `button`, `img`, `textarea`, `switch`, `slider`

**Component instances**: Any user-defined component

## 3. Widget Palette

A left-side panel lists all available widgets.

### 3.1 Features

- Search/filter widgets
- Drag widgets onto the canvas
- Hover tooltips describing each widget

### 3.2 Widget Metadata

- Name
- Icon
- Description
- Allowed children (if any)

## 4. Property Inspector

A right-side panel for editing the selected widget.

### 4.1 Editable Properties

`id`, `text`, `src`, `style`, `props`, `events`, `bindings`, `accessibility`

### 4.2 Property Types

Text inputs, dropdowns, toggles, expression editors, JSON editors

## 5. Screen Manager

Panel for managing multiple screens.

### 5.1 Capabilities

- Add, rename, delete, duplicate screens
- Switch between screens

### 5.2 Screen Metadata

- Screen name
- Initial screen indicator

## 6. Component Manager

Author and reuse components.

### 6.1 Creation

- Define component name
- Define props
- Build layout on dedicated canvas

### 6.2 Usage

- Components appear in palette
- Drag onto screens like widgets

## 7. YAML Generator

Produces YamUI YAML from the visual project.

### 7.1 Requirements

- Follow YamUI schema
- Deterministic output
- Preserve component definitions
- Include screens, styles, and state references

### 7.2 Preview Panel

- Read-only YAML view
- Auto-updates on changes
- Syntax highlighting

## 8. YAML Validator

Ensures YAML validity.

### 8.1 Rules

- Schema compliance
- Widget type validation
- Component prop validation
- Event/action syntax validation
- Expression syntax validation

### 8.2 Error Display

- Inline error messages
- Highlight invalid widgets
- Provide fix suggestions

## 9. Project Import/Export

### 9.1 Import

- Load YamUI YAML
- Parse into internal JSON model
- Reconstruct screens and components

### 9.2 Export

- Export YamUI YAML
- Export project JSON

## 10. Internal Data Model

```
{
  "app": {},
  "state": {},
  "styles": {},
  "components": {},
  "screens": {}
}
```

Widget structure example:

```
{
  "type": "label",
  "id": "title",
  "text": "{{ t('dashboard.title') }}",
  "style": "title_style",
  "widgets": [],
  "props": {},
  "events": {},
  "accessibility": {}
}
```

## 11. Deliverables Summary

| Feature                 | Included |
| ----------------------- | -------- |
| Drag-and-drop canvas    | Yes      |
| Widget palette          | Yes      |
| Property inspector      | Yes      |
| Screen manager          | Yes      |
| Component manager       | Yes      |
| YAML generator          | Yes      |
| YAML validator          | Yes      |
| Project import/export   | Yes      |
| Style editor            | Phase 2  |
| Asset manager           | Phase 2  |
| i18n editor             | Phase 2  |
| Live preview            | Phase 3  |

## 12. Summary

Phase 1 lays the foundation for YamUI visual design: screen creation, component authoring, YAML generation/validation, and project management.
