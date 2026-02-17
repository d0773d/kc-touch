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
