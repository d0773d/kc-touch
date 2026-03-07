# Release Notes

## Release

- Version: `v0.1.0`
- Date: `2026-03-06`
- Commit/Tag: `3582dba`

## Highlights

- Delivered the Yam UI Generator end-to-end stack (FastAPI backend + React editor frontend).
- Added schema-aware import/export/validate workflows with migration support for legacy project shapes.
- Completed release readiness gates: CI, lint, tests, type-checks, and release QA documentation.

## Added

- Backend API for project template, export/import, validation, preview render, styles, assets, and translations.
- Frontend editor features including canvas editing, property inspector, style manager, translation manager, asset library, snapshot restore flow, and issue accelerators.
- CI workflow for frontend/backend lint, tests, build, and frontend type-check.
- Release operations docs: checklist, QA evidence template, contributor/user/troubleshooting guides.

## Changed

- Standardized backend error responses to a single envelope shape.
- Added explicit backend contract endpoint (`GET /contract`) and documented contract behavior.
- Hardened validation parity across assets, bindings, events, styles, and translations.

## Fixed

- Frontend lint and TypeScript issues that previously blocked strict CI gates.
- Backend lint baseline issues and test expectations for new error envelope behavior.
- Import handling for older YAML project formats via migration warnings instead of hard failures.

## Migration Notes

- Contract version changes:
  - Contract endpoint added for explicit compatibility tracking.
- Import migration impact:
  - Legacy screen-list and translation-bucket/value formats are auto-migrated with warnings.
- Backward compatibility notes:
  - Import/export compatibility is preserved for supported project shapes.

## Validation and CI

- Frontend lint/test/build:
  - `npm run lint`, `npm run typecheck`, `npm test`, `npm run build` all passing.
- Backend lint/tests:
  - `poetry run ruff check .` and `poetry run pytest -q --basetemp .pytest_tmp` passing.
- CI workflow status:
  - Workflow present and configured for `main`, `yam`, and `codex/**`.

## Known Issues

- Local backend test temp directory (`tools/yam-ui-generator/backend/.pytest_tmp/`) may appear untracked after local test runs; it is not part of release artifacts.
