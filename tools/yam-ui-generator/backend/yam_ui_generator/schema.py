"""JSON schema helpers for YamUI projects."""

from __future__ import annotations

import re
from typing import Any, Dict, List

from jsonschema import Draft202012Validator, ValidationError

from .models import ValidationIssue

WIDGET_SCHEMA: Dict[str, Any] = {
    "$id": "https://yamui.spec/widget.schema.json",
    "type": "object",
    "required": ["type"],
    "properties": {
        "type": {"type": "string"},
        "id": {"type": "string"},
        "text": {"type": "string"},
        "src": {"type": "string"},
        "style": {"type": "string"},
        "props": {"type": "object"},
        "events": {"type": "object"},
        "bindings": {"type": "object"},
        "accessibility": {"type": "object"},
        "widgets": {
            "type": "array",
            "items": {"$ref": "https://yamui.spec/widget.schema.json"},
            "default": [],
        },
    },
    "additionalProperties": True,
}

COLOR_STYLE_SCHEMA: Dict[str, Any] = {
    "$id": "https://yamui.spec/style-color.schema.json",
    "type": "object",
    "properties": {
        "backgroundColor": {"type": "string"},
        "color": {"type": "string"},
        "borderColor": {"type": "string"},
        "accentColor": {"type": "string"},
        "overlayColor": {"type": "string"},
    },
    "additionalProperties": False,
    "default": {},
}

SURFACE_STYLE_SCHEMA: Dict[str, Any] = {
    "$id": "https://yamui.spec/style-surface.schema.json",
    "type": "object",
    "properties": {
        "backgroundColor": {"type": "string"},
        "color": {"type": "string"},
        "borderColor": {"type": "string"},
        "borderWidth": {"type": "number", "minimum": 0},
        "borderRadius": {"type": "number", "minimum": 0},
        "padding": {"type": "number", "minimum": 0},
        "paddingHorizontal": {"type": "number", "minimum": 0},
        "paddingVertical": {"type": "number", "minimum": 0},
        "gap": {"type": "number", "minimum": 0},
        "shadow": {"type": "string"},
        "elevation": {"type": "string"},
    },
    "additionalProperties": False,
    "default": {},
}

TEXT_STYLE_SCHEMA: Dict[str, Any] = {
    "$id": "https://yamui.spec/style-text.schema.json",
    "type": "object",
    "properties": {
        "fontFamily": {"type": "string"},
        "fontSize": {"type": "number", "minimum": 0},
        "fontWeight": {"type": "number", "minimum": 100, "maximum": 900},
        "letterSpacing": {"type": "number"},
        "lineHeight": {"type": "number", "minimum": 0},
        "textTransform": {"type": "string"},
        "textAlign": {
            "type": "string",
            "enum": ["left", "right", "center", "justify"],
        },
        "color": {"type": "string"},
    },
    "additionalProperties": False,
    "default": {},
}

SPACING_STYLE_SCHEMA: Dict[str, Any] = {
    "$id": "https://yamui.spec/style-spacing.schema.json",
    "type": "object",
    "properties": {
        "padding": {"type": "number", "minimum": 0},
        "paddingHorizontal": {"type": "number", "minimum": 0},
        "paddingVertical": {"type": "number", "minimum": 0},
        "paddingTop": {"type": "number", "minimum": 0},
        "paddingBottom": {"type": "number", "minimum": 0},
        "paddingLeft": {"type": "number", "minimum": 0},
        "paddingRight": {"type": "number", "minimum": 0},
        "margin": {"type": "number", "minimum": 0},
        "marginHorizontal": {"type": "number", "minimum": 0},
        "marginVertical": {"type": "number", "minimum": 0},
        "marginTop": {"type": "number", "minimum": 0},
        "marginBottom": {"type": "number", "minimum": 0},
        "marginLeft": {"type": "number", "minimum": 0},
        "marginRight": {"type": "number", "minimum": 0},
        "gap": {"type": "number", "minimum": 0},
        "rowGap": {"type": "number", "minimum": 0},
        "columnGap": {"type": "number", "minimum": 0},
    },
    "additionalProperties": False,
    "default": {},
}

SHADOW_STYLE_SCHEMA: Dict[str, Any] = {
    "$id": "https://yamui.spec/style-shadow.schema.json",
    "type": "object",
    "properties": {
        "offsetX": {"type": "number"},
        "offsetY": {"type": "number"},
        "blurRadius": {"type": "number", "minimum": 0},
        "spreadRadius": {"type": "number", "minimum": 0},
        "color": {"type": "string"},
        "opacity": {"type": "number", "minimum": 0, "maximum": 1},
        "inset": {"type": "boolean"},
        "elevation": {"type": "string"},
    },
    "additionalProperties": False,
    "default": {},
}

