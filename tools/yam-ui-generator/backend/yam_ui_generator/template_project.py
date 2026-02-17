"""Sample YamUI project used by the template endpoint."""

from __future__ import annotations

from .models import Project

TEMPLATE_PROJECT_DATA = {
    "app": {
        "name": "YamUI Sample Application",
        "version": "0.1.0",
        "author": "YamUI",
    },
    "state": {
        "welcome_message": "Welcome to YamUI",
        "device_status": {
            "connected": 4,
            "active": 3,
        },
    },
    "styles": {
        "card": {
            "name": "Card Surface",
            "category": "surface",
            "description": "Dark elevated card used for stats",
            "tags": ["panel", "stat"],
            "value": {
                "padding": 16,
                "borderRadius": 8,
                "backgroundColor": "#0f172a",
                "color": "#e2e8f0",
                "shadow": "md",
            },
        },
        "cta": {
            "name": "Primary CTA",
            "category": "surface",
            "description": "Button styling for primary actions",
            "value": {
                "paddingHorizontal": 24,
                "paddingVertical": 12,
                "backgroundColor": "#22d3ee",
                "color": "#0f172a",
                "borderRadius": 999,
            },
        },
        "heading": {
            "name": "Heading Text",
            "category": "text",
            "value": {
                "fontSize": 20,
                "fontWeight": 600,
                "color": "#94a3b8",
            },
        },
        "stat-label": {
            "name": "Stat Label",
            "category": "text",
            "value": {
                "fontSize": 14,
                "fontWeight": 500,
                "color": "#94a3b8",
                "textTransform": "uppercase",
            },
        },
        "stat-value": {
            "name": "Stat Value",
            "category": "text",
            "value": {
                "fontSize": 32,
                "fontWeight": 700,
                "color": "#f8fafc",
                "letterSpacing": -0.5,
            },
        },
    },
    "components": {
        "stat_card": {
            "description": "Compact statistic card with value and label.",
            "prop_schema": [
                {"name": "label", "type": "string", "required": True},
                {"name": "value", "type": "number", "required": True},
                {"name": "unit", "type": "string", "default": ""},
            ],
            "widgets": [
                {
                    "type": "column",
                    "id": "stat-card-root",
                    "style": "card",
                    "props": {"gap": 4},
                    "widgets": [
                        {
                            "type": "label",
                            "id": "stat-card-label",
                            "text": "{{ props.label }}",
                            "style": "stat-label",
                        },
                        {
                            "type": "label",
                            "id": "stat-card-value",
                            "text": "{{ props.value }}{{ props.unit }}",
                            "style": "stat-value",
                        },
                    ],
                }
            ],
        }
    },
    "screens": {
        "main": {
            "name": "main",
            "title": "Sample Dashboard",
            "initial": True,
            "metadata": {
                "layout": "column",
                "breakpoints": {
                    "mobile": 1,
                    "tablet": 2,
                },
            },
            "widgets": [
                {
                    "type": "column",
                    "id": "main-column",
                    "props": {"gap": 16, "padding": 24},
                    "widgets": [
                        {
                            "type": "label",
                            "id": "heading-label",
                            "text": "Device Overview",
                            "style": "heading",
                        },
                        {
                            "type": "row",
                            "id": "summary-row",
                            "props": {"gap": 12},
                            "widgets": [
                                {
                                    "type": "panel",
                                    "id": "panel-online",
                                    "props": {"title": "Online Devices"},
                                    "widgets": [
                                        {
                                            "type": "label",
                                            "id": "online-label",
                                            "text": "{{ state.device_status.active }} active",
                                        }
                                    ],
                                },
                                {
                                    "type": "panel",
                                    "id": "panel-total",
                                    "props": {"title": "Total Devices"},
                                    "widgets": [
                                        {
                                            "type": "label",
                                            "id": "total-label",
                                            "text": "{{ state.device_status.connected }} total",
                                        }
                                    ],
                                },
                            ],
                        },
                        {
                            "type": "panel",
                            "id": "panel-activity",
                            "props": {"title": "Recent Activity"},
                            "widgets": [
                                {
                                    "type": "list",
                                    "id": "activity-list",
                                    "widgets": [
                                        {
                                            "type": "label",
                                            "id": "activity-1",
                                            "text": "Camera connected",
                                        },
                                        {
                                            "type": "label",
                                            "id": "activity-2",
                                            "text": "Sensor calibrated",
                                        },
                                        {
                                            "type": "label",
                                            "id": "activity-3",
                                            "text": "Display refreshed",
                                        },
                                    ],
                                }
                            ],
                        },
                        {
                            "type": "button",
                            "id": "sync-button",
                            "text": "Trigger Sync",
                            "style": "cta",
                            "events": {"onPress": "sync_devices"},
                        },
                    ],
                }
            ],
        }
    },
}


def get_template_project() -> Project:
    """Return a hydrated Project instance for reuse across endpoints."""

    return Project.model_validate(TEMPLATE_PROJECT_DATA)
