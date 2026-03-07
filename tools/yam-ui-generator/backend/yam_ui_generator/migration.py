"""Migration helpers for legacy YamUI project payloads."""

from __future__ import annotations

from typing import Any, Dict, List, Tuple

from .models import ValidationIssue


def migrate_project_payload(raw: Dict[str, Any]) -> Tuple[Dict[str, Any], List[ValidationIssue]]:
    """Migrate known legacy payload shapes into the current schema."""

    payload = dict(raw or {})
    issues: List[ValidationIssue] = []

    app = payload.get("app")
    if not isinstance(app, dict):
        app = {}
        payload["app"] = app

    # Legacy top-level app settings.
    for key in ("initial_screen", "locale", "supported_locales"):
        if key in payload and key not in app:
            app[key] = payload.pop(key)
            issues.append(
                ValidationIssue(
                    path=f"/{key}",
                    message=f"Migrated legacy top-level '{key}' into app.{key}",
                    severity="warning",
                )
            )

    # Legacy screens as a list.
    screens = payload.get("screens")
    if isinstance(screens, list):
        migrated_screens: Dict[str, Any] = {}
        for index, screen in enumerate(screens):
            if not isinstance(screen, dict):
                continue
            name = str(screen.get("name") or f"screen_{index + 1}").strip()
            if not name:
                name = f"screen_{index + 1}"
            migrated_screens[name] = {**screen, "name": name}
        payload["screens"] = migrated_screens
        issues.append(
            ValidationIssue(
                path="/screens",
                message="Migrated legacy screen list into keyed screen map",
                severity="warning",
            )
        )

    # Legacy translations where locale values map directly to key/value entries.
    translations = payload.get("translations")
    if isinstance(translations, dict):
        migrated_translations: Dict[str, Any] = {}
        migrated_any = False
        for locale, bucket in translations.items():
            if not isinstance(bucket, dict):
                migrated_translations[locale] = {"entries": {}}
                migrated_any = True
                continue
            if "entries" in bucket:
                migrated_translations[locale] = bucket
                continue
            if all(isinstance(k, str) and isinstance(v, str) for k, v in bucket.items()):
                migrated_translations[locale] = {"entries": bucket}
                migrated_any = True
                continue
            values = bucket.get("values")
            if isinstance(values, list):
                entry_map: Dict[str, str] = {}
                for item in values:
                    if not isinstance(item, dict):
                        continue
                    key = str(item.get("key", "")).strip()
                    if not key:
                        continue
                    entry_map[key] = str(item.get("value", ""))
                migrated_translations[locale] = {"entries": entry_map}
                migrated_any = True
                continue
            migrated_translations[locale] = {"entries": {}}
            migrated_any = True
        payload["translations"] = migrated_translations
        if migrated_any:
            issues.append(
                ValidationIssue(
                    path="/translations",
                    message="Migrated legacy translation buckets into translation locale objects",
                    severity="warning",
                )
            )

    for key in ("state", "styles", "components", "translations"):
        if key not in payload or not isinstance(payload.get(key), dict):
            payload[key] = {}
            issues.append(
                ValidationIssue(
                    path=f"/{key}",
                    message=f"Initialized missing legacy field '{key}' with an empty object",
                    severity="warning",
                )
            )

    if "screens" not in payload or not isinstance(payload.get("screens"), dict):
        payload["screens"] = {}
        issues.append(
            ValidationIssue(
                path="/screens",
                message="Initialized missing legacy field 'screens' with an empty object",
                severity="warning",
            )
        )

    return payload, issues
