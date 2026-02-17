"""Pydantic models for the Yam UI Generator backend."""

from __future__ import annotations

from typing import Any, Dict, List, Literal, Optional

from pydantic import BaseModel, Field


class Widget(BaseModel):
    """Represents a single widget node on the canvas."""

    type: str
    id: Optional[str] = None
    text: Optional[str] = None
    src: Optional[str] = None
    style: Optional[str] = None
    props: Dict[str, Any] = Field(default_factory=dict)
    events: Dict[str, Any] = Field(default_factory=dict)
    bindings: Dict[str, Any] = Field(default_factory=dict)
    accessibility: Dict[str, Any] = Field(default_factory=dict)
    widgets: List["Widget"] = Field(default_factory=list)


Widget.model_rebuild()


class Screen(BaseModel):
    """A YamUI screen, composed of widgets."""

    name: str
    title: Optional[str] = None
    widgets: List[Widget] = Field(default_factory=list)
    initial: bool = False
    metadata: Dict[str, Any] = Field(default_factory=dict)


class ComponentProp(BaseModel):
    """Structured property definition for a component."""

    name: str
    type: str = Field(default="string")
    required: bool = Field(default=False)
    default: Optional[Any] = None


class ComponentDefinition(BaseModel):
    """Reusable component that can be dragged onto screens."""

    description: Optional[str] = None
    props: Dict[str, Any] = Field(default_factory=dict)
    prop_schema: List[ComponentProp] = Field(default_factory=list)
    widgets: List[Widget] = Field(default_factory=list)


class StyleToken(BaseModel):
    """Structured style definition used across widgets/components."""

    name: str
    category: Literal["color", "surface", "text", "spacing", "shadow"] = Field(default="surface")
    description: Optional[str] = None
    value: Dict[str, Any] = Field(default_factory=dict)
    tags: List[str] = Field(default_factory=list)
    metadata: Dict[str, Any] = Field(default_factory=dict)


class Project(BaseModel):
    """Top-level YamUI project representation."""

    app: Dict[str, Any] = Field(default_factory=dict)
    state: Dict[str, Any] = Field(default_factory=dict)
    styles: Dict[str, StyleToken] = Field(default_factory=dict)
    components: Dict[str, ComponentDefinition] = Field(default_factory=dict)
    screens: Dict[str, Screen] = Field(default_factory=dict)

    @property
    def initial_screen(self) -> Optional[str]:
        for name, screen in self.screens.items():
            if screen.initial:
                return name
        return None


class ValidationIssue(BaseModel):
    """Structured validation issue returned by the API."""

    path: str
    message: str
    severity: str = Field(default="error")


class WidgetMetadata(BaseModel):
    """Metadata describing widgets exposed in the palette."""

    type: str
    category: str
    icon: str
    description: str
    accepts_children: bool = False
    allowed_children: List[str] = Field(default_factory=list)


class ProjectImportRequest(BaseModel):
    """Request payload for importing YAML."""

    yaml: str


class ProjectExportRequest(BaseModel):
    """Request payload for exporting YAML."""

    project: Project


class ProjectValidateRequest(BaseModel):
    """Request payload for validation."""

    project: Optional[Project] = None
    yaml: Optional[str] = None

    def ensure_payload(self) -> None:
        if not self.project and not self.yaml:
            msg = "Either project or yaml must be provided"
            raise ValueError(msg)


class ProjectImportResponse(BaseModel):
    project: Project
    issues: List[ValidationIssue] = Field(default_factory=list)


class ProjectExportResponse(BaseModel):
    yaml: str
    issues: List[ValidationIssue] = Field(default_factory=list)


class ProjectValidateResponse(BaseModel):
    valid: bool
    issues: List[ValidationIssue] = Field(default_factory=list)


class StylePreviewRequest(BaseModel):
    token: StyleToken
    widget: Optional[Widget] = None


class StylePreviewResponse(BaseModel):
    preview: Dict[str, Any]


class StyleLintRequest(BaseModel):
    tokens: Dict[str, StyleToken] = Field(default_factory=dict)


class StyleLintResponse(BaseModel):
    issues: List[ValidationIssue] = Field(default_factory=list)


class AssetReference(BaseModel):
    """Represents a unique asset discovered inside a project."""

    id: str
    path: str
    label: str
    extension: str
    kind: Literal["image", "video", "audio", "font", "binary", "unknown"] = Field(default="unknown")
    usage_count: int = Field(default=0)
    widget_ids: List[str] = Field(default_factory=list)
    targets: List[str] = Field(default_factory=list)
    tags: List[str] = Field(default_factory=list)
    size_bytes: Optional[int] = None
    metadata: Dict[str, Any] = Field(default_factory=dict)
    preview_url: Optional[str] = None
    thumbnail_url: Optional[str] = None
    download_url: Optional[str] = None


class AssetCatalogFilters(BaseModel):
    """Optional server-side filters applied to the asset catalog."""

    query: Optional[str] = None
    tags: List[str] = Field(default_factory=list)
    targets: List[str] = Field(default_factory=list)
    kinds: List[Literal["image", "video", "audio", "font", "binary", "unknown"]] = Field(default_factory=list)


class AssetCatalogRequest(BaseModel):
    project: Project
    filters: Optional[AssetCatalogFilters] = None


class AssetCatalogResponse(BaseModel):
    assets: List[AssetReference] = Field(default_factory=list)


class AssetTagUpdateRequest(BaseModel):
    """Update stored metadata for an asset without editing the project."""

    path: str
    tags: List[str] = Field(default_factory=list)
    project: Optional[Project] = None


class AssetTagUpdateResponse(BaseModel):
    asset: AssetReference


class AssetUploadResponse(BaseModel):
    asset: AssetReference
