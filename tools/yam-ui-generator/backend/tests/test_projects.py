import json
import yaml

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


def test_export_endpoint_can_prune_unused_styles() -> None:
    client = _client()
    project = {
        "app": {},
        "state": {},
        "translations": {"en": {"label": "English", "entries": {}}},
        "styles": {
            "used_style": {
                "name": "Used",
                "category": "surface",
                "value": {"backgroundColor": "#ffffff"},
            },
            "unused_style": {
                "name": "Unused",
                "category": "surface",
                "value": {"backgroundColor": "#000000"},
            },
        },
        "components": {},
        "screens": {
            "main": {
                "name": "main",
                "initial": True,
                "widgets": [{"type": "label", "id": "hero", "text": "Hello", "style": "used_style"}],
            }
        },
    }
    response = client.post(
        "/projects/export",
        json={"project": project, "options": {"prune_unused_styles": True}},
    )
    assert response.status_code == 200
    body = response.json()
    exported = yaml.safe_load(body["yaml"])
    assert "used_style" in exported["styles"]
    assert "unused_style" not in exported["styles"]
    assert any("Pruned" in issue["message"] for issue in body["issues"])


def test_export_endpoint_can_include_asset_manifest() -> None:
    client = _client()
    project = {
        "app": {},
        "state": {},
        "translations": {"en": {"label": "English", "entries": {}}},
        "styles": {},
        "components": {},
        "screens": {
            "main": {
                "name": "main",
                "initial": True,
                "widgets": [{"type": "img", "id": "hero", "src": "media/hero.png"}],
            }
        },
    }
    response = client.post(
        "/projects/export",
        json={"project": project, "options": {"include_asset_manifest": True}},
    )
    assert response.status_code == 200
    body = response.json()
    exported = yaml.safe_load(body["yaml"])
    manifest = exported["app"]["asset_manifest"]
    assets = manifest["assets"]
    assert manifest["asset_count"] == len(assets)
    assert any(asset["path"] == "media/hero.png" for asset in assets)
    hero = next(asset for asset in assets if asset["path"] == "media/hero.png")
    assert hero["usage_count"] >= 1


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


def test_validate_reports_semantic_cross_reference_warnings() -> None:
    client = _client()
    project = {
        "app": {
            "initial_screen": "missing_screen",
            "locale": "fr",
            "supported_locales": ["en", "fr"],
        },
        "state": {},
        "translations": {
            "en": {
                "label": "English",
                "entries": {
                    "known.key": "Known",
                },
            }
        },
        "styles": {},
        "components": {},
        "screens": {
            "main": {
                "name": "main",
                "initial": True,
                "widgets": [
                    {
                        "type": "label",
                        "id": "label-1",
                        "text": "{{ t('missing.key') }}",
                        "style": "missing-style",
                    }
                ],
            }
        },
    }

    response = client.post("/projects/validate", json={"project": project})
    assert response.status_code == 200
    body = response.json()
    assert body["valid"] is True  # semantic findings are warnings
    issues = body["issues"]
    assert any(issue["path"] == "/app/initial_screen" and "not defined under screens" in issue["message"] for issue in issues)
    assert any(issue["path"] == "/app/locale" and "missing in translations" in issue["message"] for issue in issues)
    assert any(issue["path"] == "/app/supported_locales/1" and "missing in translations" in issue["message"] for issue in issues)
    assert any(issue["path"] == "/screens/main/widgets/0/style" and "not defined" in issue["message"] for issue in issues)
    assert any(issue["path"] == "/screens/main/widgets/0/text" and "Translation key" in issue["message"] for issue in issues)

def test_preview_render_endpoint_returns_summary_and_findings() -> None:
    client = _client()
    project = {
        "app": {
            "initial_screen": "missing",
        },
        "state": {},
        "translations": {
            "en": {
                "label": "English",
                "entries": {},
            }
        },
        "styles": {},
        "components": {},
        "screens": {
            "main": {
                "name": "main",
                "initial": True,
                "widgets": [
                    {
                        "type": "label",
                        "id": "label-1",
                        "style": "missing-style",
                    }
                ],
            }
        },
    }

    response = client.post("/preview/render", json={"project": project, "mode": "local"})
    assert response.status_code == 200
    body = response.json()
    assert body["status"] == "issues"
    assert body["summary"]["screen_count"] == 1
    assert body["summary"]["finding_count"] >= 1
    assert any(finding["path"] == "/screens/main/widgets/0/style" for finding in body["findings"])


