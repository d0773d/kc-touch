# YamUI Companion User Guide

## Start the app

1. Backend
   - `cd tools/yam-ui-generator/backend`
   - `poetry install --no-interaction`
   - `poetry run uvicorn yam_ui_generator.api:app --reload`
2. Frontend
   - `cd tools/yam-ui-generator/frontend`
   - `npm ci`
   - `npm run dev`

Open the frontend URL shown by Vite (typically `http://localhost:5173`).

## Core workflow

1. Load a sample or import YAML from the toolbar.
2. Build screens/components with drag and drop.
3. Edit widget properties in the inspector.
4. Manage styles, translations, and assets in their side panels.
5. Use Preview:
   - YAML tab for deterministic export output
   - Live tab for runtime-like rendering and findings
6. Validate and export from the toolbar.

## Snapshot workflow

1. Open `Restore Snapshot` from the toolbar.
2. Optionally label/note or pin snapshots.
3. Select `Restore` for a snapshot.
4. In confirmation modal, optionally open `Show diff preview`.
5. Confirm with `Restore now`.

## Import compatibility

Legacy project shapes are migrated on import with warnings. Review import `issues` after loading older files.
