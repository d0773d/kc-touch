# YamUI Companion App — Phase 1 & 2 Implementation Plan

This plan identifies the remaining Phase 1 requirements (MVP builder) and outlines the workload to deliver the new Phase 2 capabilities. It serves as the execution backlog for the Yam UI Generator project.

## Phase 1 Gap Analysis (Completed February 2026)

All Phase 1 requirements have been implemented and verified. The table below remains for historical reference.

| Requirement | Status | Notes |
| ----------- | ------ | ----- |
| Drag-and-drop canvas (reorder, nesting) | ✅ Implemented | Canvas uses `@dnd-kit` for intra/inter-parent drag, with handles and drop indicators. |
| Widget selection & editing | ✅ Implemented | Inspector now surfaces validation badges and helper pickers for styles/props. |
| Component creation | ✅ Implemented | Component manager supports prop schema editing that round-trips via YAML. |
| Screen manager | ✅ Implemented | Metadata editor with duplicate-name guard landed; layout responsive. |
| YAML import/export | ✅ Implemented | Modal workflow supports paste/upload, download + clipboard export, cached payload reuse. |
| Validator | ✅ Implemented | Inline issue badges in Canvas/Inspector; backend treats warnings as non-blocking. |
| Testing | ✅ Implemented | Backend pytest suite covers import/export/validate; frontend Vitest covers ProjectContext. |

## Phase 1 Execution Summary

All execution steps were completed between January–February 2026. Highlights:

1. **Canvas DnD overhaul** – `@dnd-kit` sortable tree, drag handles, cross-parent drops, and tree state persistence shipped.
2. **Widget editing improvements** – Inspector gained style/event helpers plus inline validation badges fed by backend issues.
3. **Component prop definitions** – Component manager edits `prop_schema`; backend schema + YAML import/export round-trip the data.
4. **Screen metadata editor** – Screens expose metadata/title editing with duplicate-name guard and responsive layout controls.
5. **Import/export UX** – Modal-based workflows with file upload, download, clipboard, and cached export reuse replaced prompts.
6. **Backend hardening** – Added `/projects/template`, yaml serialization fixes, and pytest coverage for import/export/validate.

## Phase 2 Goals

- Bring YamUI Companion from “screen builder” to “design studio” by adding style, asset, and localization workflows.
- Maintain the fast feedback loop established in Phase 1 (modal flows, inline validation) while layering richer data models.
- Keep backend + YAML output authoritative so generated bundles can be consumed by firmware/app teams without manual edits.

## Phase 2 Scope Breakdown

1. **Style Editor**
   - **Data model**: extend `ProjectModel.styles` to store structured tokens (`color`, `spacing`, `typography`, `shadows`). Introduce `StyleToken` type with metadata (category, usage count, fallback value).
   - **Backend**: add `/styles/preview` (receives token + sample widget, returns rendered snapshot placeholder) and `/styles/lint` (returns conflicts/unused tokens). Ensure import/export persists new structure.
   - **Frontend**: new Style workspace tab with list, filters, and detail drawer. Support create/clone/delete tokens, live usage references (list of widgets/components using the style), and guardrails (warn before deleting in-use tokens).
   - **Property Inspector hooks**: dropdowns for `style`, `textStyle`, `surfaceStyle`, fed from the style store.

2. **Asset Manager**
   - **Data model**: introduce `assets` map with entries `{ id, name, type, mime, size, path, tags }` and optional generated thumbnails.
   - **Backend**: upload endpoint (stream to `tools/yam-ui-generator/.assets`), metadata endpoint, and manifest generator that adds `assets` section to YAML exports. Provide cleanup command and validation to cap file size/types.
   - **Frontend**: panel with drag/drop uploader, grid/list views, tag filters, quick actions (copy URL, insert into widget prop). Integrate with inspector so `img.src` and `panel.background` can browse assets.

3. **i18n Editor**
   - **Data model**: add `translations` keyed by locale → flat key/value map plus metadata (last modified, missing count). Support namespace grouping for large apps.
   - **Backend**: endpoints for JSON import/export, CSV export, and simple validation (detect missing keys per locale, invalid placeholders). Extend `/projects/export` to embed translations when requested.
   - **Frontend**: spreadsheet-style editor with locale columns, filters for missing/unused keys, shortcut to convert literal text on canvas into translation keys, and link back to widgets referencing a key.

