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
        "value": {"type": "object", "default": {}},
        "tags": {
            "type": "array",
            "items": {"type": "string"},
            "default": [],
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
