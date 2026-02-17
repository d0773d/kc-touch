"""Utilities for reading and writing YamUI YAML bundles."""

from __future__ import annotations

from typing import Any, Dict

import yaml

from .models import Project


def project_from_yaml(text: str) -> Project:
    """Deserialize YAML into a :class:`Project`."""

    data = yaml.safe_load(text) or {}
    if not isinstance(data, dict):
        raise ValueError("YamUI YAML must contain a mapping at the top level")
    return Project.model_validate(data)


def project_to_yaml(project: Project) -> str:
    """Serialize a project into canonical YAML."""

    return yaml.safe_dump(
        project.model_dump(mode="json", exclude_none=True),
        sort_keys=False,
        allow_unicode=True,
    )


def ensure_project_dict(project: Project) -> Dict[str, Any]:
    """Return a plain dict that preserves ordering."""

    return project.model_dump(mode="json", exclude_none=True)