STYLE_TOKEN_SCHEMA: Dict[str, Any] = {
    "$id": "https://yamui.spec/style-token.schema.json",
    "type": "object",
    "required": ["name", "category", "value"],
    "properties": {
        "name": {"type": "string"},
        "category": {
            "type": "string",
            "enum": ["color", "surface", "text", "spacing", "shadow"],
        },
        "description": {"type": "string"},
        "value": {
            "oneOf": [
                {"$ref": "https://yamui.spec/style-color.schema.json"},
                {"$ref": "https://yamui.spec/style-surface.schema.json"},
                {"$ref": "https://yamui.spec/style-text.schema.json"},
                {"$ref": "https://yamui.spec/style-spacing.schema.json"},
                {"$ref": "https://yamui.spec/style-shadow.schema.json"},
            ],
            "default": {},
        },
        "tags": {
            "type": "array",
            "items": {"type": "string"},
            "default": [],
        },
        "metadata": {"type": "object", "default": {}},
    },
    "additionalProperties": False,
}

TRANSLATION_LOCALE_SCHEMA: Dict[str, Any] = {
    "$id": "https://yamui.spec/translation-locale.schema.json",
    "type": "object",
    "properties": {
        "label": {"type": "string"},
        "description": {"type": "string"},
        "entries": {
            "type": "object",
            "additionalProperties": {"type": "string"},
            "default": {},
        },
        "metadata": {"type": "object", "default": {}},
    },
    "additionalProperties": False,
}

PROJECT_SCHEMA: Dict[str, Any] = {
    "$id": "https://yamui.spec/project.schema.json",
    "$schema": "https://json-schema.org/draft/2020-12/schema",
    "type": "object",
    "properties": {
        "app": {"type": "object", "default": {}},
        "state": {"type": "object", "default": {}},
        "translations": {
            "type": "object",
            "additionalProperties": {"$ref": "https://yamui.spec/translation-locale.schema.json"},
            "default": {},
        },
        "styles": {
            "type": "object",
            "additionalProperties": {"$ref": "https://yamui.spec/style-token.schema.json"},
            "default": {},
        },
        "components": {
            "type": "object",
            "additionalProperties": {
                "type": "object",
                "properties": {
                    "description": {"type": "string"},
                    "props": {"type": "object", "default": {}},
                    "prop_schema": {
                        "type": "array",
                        "items": {
                            "type": "object",
                            "properties": {
                                "name": {"type": "string"},
                                "type": {
                                    "type": "string",
                                    "enum": [
                                        "string",
                                        "number",
                                        "boolean",
                                        "style",
                                        "asset",
                                        "component",
                                        "json",
                                    ],
                                },
                                "required": {"type": "boolean", "default": False},
                                "default": {},
                            },
                            "required": ["name", "type"],
                        },
                        "default": [],
                    },
                    "widgets": {
                        "type": "array",
                        "items": {"$ref": "https://yamui.spec/widget.schema.json"},
                    },
                },
                "required": ["widgets"],
            },
            "default": {},
        },
        "screens": {
            "type": "object",
            "additionalProperties": {
                "type": "object",
                "properties": {
                    "name": {"type": "string"},
                    "title": {"type": "string"},
                    "initial": {"type": "boolean", "default": False},
                    "metadata": {"type": "object", "default": {}},
                    "widgets": {
                        "type": "array",
                        "items": {"$ref": "https://yamui.spec/widget.schema.json"},
                    },
                },
                "required": ["name", "widgets"],
            },
            "default": {},
        },
    },
    "required": ["screens"],
    "additionalProperties": False,
    "$defs": {
        "widget": WIDGET_SCHEMA,
        "styleToken": STYLE_TOKEN_SCHEMA,
        "colorStyle": COLOR_STYLE_SCHEMA,
        "surfaceStyle": SURFACE_STYLE_SCHEMA,
        "textStyle": TEXT_STYLE_SCHEMA,
        "spacingStyle": SPACING_STYLE_SCHEMA,
        "shadowStyle": SHADOW_STYLE_SCHEMA,
        "translationLocale": TRANSLATION_LOCALE_SCHEMA,
    },
}

_VALIDATOR = Draft202012Validator(PROJECT_SCHEMA)
_TRANSLATION_EXPRESSION_PATTERN = re.compile(
    r"^(?:\{\{\s*)?t\(\s*[\"']([^\"'()]+)[\"']\s*(?:,[^)]*)?\)\s*(?:\}\})?$",
    re.IGNORECASE,
)


def _extract_translation_key(value: Any) -> str | None:
    if not isinstance(value, str):
        return None
    match = _TRANSLATION_EXPRESSION_PATTERN.match(value.strip())
    if not match:
        return None
    key = (match.group(1) or "").strip()
    return key or None


