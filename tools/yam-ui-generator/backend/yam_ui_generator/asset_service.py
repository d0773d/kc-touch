"""Helpers for deriving asset metadata from a YamUI project."""

from __future__ import annotations

import hashlib
import hmac
import json
import logging
import os
import time
from pathlib import Path
from typing import BinaryIO, Dict, Iterable, Iterator, List, Tuple
from urllib.parse import quote, urlparse

try:  # pragma: no cover - Pillow is installed at runtime
    from PIL import Image
except Exception:  # pragma: no cover - Pillow missing or misconfigured
    Image = None

from .models import AssetCatalogFilters, AssetReference, Project, Widget

_IMAGE_EXTENSIONS = {".png", ".jpg", ".jpeg", ".gif", ".svg", ".webp", ".bmp"}
_VIDEO_EXTENSIONS = {".mp4", ".mov", ".webm", ".mkv", ".avi"}
_AUDIO_EXTENSIONS = {".mp3", ".wav", ".aac", ".ogg"}
_FONT_EXTENSIONS = {".ttf", ".otf", ".woff", ".woff2"}

_TAGS_FILE_NAME = ".yamui_asset_tags.json"
_THUMBNAIL_DIR = ".yamui-thumbnails"
_LOCAL_ASSET_ROUTE = "/assets/files"
_DEFAULT_URL_TTL = 3600
_DEFAULT_MAX_UPLOAD_SIZE = 25 * 1024 * 1024  # 25 MB

_EXTRA_UPLOAD_EXTENSIONS = {".zip", ".json", ".yaml", ".yml", ".txt", ".csv", ".bin", ".glb", ".gltf"}
_ALLOWED_UPLOAD_EXTENSIONS = (
    _IMAGE_EXTENSIONS
    | _VIDEO_EXTENSIONS
    | _AUDIO_EXTENSIONS
    | _FONT_EXTENSIONS
    | _EXTRA_UPLOAD_EXTENSIONS
)

logger = logging.getLogger(__name__)


def _asset_root() -> Path | None:
    raw = os.environ.get("YAMUI_ASSET_ROOT")
    if not raw:
        return None
    return Path(raw).expanduser().resolve()


def _ensure_asset_root() -> Path:
    root = _asset_root()
    if not root:
        raise RuntimeError("YAMUI_ASSET_ROOT is not configured")
    root.mkdir(parents=True, exist_ok=True)
    return root


def _cdn_base() -> str | None:
    base = os.environ.get("YAMUI_ASSET_CDN")
    if not base:
        return None
    cleaned = base.strip().rstrip("/")
    if not cleaned:
        return None
    parsed = urlparse(cleaned)
    if not (parsed.scheme and parsed.netloc):
        logger.warning("Ignoring YAMUI_ASSET_CDN because it is not an absolute URL: %s", base)
        return None
    return cleaned


def _public_base_url() -> str | None:
    base = os.environ.get("YAMUI_ASSET_PUBLIC_BASE")
    return base.rstrip("/") if base else None


def _signing_secret() -> str | None:
    secret = os.environ.get("YAMUI_ASSET_SIGNING_SECRET")
    return secret if secret else None


def _url_ttl() -> int:
    raw = os.environ.get("YAMUI_ASSET_URL_TTL", str(_DEFAULT_URL_TTL))
    try:
        return max(60, int(raw))
    except ValueError:
        return _DEFAULT_URL_TTL


def _max_upload_size() -> int:
    raw = os.environ.get("YAMUI_ASSET_MAX_SIZE")
    if not raw:
        return _DEFAULT_MAX_UPLOAD_SIZE
    text = raw.strip().lower()
    multiplier = 1
    if text.endswith("kb"):
        multiplier = 1024
        text = text[:-2]
    elif text.endswith("mb"):
        multiplier = 1024 * 1024
        text = text[:-2]
    elif text.endswith("gb"):
        multiplier = 1024 * 1024 * 1024
        text = text[:-2]
    try:
        value = float(text)
    except ValueError:
        return _DEFAULT_MAX_UPLOAD_SIZE
    return max(1024, int(value * multiplier))


def _allow_unknown_extensions() -> bool:
    return os.environ.get("YAMUI_ASSET_ALLOW_UNKNOWN_EXT") == "1"


def _normalize_path(value: str) -> str:
    normalized = value.strip().replace("\\", "/")
    normalized = normalized.lstrip("/")
    segments = [segment for segment in normalized.split("/") if segment and segment not in {".", ".."}]
    return "/".join(segments)


