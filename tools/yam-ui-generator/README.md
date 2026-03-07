# Yam UI Generator

The Yam UI Generator is a companion app for building LVGL layouts that comply with the YamUI schema. It includes a Python backend for schema-aware project services and a React-based frontend that provides a drag-and-drop editor.

## Repository Layout

```
tools/yam-ui-generator/
â”œâ”€â”€ backend          # FastAPI service for YAML import/export/validation
â”œâ”€â”€ frontend         # React + Vite application for the visual editor
â”œâ”€â”€ docs             # Product specifications and design notes
â””â”€â”€ README.md
```

## Getting Started

1. **Backend**
   - Requires Python 3.11+
   - Install dependencies with `pip install -e .` inside the `backend` directory.
   - Run the API with `uvicorn yam_ui_generator.api:app --reload`.

2. **Frontend**
   - Requires Node.js 18+
   - From `frontend`, run `npm install` followed by `npm run dev`.

The frontend expects the backend to be available at `http://localhost:8000`. You can change this via the `VITE_API_BASE_URL` environment variable.

## Documentation

See [docs/phase1-core-features.md](docs/phase1-core-features.md) for the detailed Phase 1 feature breakdown.

For execution planning to final release readiness, see [docs/finish-checklist.md](docs/finish-checklist.md).

For backend contract versioning and error envelope behavior, see [docs/api-contract.md](docs/api-contract.md).

Operational docs:
- [docs/user-guide.md](docs/user-guide.md)
- [docs/contributor-guide.md](docs/contributor-guide.md)
- [docs/troubleshooting.md](docs/troubleshooting.md)
- [docs/performance-baseline.md](docs/performance-baseline.md)
- [docs/release-checklist.md](docs/release-checklist.md)
- [docs/release-qa-evidence.md](docs/release-qa-evidence.md)
- [docs/release-notes-template.md](docs/release-notes-template.md)
- [docs/release-notes-v0.1.0.md](docs/release-notes-v0.1.0.md)

Additional specifications can be added in the `docs` directory as the project evolves.

## CI Command Parity

Use the same commands locally that run in CI:

1. Frontend
   - `cd tools/yam-ui-generator/frontend`
   - `npm ci`
   - `npm run lint`
   - `npm run typecheck`
   - `npm test`
   - `npm run build`

2. Backend
   - `cd tools/yam-ui-generator/backend`
   - `poetry install --no-interaction`
   - `poetry run ruff check .`
   - `poetry run pytest -q --basetemp .pytest_tmp`
