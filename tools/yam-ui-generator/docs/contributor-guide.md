# Contributor Guide

## Prerequisites

- Python 3.11+
- Node.js 18+
- Poetry

## Local development

1. Backend:
   - `cd tools/yam-ui-generator/backend`
   - `poetry install --no-interaction`
   - `poetry run uvicorn yam_ui_generator.api:app --reload`
2. Frontend:
   - `cd tools/yam-ui-generator/frontend`
   - `npm ci`
   - `npm run dev`

## Quality gates

- Frontend:
  - `npm run lint`
  - `npm test`
  - `npm run build`
- Backend:
  - `poetry run ruff check .`
  - `poetry run pytest -q --basetemp .pytest_tmp`

## Branch and commits

- Use small, incremental commits.
- Keep tests updated with behavior changes.
- Preserve import/export compatibility unless a migration is included.

## API contract

- Check `GET /contract` for version and migration support.
- Errors use a standard `error` envelope. See `docs/api-contract.md`.