4. **Project Settings & Metadata**
   - **UI**: expandable drawer editing `project.app` (name, version, defaultLocale, author, release channel) plus high-level toggles (enable assets/i18n, default screen size).
   - **Backend**: enforce schema, expose `GET/PUT /project/settings`, and let YAML generator emit metadata block.
   - **Canvas integration**: show default device frame preview based on chosen screen size, and highlight initial screen selection.

5. **YAML Generator & Validator Enhancements**
   - **Generator**: add sections for styles, assets manifest, translations, and project settings; automatically prune unused tokens/assets if user opts in.
   - **Validator**: new rules for cross-references (style exists, asset path valid, translation key defined) and richer severities (error/warning/info). Surface new issue types inline (e.g., asset missing thumbnail) via context store.

6. **Collaboration & Quality-of-life (stretch)**
   - Autosave snapshots locally, with ability to restore previous versions.
   - Basic multi-screen layout presets (tablet, desktop) to test responsive states.

## Phase 2 Deliverables & Acceptance Criteria

| Epic | Key Deliverables | Acceptance Criteria |
| ---- | ---------------- | ------------------- |
| Style Editor | Style workspace, inspector integration, backend preview/lint endpoints | Create/edit/delete styles, prevent deleting in-use tokens, preview endpoints return 200, styles appear in YAML. |
| Asset Manager | Upload API, storage, UI browser, inspector picker | Upload ≤10 MB file, reference asset from widget, export includes manifest, validation warns on missing asset. |
| i18n Editor | Translation store, import/export UI, widget binding helpers | Add locales/keys, detect missing translations, export JSON/CSV, canvas text easily converts to key reference. |
| Project Settings | Settings drawer, schema, YAML metadata | Editing settings updates YAML, validator enforces required fields, initial screen selection reflected in UI. |
| Generator/Validator | Extended schema + issue surfacing | Export includes new sections, validator catches cross-resource issues, frontend shows new issue badges. |

## Phase 2 Milestones & Sequencing

1. **Milestone A – Style Foundation (Weeks 1-3)**
   - Implement style data model + backend endpoints.
   - Build Style Editor UI and inspector dropdown integration.
   - Add targeted Vitest/pytest coverage.

2. **Milestone B – Asset Pipeline (Weeks 3-5)**
   - Add asset storage/upload endpoints and manifest generation.
   - Ship Asset Manager UI + inspector pickers.
   - Update YAML generator/validator for assets.

3. **Milestone C – Localization Suite (Weeks 5-7)**
   - Implement translations store + backend import/export.
   - Build i18n Editor grid and widget text → key flow.

4. **Milestone D – Project Settings & Global Enhancements (Weeks 7-8)**
   - Add settings drawer, metadata schema, canvas preview controls.

5. **Milestone E – Generator/Validator Parity (Week 8+)**
   - Finalize YAML/validation upgrades, polish QA, broaden automated tests.

Each milestone should land behind feature flags where reasonable so we can ship incrementally without breaking Phase 1 workflows.

## Sequencing

1. **Stabilize Phase 1 baseline** – keep main branch release-ready; guard new features behind flags.
2. **Style Editor first** – prerequisite for assets/i18n because inspector needs style hooks for advanced bindings.
3. **Asset Manager second** – once styles exist, unlock richer visuals via managed assets.
4. **i18n Editor third** – localization depends on both style/asset data referencing.
5. **Project Settings** – after core editors exist, global metadata can drive previews/export defaults.
6. **Generator/Validator finalization** – once all new data surfaces, update YAML + validation and expand automated tests.

## Tooling & Testing

- **Frontend**: add Vitest + React Testing Library for store logic and component managers.
- **Backend**: expand pytest suite to cover import/export/validate, style merging, and asset manifest creation.
- **CI (future)**: GitHub Actions workflow to run `poetry run pytest` and `npm run test` in `frontend` once added.

---
This plan will be updated as subtasks land. Each major bullet should convert into dedicated PRs/commits to keep reviews manageable.
