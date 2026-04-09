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
        allowed_children=["label", "button", "img", "textarea", "switch", "slider", "checkbox", "dropdown", "roller", "bar", "arc", "chart", "calendar", "table", "tabview", "menu", "keyboard", "led", "component"],
    ),
    WidgetMetadata(
        type="column",
        category="layout",
        icon="mdi-view-column",
        description="Vertical layout container",
        accepts_children=True,
        allowed_children=["label", "button", "img", "textarea", "switch", "slider", "checkbox", "dropdown", "roller", "bar", "arc", "chart", "calendar", "table", "tabview", "menu", "keyboard", "led", "component"],
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
    WidgetMetadata(
        type="checkbox",
        category="ui",
        icon="mdi-checkbox-marked",
        description="Checkbox toggle with label",
    ),
    WidgetMetadata(
        type="dropdown",
        category="ui",
        icon="mdi-menu-down",
        description="Dropdown option selector",
    ),
    WidgetMetadata(
        type="roller",
        category="ui",
        icon="mdi-format-list-bulleted",
        description="Scrollable roller selector",
    ),
    WidgetMetadata(
        type="bar",
        category="ui",
        icon="mdi-progress-bar",
        description="Progress bar indicator",
    ),
    WidgetMetadata(
        type="arc",
        category="ui",
        icon="mdi-circle-outline",
        description="Circular arc/gauge indicator",
    ),
    WidgetMetadata(
        type="chart",
        category="ui",
        icon="mdi-chart-line",
        description="Line or bar chart visualization",
    ),
    WidgetMetadata(
        type="calendar",
        category="ui",
        icon="mdi-calendar",
        description="Calendar date picker",
    ),
    WidgetMetadata(
        type="table",
        category="ui",
        icon="mdi-table",
        description="Data table with rows and columns",
    ),
    WidgetMetadata(
        type="tabview",
        category="layout",
        icon="mdi-tab",
        description="Tabbed content container",
        accepts_children=True,
        allowed_children=["label", "button", "row", "column", "component"],
    ),
    WidgetMetadata(
        type="menu",
        category="layout",
        icon="mdi-menu",
        description="Hierarchical menu with pages",
        accepts_children=True,
        allowed_children=["label", "button", "row", "column", "component"],
    ),
    WidgetMetadata(
        type="keyboard",
        category="ui",
        icon="mdi-keyboard",
        description="On-screen keyboard overlay",
    ),
    WidgetMetadata(
        type="led",
        category="ui",
        icon="mdi-led-on",
        description="LED status indicator",
    ),
]


def extend_with_components(components: List[WidgetMetadata]) -> List[WidgetMetadata]:
    """Combine static palette entries with component instances."""

    return PALETTE + components