def _iter_widgets(widgets: Any, prefix: str):
    if not isinstance(widgets, list):
        return
    for index, widget in enumerate(widgets):
        if not isinstance(widget, dict):
            continue
        widget_path = f"{prefix}/{index}"
        yield widget_path, widget
        child_widgets = widget.get("widgets")
        if isinstance(child_widgets, list):
            yield from _iter_widgets(child_widgets, f"{widget_path}/widgets")


def _validate_semantic_rules(raw: Dict[str, Any]) -> List[ValidationIssue]:
    issues: List[ValidationIssue] = []
    app = raw.get("app") if isinstance(raw.get("app"), dict) else {}
    screens = raw.get("screens") if isinstance(raw.get("screens"), dict) else {}
    styles = raw.get("styles") if isinstance(raw.get("styles"), dict) else {}
    components = raw.get("components") if isinstance(raw.get("components"), dict) else {}
    translations = raw.get("translations") if isinstance(raw.get("translations"), dict) else {}

    # App settings cross-reference checks.
    initial_screen = app.get("initial_screen")
    if isinstance(initial_screen, str) and initial_screen.strip() and initial_screen not in screens:
        issues.append(
            ValidationIssue(
                path="/app/initial_screen",
                message=f"Initial screen '{initial_screen}' is not defined under screens",
                severity="warning",
            )
        )

    locale = app.get("locale")
    if isinstance(locale, str) and locale.strip() and locale not in translations:
        issues.append(
            ValidationIssue(
                path="/app/locale",
                message=f"Default locale '{locale}' is missing in translations",
                severity="warning",
            )
        )

    supported_locales = app.get("supported_locales")
    if isinstance(supported_locales, list):
        for idx, entry in enumerate(supported_locales):
            if not isinstance(entry, str) or not entry.strip():
                issues.append(
                    ValidationIssue(
                        path=f"/app/supported_locales/{idx}",
                        message="Supported locale values must be non-empty strings",
                        severity="warning",
                    )
                )
                continue
            if entry not in translations:
                issues.append(
                    ValidationIssue(
                        path=f"/app/supported_locales/{idx}",
                        message=f"Supported locale '{entry}' is missing in translations",
                        severity="warning",
                    )
                )

    translation_keys: set[str] = set()
    for locale_payload in translations.values():
        if not isinstance(locale_payload, dict):
            continue
        entries = locale_payload.get("entries")
        if not isinstance(entries, dict):
            continue
        for key in entries.keys():
            if isinstance(key, str) and key.strip():
                translation_keys.add(key)

    def validate_widget_tree(widgets: Any, base_path: str) -> None:
        for widget_path, widget in _iter_widgets(widgets, base_path):
            style_name = widget.get("style")
            if isinstance(style_name, str) and style_name.strip() and style_name not in styles:
                issues.append(
                    ValidationIssue(
                        path=f"{widget_path}/style",
                        message=f"Style '{style_name}' is not defined",
                        severity="warning",
                    )
                )

            if widget.get("type") == "component":
                props = widget.get("props")
                component_name = props.get("component") if isinstance(props, dict) else None
                if isinstance(component_name, str) and component_name.strip() and component_name not in components:
                    issues.append(
                        ValidationIssue(
                            path=f"{widget_path}/props/component",
                            message=f"Component '{component_name}' is not defined",
                            severity="warning",
                        )
                    )

            text_key = _extract_translation_key(widget.get("text"))
            if text_key and text_key not in translation_keys:
                issues.append(
                    ValidationIssue(
                        path=f"{widget_path}/text",
                        message=f"Translation key '{text_key}' is not defined",
                        severity="warning",
                    )
                )

            bindings = widget.get("bindings")
            if isinstance(bindings, dict):
                for binding_name, binding_value in bindings.items():
                    key = _extract_translation_key(binding_value)
                    if key and key not in translation_keys:
                        issues.append(
                            ValidationIssue(
                                path=f"{widget_path}/bindings/{binding_name}",
                                message=f"Translation key '{key}' is not defined",
                                severity="warning",
                            )
                        )

    for screen_name, screen in screens.items():
        if not isinstance(screen, dict):
            continue
        validate_widget_tree(screen.get("widgets"), f"/screens/{screen_name}/widgets")

    for component_name, component in components.items():
        if not isinstance(component, dict):
            continue
        validate_widget_tree(component.get("widgets"), f"/components/{component_name}/widgets")

    return issues


def validate_project(raw: Dict[str, Any]) -> List[ValidationIssue]:
    """Validate a project dictionary and return structured issues."""

    issues: List[ValidationIssue] = []
    for error in _VALIDATOR.iter_errors(raw):
        issues.append(
            ValidationIssue(
                path="/" + "/".join(str(p) for p in error.absolute_path),
                message=error.message,
                severity="error" if error.validator == "required" else "warning",
            )
        )
    issues.extend(_validate_semantic_rules(raw))
    return issues
