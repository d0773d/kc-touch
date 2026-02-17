# Yam UI Generator Backend

FastAPI service that powers the Yam UI Companion App. The service is responsible for:

- Importing YamUI YAML into the canonical JSON project model
- Exporting JSON projects back to YamUI YAML
- Validating projects against a JSON Schema derived from the YamUI spec
- Serving widget metadata and schema snapshots to the frontend

## Setup

```bash
cd tools/yam-ui-generator/backend
python -m venv .venv
. .venv/Scripts/activate  # Windows
pip install -e .
uvicorn yam_ui_generator.api:app --reload
```

The service listens on `http://0.0.0.0:8000` by default.

## Endpoints

- `GET /health` — simple readiness probe
- `GET /widgets/palette` — list of supported widgets and their capabilities
- `GET /schema` — JSON schema for YamUI projects
- `POST /projects/import` — accepts YamUI YAML and returns the JSON model
- `POST /projects/export` — accepts the JSON project and returns YAML
- `POST /projects/validate` — validates YAML or JSON payloads and reports structured errors

Refer to the OpenAPI spec at `/docs` once the server is running.
