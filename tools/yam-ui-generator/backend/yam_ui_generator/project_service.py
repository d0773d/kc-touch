"""High-level project operations used by the API layer."""

from __future__ import annotations

import csv
import io
import json
from typing import Dict, List, Tuple, Literal

from .models import Project, TranslationLocale, ValidationIssue
from .schema import validate_project
from .yaml_io import ensure_project_dict, project_from_yaml, project_to_yaml


def import_project_from_yaml(text: str) -> Tuple[Project, List[ValidationIssue]]:
    """Parse YAML and run schema validation."""

    project = project_from_yaml(text)
    issues = validate_project(ensure_project_dict(project))
    return project, issues


def export_project_to_yaml(project: Project) -> Tuple[str, List[ValidationIssue]]:
    """Serialize a project and validate the result for determinism."""

    issues = validate_project(ensure_project_dict(project))
    yaml_text = project_to_yaml(project)
    return yaml_text, issues


def validate_payload(project: Project | None, yaml_text: str | None) -> List[ValidationIssue]:
    """Validate either JSON or YAML payloads."""

    if project:
        return validate_project(ensure_project_dict(project))
    if yaml_text:
        loaded, issues = import_project_from_yaml(yaml_text)
        # import already validated; return issues without re-reading YAML
        return issues
    return [ValidationIssue(path="/", message="No payload to validate", severity="error")]


def apply_project_settings(project: Project, settings: Dict[str, object]) -> Tuple[Project, List[ValidationIssue]]:
    """Apply app-level settings and normalize dependent state."""

    next_project = project.model_copy(deep=True)
    next_app = dict(next_project.app or {})
    issues: List[ValidationIssue] = []

    for key, value in (settings or {}).items():
        next_app[key] = value

    requested_initial = str(next_app.get("initial_screen", "") or "").strip()
    if requested_initial:
        if requested_initial in next_project.screens:
            for name, screen in next_project.screens.items():
                screen.initial = name == requested_initial
        else:
            issues.append(
                ValidationIssue(
                    path="/app/initial_screen",
                    message=f"Initial screen '{requested_initial}' does not exist",
                    severity="warning",
                )
            )

    locale = str(next_app.get("locale", "") or "").strip()
    if locale and locale not in next_project.translations:
        next_project.translations[locale] = TranslationLocale(
            label=locale,
            entries={},
            metadata={},
        )

    raw_supported = next_app.get("supported_locales")
    supported_locales: List[str] = []
    if isinstance(raw_supported, list):
        supported_locales = [str(item).strip() for item in raw_supported if str(item).strip()]
    elif isinstance(raw_supported, str) and raw_supported.strip():
        supported_locales = [item.strip() for item in raw_supported.split(",") if item.strip()]

    if locale and locale not in supported_locales:
        supported_locales.append(locale)

    for code in supported_locales:
        if code not in next_project.translations:
            next_project.translations[code] = TranslationLocale(
                label=code,
                entries={},
                metadata={},
            )

    if supported_locales:
        next_app["supported_locales"] = supported_locales

    next_project.app = next_app
    return next_project, issues


TranslationFormat = Literal["json", "csv"]


def export_translations_payload(
    project: Project,
    fmt: TranslationFormat,
) -> Tuple[str, str, str, List[ValidationIssue]]:
    """Serialize translations into JSON or CSV."""

    normalized = (fmt or "json").lower()
    if normalized not in {"json", "csv"}:
        msg = "Unsupported translation export format"
        raise ValueError(msg)
    translations = {code: locale.model_copy(deep=True) for code, locale in (project.translations or {}).items()}
    issues = _collect_translation_issues(translations)
    if normalized == "json":
        content = _export_translations_json(translations)
        filename = "translations.json"
        mime_type = "application/json"
    else:
        content = _export_translations_csv(translations)
        filename = "translations.csv"
        mime_type = "text/csv"
    return content, filename, mime_type, issues


def import_translations_payload(
    project: Project,
    fmt: TranslationFormat,
    content: str,
) -> Tuple[Dict[str, TranslationLocale], List[ValidationIssue]]:
    """Parse uploaded translations and merge them into the project."""

    normalized = (fmt or "json").lower()
    if normalized == "json":
        parsed = _parse_translation_json(content)
    elif normalized == "csv":
        parsed = _parse_translation_csv(content)
    else:
        msg = "Unsupported translation import format"
        raise ValueError(msg)

    merged = {code: locale.model_copy(deep=True) for code, locale in (project.translations or {}).items()}
    for code, locale in parsed.items():
        existing = merged.get(code)
        if existing and not locale.label:
            locale.label = existing.label
        merged[code] = locale
    issues = _collect_translation_issues(merged)
    return merged, issues


