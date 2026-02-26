import json

from fastapi.testclient import TestClient

from yam_ui_generator.api import app
from yam_ui_generator.template_project import get_template_project
from yam_ui_generator.yaml_io import project_to_yaml


def _client() -> TestClient:
    return TestClient(app)


def test_template_endpoint_returns_default_project() -> None:
    client = _client()
    response = client.get("/projects/template")
    assert response.status_code == 200
    body = response.json()
    assert "screens" in body
    assert "main" in body["screens"]
    assert body["screens"]["main"]["initial"] is True
    assert body["components"]["stat_card"]["prop_schema"][0]["name"] == "label"
    assert "styles" in body
    assert body["styles"]["card"]["category"] == "surface"
    assert body["styles"]["stat-value"]["value"]["fontSize"] == 32
    assert "translations" in body
    assert body["translations"]["en"]["entries"]["app.device_overview"] == "Device Overview"
    assert body["translations"]["es"]["entries"]["actions.trigger_sync"] == "Iniciar sincronización"


def test_export_endpoint_returns_yaml_and_no_issues() -> None:
    client = _client()
    project = get_template_project()
    response = client.post("/projects/export", json={"project": project.model_dump(mode="json")})
    assert response.status_code == 200
    body = response.json()
    assert body["issues"] == []
    assert "screens:" in body["yaml"]


def test_import_endpoint_round_trips_yaml() -> None:
    client = _client()
    project = get_template_project()
    yaml_text = project_to_yaml(project)
    response = client.post("/projects/import", json={"yaml": yaml_text})
    assert response.status_code == 200
    body = response.json()
    assert body["issues"] == []
    assert body["project"]["screens"]["main"]["widgets"]
    assert body["project"]["translations"]["en"]["entries"]["actions.trigger_sync"] == "Trigger Sync"


def test_validate_endpoint_requires_payload_and_accepts_project() -> None:
    client = _client()
    project = get_template_project()
    response = client.post("/projects/validate", json={"project": project.model_dump(mode="json")})
    assert response.status_code == 200
    body = response.json()
    assert body["valid"] is True
    assert body["issues"] == []

    missing = client.post("/projects/validate", json={})
    assert missing.status_code == 400
    assert missing.json()["detail"] == "Either project or yaml must be provided"


def test_style_preview_endpoint_returns_stub_preview() -> None:
    client = _client()
    token = {
        "name": "Test Surface",
        "category": "surface",
        "value": {
            "backgroundColor": "#ffffff",
            "color": "#000000",
        },
    }
    response = client.post("/styles/preview", json={"token": token})
    assert response.status_code == 200
    preview = response.json()["preview"]
    assert preview["category"] == "surface"
    assert preview["backgroundColor"] == "#ffffff"


def test_style_lint_endpoint_detects_missing_values() -> None:
    client = _client()
    tokens = {
        "heading": {
            "name": "Heading",
            "category": "text",
            "value": {"fontWeight": 600},
        },
        "surface": {
            "name": "Surface",
            "category": "surface",
            "value": {},
        },
    }
    response = client.post("/styles/lint", json={"tokens": tokens})
    assert response.status_code == 200
    issues = response.json()["issues"]
    assert any("fontSize" in issue["message"] for issue in issues)
    assert any("backgroundColor" in issue["message"] for issue in issues)


def test_project_settings_get_endpoint_returns_template_app_settings() -> None:
    client = _client()
    response = client.get("/project/settings")
    assert response.status_code == 200
    body = response.json()
    assert body["settings"]["name"] == "YamUI Sample Application"
    assert body["settings"]["initial_screen"] == "main"
    assert body["settings"]["locale"] == "en"


def test_project_settings_put_endpoint_normalizes_project_state() -> None:
    client = _client()
    project = get_template_project().model_copy(deep=True)
    project.screens["secondary"] = project.screens["main"].model_copy(deep=True)
    project.screens["secondary"].name = "secondary"
    project.screens["secondary"].initial = False

    response = client.put(
        "/project/settings",
        json={
            "project": project.model_dump(mode="json"),
            "settings": {
                "name": "Hydro Console",
                "initial_screen": "secondary",
                "locale": "fr",
                "supported_locales": ["en", "fr"],
            },
        },
    )
    assert response.status_code == 200
    body = response.json()
    assert body["project"]["app"]["name"] == "Hydro Console"
    assert body["project"]["app"]["initial_screen"] == "secondary"
    assert body["project"]["app"]["locale"] == "fr"
    assert "fr" in body["project"]["translations"]
    assert body["project"]["screens"]["secondary"]["initial"] is True
    assert body["project"]["screens"]["main"]["initial"] is False


def test_translation_export_endpoints_return_content() -> None:
    client = _client()
    project = get_template_project()
    json_response = client.post(
        "/translations/export",
        json={"project": project.model_dump(mode="json"), "format": "json"},
    )
    assert json_response.status_code == 200
    json_body = json_response.json()
    assert json_body["filename"].endswith(".json")
    assert json_body["mime_type"] == "application/json"
    assert "app.device_overview" in json_body["content"]

    csv_response = client.post(
        "/translations/export",
        json={"project": project.model_dump(mode="json"), "format": "csv"},
    )
    assert csv_response.status_code == 200
    csv_body = csv_response.json()
    assert csv_body["filename"].endswith(".csv")
    assert csv_body["mime_type"] == "text/csv"
    assert "app.device_overview" in csv_body["content"]


def test_translation_import_json_merges_locales() -> None:
    client = _client()
    project = get_template_project()
    payload = {
        "translations": {
            "fr": {
                "label": "French",
                "entries": {
                    "app.device_overview": "Vue d'appareils",
                    "actions.trigger_sync": "Lancer la synchronisation",
                },
            }
        }
    }
    response = client.post(
        "/translations/import",
        json={
            "project": project.model_dump(mode="json"),
            "format": "json",
            "content": json.dumps(payload),
        },
    )
    assert response.status_code == 200
    body = response.json()
    assert body["issues"] == []
    assert "fr" in body["translations"]
    assert "en" in body["translations"]
    assert body["translations"]["fr"]["entries"]["actions.trigger_sync"].startswith("Lancer")


def test_translation_import_csv_updates_entries() -> None:
    client = _client()
    project = get_template_project()
    csv_payload = "key,en,es\nstatus.banner,Online,En linea\n"
    response = client.post(
        "/translations/import",
        json={
            "project": project.model_dump(mode="json"),
            "format": "csv",
            "content": csv_payload,
        },
    )
    assert response.status_code == 200
    body = response.json()
    assert body["translations"]["en"]["entries"]["status.banner"] == "Online"
    assert body["translations"]["es"]["entries"]["status.banner"] == "En linea"
