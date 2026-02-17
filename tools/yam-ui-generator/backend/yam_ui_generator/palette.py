"""Widget palette metadata exposed to the frontend."""

from __future__ import annotations

from typing import List

from .models import WidgetMetadata

PALETTE: List[WidgetMetadata] = [
    WidgetMetadata(
        type="row",
        category="layout",
        icon="mdi-view-row",
        description="Horizontal layout container",
        accepts_children=True,
        allowed_children=["label", "button", "img", "textarea", "switch", "slider", "component"],
    ),
    WidgetMetadata(
        type="column",
        category="layout",
        icon="mdi-view-column",
        description="Vertical layout container",
        accepts_children=True,
        allowed_children=["label", "button", "img", "textarea", "switch", "slider", "component"],
    ),
    WidgetMetadata(
        type="panel",
        category="layout",
        icon="mdi-card-outline",
        description="Panel with optional header",
        accepts_children=True,
        allowed_children=["row", "column", "list", "component"],
    ),
    WidgetMetadata(
        type="spacer",
        category="layout",
        icon="mdi-dots-horizontal",
        description="Flexible spacer used inside layouts",
        accepts_children=False,
    ),
    WidgetMetadata(
        type="list",
        category="layout",
        icon="mdi-view-list",
        description="Scrollable list container",
        accepts_children=True,
        allowed_children=["row", "column", "component"],
    ),
    WidgetMetadata(
        type="label",
        category="ui",
        icon="mdi-format-text",
        description="Displays static or bound text",
    ),
    WidgetMetadata(
        type="button",
        category="ui",
        icon="mdi-gesture-tap",
        description="Interactive button with actions",
    ),
    WidgetMetadata(
        type="img",
        category="ui",
        icon="mdi-image",
        description="Bitmap or vector image",
    ),
    WidgetMetadata(
        type="textarea",
        category="ui",
        icon="mdi-form-textarea",
        description="Multi-line text entry",
    ),
    WidgetMetadata(
        type="switch",
        category="ui",
        icon="mdi-toggle-switch",
        description="Binary toggle switch",
    ),
    WidgetMetadata(
        type="slider",
        category="ui",
        icon="mdi-tune-variant",
        description="Analog slider",
    ),
]


def extend_with_components(components: List[WidgetMetadata]) -> List[WidgetMetadata]:
    """Combine static palette entries with component instances."""

    return PALETTE + components
