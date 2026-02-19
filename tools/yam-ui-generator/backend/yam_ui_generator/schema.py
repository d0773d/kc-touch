"""JSON schema helpers for YamUI projects."""

from __future__ import annotations

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
    return issues