def test_validate_reports_asset_binding_and_event_semantic_warnings() -> None:
    client = _client()
    project = {
        "app": {
            "asset_manifest": {
                "assets": [
                    {"path": "media/known.png"},
                ]
            }
        },
        "state": {
            "device": {
                "connected": 4,
            }
        },
        "translations": {
            "en": {
                "label": "English",
                "entries": {},
            }
        },
        "styles": {},
        "components": {},
        "screens": {
            "main": {
                "name": "main",
                "initial": True,
                "widgets": [
                    {
                        "type": "img",
                        "id": "img-1",
                        "src": "../unsafe.png",
                        "bindings": {
                            "text": "{{ state.device.missing }}",
                            "": "state.device.connected",
                        },
                        "events": {
                            "on_click": [
                                "push(missing_screen)",
                                "modal(missing_component)",
                                "set(state.missing.path, 1)",
                                "set(user.name, 1)",
                                "",
                            ],
                            "on_invalid": 12,
                            "": ["noop()"],
                        },
                    }
                ],
            }
        },
    }

    response = client.post("/projects/validate", json={"project": project})
    assert response.status_code == 200
    body = response.json()
    assert body["valid"] is True
    issues = body["issues"]
    assert any(issue["path"] == "/screens/main/widgets/0/src" and "parent directory traversal" in issue["message"] for issue in issues)
    assert any(issue["path"] == "/screens/main/widgets/0/src" and "asset_manifest" in issue["message"] for issue in issues)
    assert any(issue["path"] == "/screens/main/widgets/0/bindings/text" and "state.device.missing" in issue["message"] for issue in issues)
    assert any(issue["path"] == "/screens/main/widgets/0/bindings" and "Binding names" in issue["message"] for issue in issues)
    assert any(issue["path"] == "/screens/main/widgets/0/events/on_click/0" and "Target screen" in issue["message"] for issue in issues)
    assert any(issue["path"] == "/screens/main/widgets/0/events/on_click/1" and "Modal component" in issue["message"] for issue in issues)
    assert any(issue["path"] == "/screens/main/widgets/0/events/on_click/2" and "state.missing.path" in issue["message"] for issue in issues)
    assert any(issue["path"] == "/screens/main/widgets/0/events/on_click/3" and "must target a state path" in issue["message"] for issue in issues)
    assert any(issue["path"] == "/screens/main/widgets/0/events/on_click/4" and "must not be empty" in issue["message"] for issue in issues)
    assert any(issue["path"] == "/screens/main/widgets/0/events/on_invalid" and "string or string array" in issue["message"] for issue in issues)
    assert any(issue["path"] == "/screens/main/widgets/0/events" and "Event names" in issue["message"] for issue in issues)


