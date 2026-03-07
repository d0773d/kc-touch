from fastapi.testclient import TestClient

from yam_ui_generator.api import app


def test_health_endpoint() -> None:
    client = TestClient(app)
    response = client.get("/health")
    assert response.status_code == 200
    assert response.json()["status"] == "ok"


def test_contract_endpoint_exposes_version_and_migration_support() -> None:
    client = TestClient(app)
    response = client.get("/contract")
    assert response.status_code == 200
    body = response.json()
    assert body["api_version"] == "1.0"
    assert body["schema_version"] == "2020-12"
    assert "legacy_screen_list" in body["migration_support"]


def test_validation_errors_use_standard_error_envelope() -> None:
    client = TestClient(app)
    response = client.post("/projects/export", json={})
    assert response.status_code == 422
    body = response.json()
    assert body["error"]["code"] == "validation_error"
    assert body["error"]["message"] == "Request validation failed"
    assert isinstance(body["error"]["details"], list)
