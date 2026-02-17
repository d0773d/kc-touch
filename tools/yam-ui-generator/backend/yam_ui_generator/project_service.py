"""High-level project operations used by the API layer."""

from __future__ import annotations

from typing import List, Tuple

from .models import Project, ValidationIssue
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
