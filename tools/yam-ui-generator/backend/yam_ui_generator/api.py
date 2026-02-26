"""FastAPI application for the Yam UI Generator backend."""

from __future__ import annotations

import json
from typing import List

from fastapi import File, Form, HTTPException, UploadFile, FastAPI
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse

from .asset_service import collect_asset_catalog, ingest_uploaded_asset, resolve_asset_file, update_asset_tags
from .models import (
    AssetCatalogRequest,
    AssetCatalogResponse,
    AssetTagUpdateRequest,
    AssetTagUpdateResponse,
    AssetUploadResponse,
    Project,
    ProjectExportRequest,
    ProjectExportResponse,
    ProjectImportRequest,
    ProjectImportResponse,
    ProjectSettingsResponse,
    ProjectSettingsUpdateRequest,
    ProjectSettingsUpdateResponse,
    ProjectValidateRequest,
    ProjectValidateResponse,
    StyleLintRequest,
    StyleLintResponse,
    StylePreviewRequest,
    StylePreviewResponse,
    TranslationExportRequest,
    TranslationExportResponse,
    TranslationImportRequest,
    TranslationImportResponse,
    ValidationIssue,
)
from .palette import PALETTE
from .project_service import (
    export_project_to_yaml,
    export_translations_payload,
    apply_project_settings,
    import_project_from_yaml,
    import_translations_payload,
    validate_payload,
)
from .schema import PROJECT_SCHEMA
from .template_project import get_template_project

app = FastAPI(title="Yam UI Generator API", version="0.1.0")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


@app.get("/health")
def health() -> dict[str, str]:
    return {"status": "ok"}


@app.get("/widgets/palette")
def get_palette() -> list[dict[str, str | bool | list[str]]]:
    return [entry.model_dump() for entry in PALETTE]


@app.get("/schema")
def get_schema() -> dict[str, object]:
    return PROJECT_SCHEMA


@app.get("/projects/template", response_model=Project)
def get_project_template() -> Project:
    return get_template_project()


@app.get("/project/settings", response_model=ProjectSettingsResponse)
def get_project_settings() -> ProjectSettingsResponse:
    template = get_template_project()
    return ProjectSettingsResponse(settings=dict(template.app or {}))


@app.put("/project/settings", response_model=ProjectSettingsUpdateResponse)
def put_project_settings(payload: ProjectSettingsUpdateRequest) -> ProjectSettingsUpdateResponse:
    updated_project, issues = apply_project_settings(payload.project, payload.settings)
    issues.extend(validate_payload(updated_project, None))
    return ProjectSettingsUpdateResponse(
        project=updated_project,
        settings=dict(updated_project.app or {}),
        issues=issues,
    )


@app.post("/styles/preview", response_model=StylePreviewResponse)
def preview_style(payload: StylePreviewRequest) -> StylePreviewResponse:
    token = payload.token
    background = token.value.get("backgroundColor") or token.value.get("color") or "#e2e8f0"
    foreground = token.value.get("color") or "#0f172a"
    preview = {
        "category": token.category,
        "backgroundColor": background,
        "color": foreground,
        "description": token.description or token.name,
        "widget": payload.widget.model_dump(mode="json") if payload.widget else None,
    }
    return StylePreviewResponse(preview=preview)


@app.post("/styles/lint", response_model=StyleLintResponse)
def lint_styles(payload: StyleLintRequest) -> StyleLintResponse:
    issues: list[ValidationIssue] = []
    seen: set[str] = set()
    for name, token in payload.tokens.items():
        if token.name in seen:
            issues.append(
                ValidationIssue(
                    path=f"/styles/{name}",
                    message=f"Duplicate style name '{token.name}'",
                    severity="warning",
                )
            )
        seen.add(token.name)
        if token.category in {"color", "surface"} and "backgroundColor" not in token.value:
            issues.append(
                ValidationIssue(
                    path=f"/styles/{name}",
                    message="backgroundColor is recommended for color/surface styles",
                    severity="warning",
                )
            )
        if token.category == "text" and "fontSize" not in token.value:
            issues.append(
                ValidationIssue(
                    path=f"/styles/{name}",
                    message="fontSize missing for text style",
                    severity="warning",
                )
            )
    return StyleLintResponse(issues=issues)


@app.post("/assets/catalog", response_model=AssetCatalogResponse)
def build_asset_catalog(payload: AssetCatalogRequest) -> AssetCatalogResponse:
    assets = collect_asset_catalog(payload.project, payload.filters)
    return AssetCatalogResponse(assets=assets)


def _parse_tags(raw: str | None) -> List[str]:
    if not raw:
        return []
    try:
        data = json.loads(raw)
    except json.JSONDecodeError:
        data = None
    if isinstance(data, list):
        return [str(tag).strip() for tag in data if str(tag).strip()]
    return [token.strip() for token in raw.split(",") if token.strip()]


@app.post("/assets/upload", response_model=AssetUploadResponse)
async def upload_asset(
    file: UploadFile = File(...),
    path: str | None = Form(None),
    tags: str | None = Form(None),
) -> AssetUploadResponse:
    try:
        asset = ingest_uploaded_asset(file.file, file.filename, path, _parse_tags(tags))
    except (ValueError, RuntimeError) as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc
    finally:
        await file.close()
    return AssetUploadResponse(asset=asset)


@app.patch("/assets/catalog/tags", response_model=AssetTagUpdateResponse)
def patch_asset_tags(payload: AssetTagUpdateRequest) -> AssetTagUpdateResponse:
    try:
        asset = update_asset_tags(payload.path, payload.tags, project=payload.project)
    except (ValueError, RuntimeError) as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc
    return AssetTagUpdateResponse(asset=asset)


@app.get("/assets/files/{file_path:path}")
def download_asset(file_path: str):
    try:
        resolved = resolve_asset_file(file_path)
    except FileNotFoundError:
        raise HTTPException(status_code=404, detail="Asset not found") from None
    except RuntimeError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc
    return FileResponse(resolved)


@app.post("/projects/import", response_model=ProjectImportResponse)
def import_project(payload: ProjectImportRequest) -> ProjectImportResponse:
    try:
        project, issues = import_project_from_yaml(payload.yaml)
    except ValueError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc
    return ProjectImportResponse(project=project, issues=issues)


@app.post("/projects/export", response_model=ProjectExportResponse)
def export_project(payload: ProjectExportRequest) -> ProjectExportResponse:
    yaml_text, issues = export_project_to_yaml(payload.project)
    return ProjectExportResponse(yaml=yaml_text, issues=issues)


@app.post("/projects/validate", response_model=ProjectValidateResponse)
def validate_project(payload: ProjectValidateRequest) -> ProjectValidateResponse:
    try:
        payload.ensure_payload()
    except ValueError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc
    issues = validate_payload(payload.project, payload.yaml)
    is_valid = not any(issue.severity == "error" for issue in issues)
    return ProjectValidateResponse(valid=is_valid, issues=issues)


@app.post("/translations/export", response_model=TranslationExportResponse)
def export_translations(payload: TranslationExportRequest) -> TranslationExportResponse:
    try:
        content, filename, mime_type, issues = export_translations_payload(payload.project, payload.format)
    except ValueError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc
    return TranslationExportResponse(content=content, filename=filename, mime_type=mime_type, issues=issues)


@app.post("/translations/import", response_model=TranslationImportResponse)
def import_translations(payload: TranslationImportRequest) -> TranslationImportResponse:
    try:
        translations, issues = import_translations_payload(payload.project, payload.format, payload.content)
    except ValueError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc
    return TranslationImportResponse(translations=translations, issues=issues)