def _asset_digest(normalized: str) -> str:
    return hashlib.sha1(normalized.encode("utf-8")).hexdigest()[:12]


def _guess_kind(path: str) -> str:
    extension = Path(path).suffix.lower()
    if extension in _IMAGE_EXTENSIONS:
        return "image"
    if extension in _VIDEO_EXTENSIONS:
        return "video"
    if extension in _AUDIO_EXTENSIONS:
        return "audio"
    if extension in _FONT_EXTENSIONS:
        return "font"
    if extension:
        return "binary"
    return "unknown"


def _resolve_asset_path(normalized: str) -> Path | None:
    root = _asset_root()
    if not root or not normalized:
        return None
    candidate = (root / normalized).resolve()
    try:
        candidate.relative_to(root)
    except ValueError:
        return None
    if candidate.exists() and candidate.is_file():
        return candidate
    return None


def _thumbnail_path(normalized: str) -> Path | None:
    root = _asset_root()
    if not root or not normalized:
        return None
    candidate = (root / _THUMBNAIL_DIR / normalized).with_suffix(".jpg")
    if candidate.exists() and candidate.is_file():
        return candidate
    return None


def _quote_path(normalized: str) -> str:
    return quote(normalized, safe="/+")


def _sign_url(url: str, normalized: str) -> str:
    secret = _signing_secret()
    if not secret:
        return url
    expires = int(time.time()) + _url_ttl()
    payload = f"{normalized}:{expires}".encode("utf-8")
    digest = hmac.new(secret.encode("utf-8"), payload, hashlib.sha256).hexdigest()
    separator = "&" if "?" in url else "?"
    return f"{url}{separator}token={digest}&expires={expires}"


def _public_url(normalized: str) -> str | None:
    if not normalized:
        return None
    cdn = _cdn_base()
    encoded = _quote_path(normalized)
    if cdn:
        url = f"{cdn}/{encoded}"
    else:
        root = _asset_root()
        if not root:
            return None
        local_route = f"{_LOCAL_ASSET_ROUTE}/{encoded}"
        public_base = _public_base_url()
        url = f"{public_base}{local_route}" if public_base else local_route
    return _sign_url(url, normalized)


def _thumbnail_url(normalized: str) -> str | None:
    thumbnail = _thumbnail_path(normalized)
    if not thumbnail:
        return None
    root = _asset_root()
    if not root:
        return None
    relative = thumbnail.relative_to(root)
    encoded = _quote_path(relative.as_posix())
    cdn = _cdn_base()
    if cdn:
        url = f"{cdn}/{encoded}"
    else:
        local_route = f"{_LOCAL_ASSET_ROUTE}/{encoded}"
        public_base = _public_base_url()
        url = f"{public_base}{local_route}" if public_base else local_route
    return _sign_url(url, normalized)


def _tag_store_path() -> Path:
    return _ensure_asset_root() / _TAGS_FILE_NAME


def _read_tag_store() -> Dict[str, List[str]]:
    path = _tag_store_path()
    if not path.exists():
        return {}
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError):
        return {}
    normalized: Dict[str, List[str]] = {}
    for key, value in payload.items():
        if not isinstance(value, list):
            continue
        normalized[key] = [str(tag).strip() for tag in value if str(tag).strip()]
    return normalized


def _write_tag_store(data: Dict[str, List[str]]) -> None:
    path = _tag_store_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2), encoding="utf-8")


def _normalize_tags(tags: Iterable[str]) -> List[str]:
    normalized: List[str] = []
    for tag in tags:
        trimmed = str(tag).strip()
        if trimmed and trimmed not in normalized:
            normalized.append(trimmed)
    return normalized


def _persist_tags(normalized: str, tags: Iterable[str]) -> Dict[str, List[str]]:
    store = _read_tag_store()
    normalized_tags = _normalize_tags(tags)
    if normalized_tags:
        store[normalized] = normalized_tags
    elif normalized in store:
        del store[normalized]
    _write_tag_store(store)
    return store


def _is_extension_allowed(extension: str) -> bool:
    if not extension:
        return False
    return extension in _ALLOWED_UPLOAD_EXTENSIONS


def _ensure_extension_allowed(extension: str) -> None:
    if not extension and not _allow_unknown_extensions():
        raise ValueError("Uploaded file must include an extension")
    if extension and not _is_extension_allowed(extension) and not _allow_unknown_extensions():
        allowed = ", ".join(sorted(_ALLOWED_UPLOAD_EXTENSIONS))
        raise ValueError(f"Extension '{extension}' is not allowed. Allowed types: {allowed}")


