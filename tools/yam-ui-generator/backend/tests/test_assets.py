import io
import json
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from yam_ui_generator.api import app


@pytest.fixture(name="asset_client")
def _asset_client(tmp_path, monkeypatch):
    root = tmp_path / "assets"
    root.mkdir()
    monkeypatch.setenv("YAMUI_ASSET_ROOT", str(root))
    monkeypatch.delenv("YAMUI_ASSET_CDN", raising=False)
    monkeypatch.delenv("YAMUI_ASSET_SIGNING_SECRET", raising=False)
    monkeypatch.delenv("YAMUI_ASSET_URL_TTL", raising=False)
    return TestClient(app), root


def test_asset_catalog_endpoint_counts_usage(asset_client) -> None:
    client, root = asset_client
    media_dir = root / "media"
    media_dir.mkdir(parents=True, exist_ok=True)
    (media_dir / "hero.png").write_bytes(b"hero")
    (media_dir / "thumb.png").write_bytes(b"thumb")

    project = {
        "app": {},
        "state": {},
        "styles": {},
        "components": {},
        "screens": {
            "main": {
                "name": "main",
                "initial": True,
                "widgets": [
                    {"type": "img", "id": "hero", "src": "media/hero.png"},
                    {
                        "type": "row",
                        "id": "gallery",
                        "widgets": [
                            {"type": "img", "id": "thumb-1", "src": "media/thumb.png"},
                            {"type": "img", "id": "thumb-2", "src": "media/thumb.png"},
                        ],
                    },
                ],
            }
        },
    }

    response = client.post("/assets/catalog", json={"project": project})
    assert response.status_code == 200
    assets = {asset["path"]: asset for asset in response.json()["assets"]}

    hero = assets["media/hero.png"]
    assert hero["usage_count"] == 1
    assert hero["targets"] == ["screen:main"]
    assert hero["widget_ids"] == ["hero"]
    assert hero["preview_url"]

    thumb = assets["media/thumb.png"]
    assert thumb["usage_count"] == 2
    assert thumb["targets"] == ["screen:main"]
    assert set(thumb["widget_ids"]) == {"thumb-1", "thumb-2"}
    assert thumb["preview_url"]


def test_asset_upload_creates_file(asset_client) -> None:
    client, root = asset_client
    files = {"file": ("upload.png", b"binary", "image/png")}
    response = client.post(
        "/assets/upload",
        data={"path": "media/upload.png", "tags": json.dumps(["ui", "hero"])},
        files=files,
    )
    assert response.status_code == 200
    asset = response.json()["asset"]
    assert asset["path"] == "media/upload.png"
    assert set(asset["tags"]) == {"ui", "hero"}
    assert (root / "media" / "upload.png").exists()


def test_asset_tag_patch_updates_catalog(asset_client) -> None:
    client, root = asset_client
    media_dir = root / "media"
    media_dir.mkdir(parents=True, exist_ok=True)
    (media_dir / "pattern.png").write_bytes(b"pattern")

    project = {
        "app": {},
        "state": {},
        "styles": {},
        "components": {},
        "screens": {
            "main": {
                "name": "main",
                "widgets": [{"type": "img", "id": "p", "src": "media/pattern.png"}],
            }
        },
    }

    response = client.patch(
        "/assets/catalog/tags",
        json={"path": "media/pattern.png", "tags": ["bg", "panel"], "project": project},
    )
    assert response.status_code == 200
    body = response.json()["asset"]
    assert body["tags"] == ["bg", "panel"]
    assert body["usage_count"] == 1


def test_asset_file_route_serves_bytes(asset_client) -> None:
    client, root = asset_client
    target = root / "media" / "served.png"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(b"served")

    response = client.get("/assets/files/media/served.png")
    assert response.status_code == 200
    assert response.content == b"served"


def test_asset_catalog_filters(asset_client) -> None:
    client, root = asset_client
    media_dir = root / "media"
    media_dir.mkdir(parents=True, exist_ok=True)
    (media_dir / "hero.png").write_bytes(b"hero")

    response = client.post(
        "/assets/upload",
        data={"path": "media/gallery.png", "tags": json.dumps(["gallery"])},
        files={"file": ("gallery.png", b"gallery-bytes", "image/png")},
    )
    assert response.status_code == 200

    project = {
        "app": {},
        "state": {},
        "styles": {},
        "components": {},
        "screens": {
            "main": {
                "name": "main",
                "widgets": [
                    {"type": "img", "id": "hero", "src": "media/hero.png"},
                    {"type": "img", "id": "gallery", "src": "media/gallery.png"},
                ],
            }
        },
    }

    response = client.post(
        "/assets/catalog",
        json={
            "project": project,
            "filters": {"tags": ["gallery"], "targets": ["screen:main"], "kinds": ["image"]},
        },
    )
    assert response.status_code == 200
    payload = response.json()["assets"]
    assert len(payload) == 1
    assert payload[0]["path"] == "media/gallery.png"

    response = client.post(
        "/assets/catalog",
        json={"project": project, "filters": {"query": "hero"}},
    )
    assert response.status_code == 200
    filtered = response.json()["assets"]
    assert len(filtered) == 1
    assert filtered[0]["path"] == "media/hero.png"


def test_asset_upload_respects_size_limit(asset_client, monkeypatch) -> None:
    client, _ = asset_client
    monkeypatch.setenv("YAMUI_ASSET_MAX_SIZE", "1kb")
    files = {"file": ("large.png", b"x" * 2048, "image/png")}
    response = client.post("/assets/upload", data={"path": "media/too_big.png"}, files=files)
    assert response.status_code == 400
    assert "max upload size" in response.json()["detail"].lower()


def test_asset_upload_rejects_extension(asset_client, monkeypatch) -> None:
    client, _ = asset_client
    monkeypatch.delenv("YAMUI_ASSET_ALLOW_UNKNOWN_EXT", raising=False)
    files = {"file": ("script.exe", b"binary", "application/octet-stream")}
    response = client.post("/assets/upload", data={"path": "media/script.exe"}, files=files)
    assert response.status_code == 400
    assert "extension" in response.json()["detail"].lower()


def test_asset_upload_generates_thumbnail(asset_client) -> None:
    Image = pytest.importorskip("PIL.Image")
    client, root = asset_client
    image = Image.new("RGB", (8, 8), color="red")
    buffer = io.BytesIO()
    image.save(buffer, format="PNG")
    buffer.seek(0)

    files = {"file": ("thumb.png", buffer.getvalue(), "image/png")}
    response = client.post("/assets/upload", data={"path": "media/thumb.png"}, files=files)
    assert response.status_code == 200

    thumbnail = root / ".yamui-thumbnails" / "media" / "thumb.jpg"
    assert thumbnail.exists()