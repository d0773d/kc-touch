# Yam UI Generator

The Yam UI Generator is a companion app for building LVGL layouts that comply with the YamUI schema. It includes a Python backend for schema-aware project services and a React-based frontend that provides a drag-and-drop editor.

## Repository Layout

```
tools/yam-ui-generator/
├── backend          # FastAPI service for YAML import/export/validation
├── frontend         # React + Vite application for the visual editor
├── docs             # Product specifications and design notes
└── README.md
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

See [docs/phase1-core-features.md](docs/phase1-core-features.md) for the detailed Phase 1 feature breakdown. Additional specifications can be added in the `docs` directory as the project evolves.