def test_roundtrip_preserves_semantic_state_for_complex_project_fixture() -> None:
    client = _client()
    project = {
        "app": {
            "name": "Roundtrip Fixture",
            "initial_screen": "main",
            "locale": "en",
            "supported_locales": ["en", "es"],
            "asset_manifest": {
                "assets": [
                    {"path": "media/hero.png", "kind": "image", "usage_count": 1},
                ]
            },
        },
        "state": {
            "dashboard": {
                "title": "Overview",
                "count": 3,
            }
        },
        "translations": {
            "en": {
                "label": "English",
                "entries": {
                    "dashboard.title": "Dashboard",
                    "button.sync": "Sync",
                },
            },
            "es": {
                "label": "Espanol",
                "entries": {
                    "dashboard.title": "Tablero",
                    "button.sync": "Sincronizar",
                },
            },
        },
        "styles": {
            "hero": {
                "name": "Hero",
                "category": "surface",
                "value": {"backgroundColor": "#111827", "color": "#f8fafc", "padding": 12},
            },
            "title": {
                "name": "Title",
                "category": "text",
                "value": {"fontSize": 24, "fontWeight": 700, "color": "#f8fafc"},
            },
        },
        "components": {
            "sync_modal": {
                "description": "Modal card",
                "widgets": [
                    {
                        "type": "panel",
                        "id": "modal-root",
                        "style": "hero",
                        "widgets": [
                            {
                                "type": "label",
                                "id": "modal-label",
                                "text": "{{ t('dashboard.title') }}",
                                "style": "title",
                            }
                        ],
                    }
                ],
            }
        },
        "screens": {
            "main": {
                "name": "main",
                "initial": True,
                "widgets": [
                    {
                        "type": "column",
                        "id": "main-col",
                        "widgets": [
                            {
                                "type": "img",
                                "id": "hero-img",
                                "src": "media/hero.png",
                            },
                            {
                                "type": "label",
                                "id": "title-label",
                                "text": "{{ t('dashboard.title') }}",
                                "style": "title",
                                "bindings": {
                                    "value": "{{ state.dashboard.title }}",
                                },
                            },
                            {
                                "type": "button",
                                "id": "sync-btn",
                                "text": "{{ t('button.sync') }}",
                                "events": {
                                    "on_click": [
                                        "push(settings)",
                                        "modal(sync_modal)",
                                        "set(state.dashboard.count, 5)",
                                    ]
                                },
                            },
                        ],
                    }
                ],
            },
            "settings": {
                "name": "settings",
                "initial": False,
                "widgets": [],
            },
        },
    }

    export_response = client.post("/projects/export", json={"project": project})
    assert export_response.status_code == 200
    export_body = export_response.json()
    assert export_body["issues"] == []
    yaml_text = export_body["yaml"]

    import_response = client.post("/projects/import", json={"yaml": yaml_text})
    assert import_response.status_code == 200
    import_body = import_response.json()
    assert import_body["issues"] == []
    imported = import_body["project"]

    validate_response = client.post("/projects/validate", json={"project": imported})
    assert validate_response.status_code == 200
    validate_body = validate_response.json()
    assert validate_body["valid"] is True
    assert validate_body["issues"] == []

    assert imported["app"]["initial_screen"] == "main"
    assert imported["app"]["asset_manifest"]["assets"][0]["path"] == "media/hero.png"
    assert imported["state"]["dashboard"]["count"] == 3
    assert imported["translations"]["es"]["entries"]["button.sync"] == "Sincronizar"
    assert imported["styles"]["hero"]["category"] == "surface"
    assert imported["components"]["sync_modal"]["widgets"][0]["id"] == "modal-root"
    assert imported["screens"]["main"]["widgets"][0]["widgets"][0]["src"] == "media/hero.png"
    assert imported["screens"]["main"]["widgets"][0]["widgets"][1]["bindings"]["value"] == "{{ state.dashboard.title }}"
    assert imported["screens"]["main"]["widgets"][0]["widgets"][2]["events"]["on_click"][0] == "push(settings)"
    assert imported["screens"]["main"]["widgets"][0]["widgets"][2]["events"]["on_click"][1] == "modal(sync_modal)"


def test_import_migrates_legacy_screen_list_and_translation_map() -> None:
    client = _client()
    legacy_yaml = """
initial_screen: main
locale: en
state:
  counter: 1
translations:
  en:
    app.title: Legacy Title
screens:
  - name: main
    initial: true
    widgets:
      - type: label
        id: label-1
        text: "{{ t('app.title') }}"
"""

    response = client.post("/projects/import", json={"yaml": legacy_yaml})
    assert response.status_code == 200
    body = response.json()
    project = body["project"]
    issues = body["issues"]

    assert project["app"]["initial_screen"] == "main"
    assert project["app"]["locale"] == "en"
    assert "main" in project["screens"]
    assert project["translations"]["en"]["entries"]["app.title"] == "Legacy Title"
    assert any("Migrated legacy screen list" in issue["message"] for issue in issues)
    assert any("Migrated legacy translation buckets" in issue["message"] for issue in issues)


def test_import_migrates_legacy_translation_values_array() -> None:
    client = _client()
    legacy_yaml = """
app:
  initial_screen: main
state: {}
translations:
  en:
    values:
      - key: app.title
        value: Legacy Values API
components: {}
styles: {}
screens:
  main:
    name: main
    initial: true
    widgets: []
"""

    response = client.post("/projects/import", json={"yaml": legacy_yaml})
    assert response.status_code == 200
    body = response.json()
    project = body["project"]
    issues = body["issues"]

    assert project["translations"]["en"]["entries"]["app.title"] == "Legacy Values API"
    assert any("Migrated legacy translation buckets" in issue["message"] for issue in issues)