def _copy_stream_with_quota(stream: BinaryIO, destination: Path, limit: int) -> int:
    total = 0
    chunk_size = 1024 * 1024
    with destination.open("wb") as buffer:
        while True:
            chunk = stream.read(chunk_size)
            if not chunk:
                break
            total += len(chunk)
            if total > limit:
                buffer.flush()
                buffer.close()
                destination.unlink(missing_ok=True)
                human_limit = max(1, limit // (1024 * 1024))
                raise ValueError(f"File exceeds max upload size of {human_limit} MB")
            buffer.write(chunk)
    return total


def _maybe_generate_thumbnail(normalized: str, source: Path) -> None:
    if not Image:
        return
    extension = source.suffix.lower()
    if extension not in _IMAGE_EXTENSIONS:
        return
    thumbnail_root = _ensure_asset_root() / _THUMBNAIL_DIR
    thumbnail_path = (thumbnail_root / normalized).with_suffix(".jpg")
    thumbnail_path.parent.mkdir(parents=True, exist_ok=True)
    try:
        with Image.open(source) as img:
            img.convert("RGB")
            img.thumbnail((640, 640))
            img.save(thumbnail_path, format="JPEG", optimize=True, quality=80)
    except Exception as exc:  # pragma: no cover - best-effort thumbnail
        logger.debug("Thumbnail generation failed for %s: %s", normalized, exc)


def _compute_sha256(path: Path) -> str | None:
    if not path.exists():
        return None
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _build_search_index(asset: AssetReference) -> str:
    tokens = [asset.label, asset.path, " ".join(asset.tags), " ".join(asset.targets)]
    return " ".join(token.lower() for token in tokens if token)


def _iter_widgets(collection: Iterable[Widget], parent_path: Tuple[int, ...] = ()) -> Iterator[Tuple[Widget, Tuple[int, ...]]]:
    for index, widget in enumerate(collection):
        current_path = (*parent_path, index)
        yield widget, current_path
        if widget.widgets:
            yield from _iter_widgets(widget.widgets, current_path)


def _iter_asset_files() -> Iterator[str]:
    root = _asset_root()
    if not root or not root.exists():
        return iter(())

    def generator() -> Iterator[str]:
        for path in root.rglob("*"):
            if not path.is_file():
                continue
            relative = path.relative_to(root).as_posix()
            if relative == _TAGS_FILE_NAME or relative.startswith(f"{_THUMBNAIL_DIR}/"):
                continue
            yield relative

    return generator()


def _apply_filters(assets: List[AssetReference], filters: AssetCatalogFilters | None) -> List[AssetReference]:
    if not filters:
        return assets
    query = (filters.query or "").strip().lower()
    tag_filters = {tag.strip().lower() for tag in filters.tags if tag.strip()}
    target_filters = set(filters.targets)
    kind_filters = set(filters.kinds)

    def matches(asset: AssetReference) -> bool:
        if kind_filters and asset.kind not in kind_filters:
            return False
        if tag_filters and not tag_filters.issubset({tag.lower() for tag in asset.tags}):
            return False
        if target_filters and not any(target in target_filters for target in asset.targets):
            return False
        if query:
            haystack = asset.metadata.get("search_index") or _build_search_index(asset)
            if query not in haystack:
                return False
        return True

    return [asset for asset in assets if matches(asset)]


def _summarize_asset(
    normalized: str,
    *,
    usage_count: int,
    widget_ids: List[str],
    targets: List[str],
    tag_store: Dict[str, List[str]] | None = None,
) -> AssetReference:
    label = Path(normalized).name or normalized or "asset"
    kind = _guess_kind(normalized)
    resolved = _resolve_asset_path(normalized)
    download_url = _public_url(normalized)
    thumbnail_url = _thumbnail_url(normalized)
    preview_url = None
    if kind == "image":
        preview_url = download_url
        thumbnail_url = thumbnail_url or download_url
    elif kind == "video":
        preview_url = thumbnail_url or download_url
    metadata: Dict[str, object] = {}
    size_bytes = None
    sha256 = None
    if resolved:
        stat = resolved.stat()
        size_bytes = stat.st_size
        metadata["exists_on_disk"] = True
        metadata["filesystem_path"] = str(resolved)
        metadata["modified_ts"] = stat.st_mtime
        sha256 = _compute_sha256(resolved)
    else:
        metadata["exists_on_disk"] = False
    tags = (tag_store or _read_tag_store()).get(normalized, [])
    asset = AssetReference(
        id=_asset_digest(normalized or label),
        path=normalized,
        label=label,
        extension=Path(normalized).suffix.lower(),
        kind=kind,
        usage_count=usage_count,
        widget_ids=widget_ids,
        targets=targets,
        tags=tags,
        size_bytes=size_bytes,
        metadata=metadata,
        preview_url=preview_url,
        thumbnail_url=thumbnail_url,
        download_url=download_url,
    )
    if sha256:
        asset.metadata["sha256"] = sha256
    asset.metadata["search_index"] = _build_search_index(asset)
    return asset


def collect_asset_catalog(project: Project, filters: AssetCatalogFilters | None = None) -> List[AssetReference]:
    """Derive a normalized asset catalog for the given project and asset store."""

    tag_store = _read_tag_store() if _asset_root() else {}
    catalog: Dict[str, AssetReference] = {}

    def register(path: str, widget: Widget, target_label: str) -> None:
        normalized = _normalize_path(path)
        if not normalized:
            return
        entry = catalog.get(normalized)
        if not entry:
            entry = _summarize_asset(normalized, usage_count=0, widget_ids=[], targets=[], tag_store=tag_store or None)
            entry.metadata["source"] = "project"
            catalog[normalized] = entry
        entry.usage_count += 1
        widget_id = widget.id or "anonymous"
        if widget_id not in entry.widget_ids:
            entry.widget_ids.append(widget_id)
        if target_label not in entry.targets:
            entry.targets.append(target_label)

    for screen_name, screen in project.screens.items():
        target = f"screen:{screen_name}"
        for widget, _ in _iter_widgets(screen.widgets):
            if widget.src:
                register(widget.src, widget, target)

    for component_name, component in project.components.items():
        target = f"component:{component_name}"
        for widget, _ in _iter_widgets(component.widgets):
            if widget.src:
                register(widget.src, widget, target)

    if _asset_root():
        for stored in _iter_asset_files():
            normalized = _normalize_path(stored)
            if not normalized or normalized in catalog:
                continue
            entry = _summarize_asset(normalized, usage_count=0, widget_ids=[], targets=[], tag_store=tag_store)
            entry.metadata.setdefault("source", "store")
            catalog[normalized] = entry

    assets = sorted(catalog.values(), key=lambda asset: (-asset.usage_count, asset.label.lower()))
    return _apply_filters(assets, filters)


def ingest_uploaded_asset(stream: BinaryIO, filename: str, desired_path: str | None, tags: List[str]) -> AssetReference:
    normalized_input = desired_path or filename
    normalized = _normalize_path(normalized_input)
    if not normalized:
        raise ValueError("A destination path or filename is required")
    root = _ensure_asset_root()
    destination = (root / normalized).resolve()
    try:
        destination.relative_to(root)
    except ValueError as exc:  # pragma: no cover - sanity guard
        raise ValueError("Asset path escapes configured root") from exc
    destination.parent.mkdir(parents=True, exist_ok=True)
    extension = Path(normalized).suffix.lower()
    _ensure_extension_allowed(extension)
    if hasattr(stream, "seek"):
        try:
            stream.seek(0)
        except (OSError, ValueError):  # pragma: no cover - non-seekable streams
            pass
    max_size = _max_upload_size()
    bytes_written = _copy_stream_with_quota(stream, destination, max_size)
    if bytes_written == 0:
        destination.unlink(missing_ok=True)
        raise ValueError("Uploaded file is empty")
    _maybe_generate_thumbnail(normalized, destination)
    tag_store = _persist_tags(normalized, tags)
    return _summarize_asset(normalized, usage_count=0, widget_ids=[], targets=[], tag_store=tag_store)


def update_asset_tags(path: str, tags: List[str], project: Project | None = None) -> AssetReference:
    normalized = _normalize_path(path)
    if not normalized:
        raise ValueError("path is required")
    tag_store = _persist_tags(normalized, tags)
    if project:
        catalog = {asset.path: asset for asset in collect_asset_catalog(project)}
        asset = catalog.get(normalized)
        if asset:
            asset.tags = tag_store.get(normalized, [])
            return asset
    return _summarize_asset(normalized, usage_count=0, widget_ids=[], targets=[], tag_store=tag_store)


def resolve_asset_file(path: str) -> Path:
    normalized = _normalize_path(path)
    if not normalized:
        raise FileNotFoundError("Asset path is empty")
    resolved = _resolve_asset_path(normalized)
    if not resolved:
        root = _asset_root()
        if not root:
            raise RuntimeError("YAMUI_ASSET_ROOT is not configured")
        raise FileNotFoundError(normalized)
    return resolved