def _export_translations_json(translations: Dict[str, TranslationLocale]) -> str:
    payload = {
        "translations": {
            code: locale.model_dump(mode="json") for code, locale in sorted(translations.items())
        }
    }
    return json.dumps(payload, ensure_ascii=False, indent=2)


def _export_translations_csv(translations: Dict[str, TranslationLocale]) -> str:
    locale_codes = sorted(translations.keys())
    key_set: set[str] = set()
    for locale in translations.values():
        key_set.update(locale.entries or {})
    sorted_keys = sorted(key_set)
    buffer = io.StringIO()
    writer = csv.writer(buffer)
    writer.writerow(["key", *locale_codes])
    for key in sorted_keys:
        row = [key]
        for code in locale_codes:
            locale = translations.get(code)
            row.append((locale.entries.get(key) if locale else "") or "")
        writer.writerow(row)
    return buffer.getvalue()


def _parse_translation_json(content: str) -> Dict[str, TranslationLocale]:
    if not content or not content.strip():
        msg = "Translation JSON payload is empty"
        raise ValueError(msg)
    try:
        data = json.loads(content)
    except json.JSONDecodeError as exc:
        raise ValueError("Invalid translation JSON payload") from exc
    if not isinstance(data, dict):
        msg = "Translations JSON must be an object"
        raise ValueError(msg)
    if "translations" in data and isinstance(data["translations"], dict):
        raw = data["translations"]
    elif "locales" in data and isinstance(data["locales"], list):
        raw = {}
        for entry in data["locales"]:
            if not isinstance(entry, dict):
                msg = "Locales entries must be objects"
                raise ValueError(msg)
            code = str(entry.get("code", "")).strip()
            if not code:
                msg = "Locale entries must include a code field"
                raise ValueError(msg)
            raw[code] = {k: v for k, v in entry.items() if k != "code"}
    else:
        raw = data

    translations: Dict[str, TranslationLocale] = {}
    for code, payload in raw.items():
        if not isinstance(code, str) or not code.strip():
            msg = "Locale codes must be non-empty strings"
            raise ValueError(msg)
        normalized_code = code.strip()
        if not isinstance(payload, dict):
            msg = f"Locale '{code}' must map to an object"
            raise ValueError(msg)
        body = payload
        if "entries" not in body:
            body = {"entries": body}
        translations[normalized_code] = TranslationLocale.model_validate(body)
    return translations


def _parse_translation_csv(content: str) -> Dict[str, TranslationLocale]:
    if not content or not content.strip():
        msg = "CSV payload is empty"
        raise ValueError(msg)
    reader = csv.reader(io.StringIO(content))
    try:
        header_row = next(reader)
    except StopIteration as exc:
        raise ValueError("CSV payload is empty") from exc
    headers = [cell.strip() for cell in header_row]
    if headers:
        headers[0] = headers[0].lstrip("\ufeff")
    if not headers or headers[0].lower() != "key":
        msg = "First CSV column must be 'key'"
        raise ValueError(msg)
    locale_headers = [code for code in headers[1:] if code]
    if not locale_headers:
        msg = "CSV must include at least one locale column"
        raise ValueError(msg)
    translations: Dict[str, TranslationLocale] = {
        code: TranslationLocale(entries={}) for code in locale_headers
    }
    for row in reader:
        if not row or not any(cell.strip() for cell in row):
            continue
        key = (row[0] if len(row) > 0 else "").strip()
        if not key:
            continue
        for idx, code in enumerate(locale_headers, start=1):
            value = row[idx] if idx < len(row) else ""
            translations[code].entries[key] = value
    return translations


def _collect_translation_issues(translations: Dict[str, TranslationLocale]) -> List[ValidationIssue]:
    issues: List[ValidationIssue] = []
    for code, locale in translations.items():
        for key, value in (locale.entries or {}).items():
            if not (value or "").strip():
                issues.append(
                    ValidationIssue(
                        path=f"/translations/{code}/entries/{key}",
                        message="Missing translation text",
                        severity="warning",
                    )
                )
    return issues
