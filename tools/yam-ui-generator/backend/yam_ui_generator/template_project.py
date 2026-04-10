"""Sample YamUI project used by the template endpoint.

This mirrors the firmware's embedded showcase (components/ui_schemas/schemas/home.yml)
so the Web IDE opens with a feature-rich demo of every supported widget type.
"""

from __future__ import annotations

from .models import Project

TEMPLATE_PROJECT_DATA = {
    "app": {
        "name": "YamUI Device Showcase",
        "version": "0.1.0",
        "author": "YamUI",
        "initial_screen": "showcase",
        "locale": "en",
        "supported_locales": ["en", "es"],
    },
    "state": {
        "welcome_message": "status.ready",
        "ui": {
            "dark_mode": "false",
            "locale": "en",
            "keyboard_visible": "false",
            "alerts_enabled": "true",
            "temperature": "22",
            "humidity": "48",
            "pressure": "1012",
            "sync_count": "0",
        },
        "display": {
            "brightness": "72",
        },
        "profile": {
            "name": "Workshop Node",
            "note": "Ready for provisioning",
        },
        "network": {
            "ssid": "YamUI-Guest",
            "security": "WPA2",
            "auto_start": "true",
        },
        "demo": {
            "led_level": "180",
            "mode": "Balanced",
        },
    },
    "translations": {
        "en": {
            "label": "English",
            "entries": {
                "app.title": "YamUI Device Showcase",
                "app.subtitle": "A compact demo of the current on-device feature set",
                "cards.temperature": "Temperature",
                "cards.humidity": "Humidity",
                "cards.pressure": "Pressure",
                "panel.controls": "Interactive Controls",
                "panel.profile": "Profile",
                "panel.activity": "Recent Activity",
                "actions.sync": "Trigger Sync",
                "actions.reset": "Reset Demo",
                "actions.details": "Open Details",
                "actions.modal": "Open Modal",
                "actions.english": "English",
                "actions.spanish": "Espanol",
                "actions.open_widgets": "Open Widgets",
                "actions.open_new_widgets": "New Widgets",
                "actions.open_grid": "Grid Demo",
                "actions.open_table": "Open Table",
                "actions.open_chart": "Open Chart",
                "actions.open_calendar": "Open Calendar",
                "actions.open_tabview": "Open Tabs",
                "actions.open_menu": "Open Menu",
                "labels.theme": "Dark Theme",
                "labels.auto_start": "Auto-start",
                "labels.alerts": "Enable Alerts",
                "labels.security": "Security",
                "labels.brightness": "Brightness",
                "labels.profile_name": "Node Name",
                "labels.profile_note": "Operator Note",
                "labels.keyboard": "Keyboard Input",
                "modal.title": "Start Routine",
                "modal.body": "This demonstrates modal composition, button actions, and safe overlay behavior.",
                "modal.close": "Close",
                "modal.start": "Start",
                "screen.details": "Device Details",
                "screen.widgets": "Widget Gallery",
                "screen.table": "Table Demo",
                "screen.chart": "Chart Demo",
                "screen.calendar": "Calendar Demo",
                "screen.tabview": "Tabview Demo",
                "screen.menu": "Menu Demo",
                "screen.new_widgets": "New Widgets",
                "screen.grid": "Grid Demo",
                "screen.back": "Back",
                "status.ready": "Ready for interaction",
            },
        },
        "es": {
            "label": "Espanol",
            "entries": {
                "app.title": "Demostracion del dispositivo YamUI",
                "app.subtitle": "Una vista compacta del conjunto actual de funciones en el dispositivo",
                "cards.temperature": "Temperatura",
                "cards.humidity": "Humedad",
                "cards.pressure": "Presion",
                "panel.controls": "Controles interactivos",
                "panel.profile": "Perfil",
                "actions.sync": "Iniciar sincronizacion",
                "actions.reset": "Restablecer demo",
                "actions.english": "Ingles",
                "actions.spanish": "Espanol",
                "labels.theme": "Tema oscuro",
                "labels.auto_start": "Inicio automatico",
                "labels.alerts": "Habilitar alertas",
                "labels.brightness": "Brillo",
                "screen.back": "Regresar",
                "status.ready": "Listo para interactuar",
            },
        },
    },
    "styles": {
        "light.card": {
            "name": "Card",
            "category": "surface",
            "description": "Dark elevated card for content sections",
            "tags": ["panel", "card"],
            "value": {
                "backgroundColor": "#0F172A",
                "color": "#E2E8F0",
                "borderRadius": 12,
                "padding": 16,
            },
        },
        "dark.card": {
            "name": "Card",
            "category": "surface",
            "description": "Dark elevated card for content sections",
            "tags": ["panel", "card"],
            "value": {
                "backgroundColor": "#0F172A",
                "color": "#E2E8F0",
                "borderRadius": 12,
                "padding": 16,
            },
        },
        "light.hero": {
            "name": "Hero Card",
            "category": "surface",
            "description": "Large hero section card",
            "tags": ["panel", "hero"],
            "value": {
                "backgroundColor": "#111827",
                "color": "#F8FAFC",
                "borderRadius": 16,
                "padding": 20,
            },
        },
        "dark.hero": {
            "name": "Hero Card",
            "category": "surface",
            "description": "Large hero section card",
            "tags": ["panel", "hero"],
            "value": {
                "backgroundColor": "#111827",
                "color": "#F8FAFC",
                "borderRadius": 16,
                "padding": 20,
            },
        },
        "light.cta": {
            "name": "CTA",
            "category": "surface",
            "description": "Call-to-action button style",
            "tags": ["button"],
            "value": {
                "backgroundColor": "#22D3EE",
                "color": "#0F172A",
                "borderRadius": 999,
                "paddingHorizontal": 24,
                "paddingVertical": 12,
            },
        },
        "dark.cta": {
            "name": "CTA",
            "category": "surface",
            "description": "Call-to-action button style",
            "tags": ["button"],
            "value": {
                "backgroundColor": "#22D3EE",
                "color": "#0F172A",
                "borderRadius": 999,
                "paddingHorizontal": 24,
                "paddingVertical": 12,
            },
        },
        "light.ghost": {
            "name": "Ghost",
            "category": "surface",
            "description": "Subtle ghost button",
            "tags": ["button"],
            "value": {
                "backgroundColor": "#1F2937",
                "color": "#E5E7EB",
                "borderRadius": 999,
                "paddingHorizontal": 18,
                "paddingVertical": 10,
            },
        },
        "dark.ghost": {
            "name": "Ghost",
            "category": "surface",
            "description": "Subtle ghost button",
            "tags": ["button"],
            "value": {
                "backgroundColor": "#1F2937",
                "color": "#E5E7EB",
                "borderRadius": 999,
                "paddingHorizontal": 18,
                "paddingVertical": 10,
            },
        },
        "light.heading": {
            "name": "Heading",
            "category": "text",
            "value": {
                "color": "#94A3B8",
                "fontSize": 20,
                "fontWeight": 600,
            },
        },
        "dark.heading": {
            "name": "Heading",
            "category": "text",
            "value": {
                "color": "#94A3B8",
                "fontSize": 20,
                "fontWeight": 600,
            },
        },
        "light.body": {
            "name": "Body",
            "category": "text",
            "value": {
                "color": "#CBD5E1",
                "fontSize": 14,
                "fontWeight": 400,
            },
        },
        "dark.body": {
            "name": "Body",
            "category": "text",
            "value": {
                "color": "#CBD5E1",
                "fontSize": 14,
                "fontWeight": 400,
            },
        },
        "light.stat-label": {
            "name": "Stat Label",
            "category": "text",
            "value": {
                "color": "#94A3B8",
                "fontSize": 14,
                "fontWeight": 500,
            },
        },
        "dark.stat-label": {
            "name": "Stat Label",
            "category": "text",
            "value": {
                "color": "#94A3B8",
                "fontSize": 14,
                "fontWeight": 500,
            },
        },
        "light.stat-value": {
            "name": "Stat Value",
            "category": "text",
            "value": {
                "color": "#F8FAFC",
                "fontSize": 32,
                "fontWeight": 700,
            },
        },
        "dark.stat-value": {
            "name": "Stat Value",
            "category": "text",
            "value": {
                "color": "#F8FAFC",
                "fontSize": 32,
                "fontWeight": 700,
            },
        },
    },
    "components": {
        "stat_card": {
            "description": "Compact metric card with label and value",
            "prop_schema": [
                {"name": "label_key", "type": "string", "required": True},
                {"name": "value", "type": "number", "required": True},
                {"name": "unit", "type": "string", "default": ""},
            ],
            "widgets": [
                {
                    "type": "column",
                    "id": "stat-card-root",
                    "style": "card",
                    "widgets": [
                        {
                            "type": "label",
                            "id": "stat-card-label",
                            "text": "{{label_key}}",
                            "style": "stat-label",
                        },
                        {
                            "type": "label",
                            "id": "stat-card-value",
                            "text": "{{value}}{{unit}}",
                            "style": "stat-value",
                        },
                    ],
                }
            ],
        },
        "routine_modal": {
            "description": "Modal dialog for starting routines",
            "widgets": [
                {
                    "type": "column",
                    "id": "modal-root",
                    "widgets": [
                        {"type": "label", "id": "modal-title", "text": "Start Routine", "style": "heading"},
                        {"type": "label", "id": "modal-body", "text": "This demonstrates modal composition.", "style": "body"},
                        {
                            "type": "row",
                            "id": "modal-actions",
                            "widgets": [
                                {"type": "button", "id": "modal-close", "text": "Close", "style": "ghost"},
                                {"type": "button", "id": "modal-start", "text": "Start", "style": "cta"},
                            ],
                        },
                    ],
                }
            ],
        },
    },
    "screens": {
        "showcase": {
            "name": "showcase",
            "title": "YamUI Showcase",
            "initial": True,
            "widgets": [
                {
                    "type": "column",
                    "id": "main-column",
                    "widgets": [
                        # Hero panel
                        {
                            "type": "panel",
                            "id": "hero-panel",
                            "style": "hero",
                            "widgets": [
                                {"type": "label", "id": "app-title", "text": "YamUI Device Showcase", "style": "heading"},
                                {"type": "label", "id": "app-subtitle", "text": "A compact demo of the current on-device feature set", "style": "body"},
                            ],
                        },
                        # Metrics row with stat cards
                        {
                            "type": "row",
                            "id": "metrics-row",
                            "widgets": [
                                {"type": "label", "id": "temp-label", "text": "Temperature: 22\u00b0", "style": "stat-value"},
                                {"type": "label", "id": "humid-label", "text": "Humidity: 48%", "style": "stat-value"},
                                {"type": "label", "id": "press-label", "text": "Pressure: 1012 hPa", "style": "stat-value"},
                            ],
                        },
                        # Controls panel
                        {
                            "type": "panel",
                            "id": "controls-panel",
                            "style": "card",
                            "widgets": [
                                {"type": "label", "id": "controls-heading", "text": "Interactive Controls", "style": "heading"},
                                {
                                    "type": "row",
                                    "id": "theme-row",
                                    "widgets": [
                                        {"type": "label", "id": "theme-label", "text": "Dark Theme", "style": "heading"},
                                        {"type": "switch", "id": "theme-switch"},
                                    ],
                                },
                                {
                                    "type": "row",
                                    "id": "lang-row",
                                    "widgets": [
                                        {"type": "button", "id": "btn-en", "text": "English", "style": "cta"},
                                        {"type": "button", "id": "btn-es", "text": "Espanol", "style": "cta"},
                                    ],
                                },
                                {
                                    "type": "row",
                                    "id": "toggle-row",
                                    "widgets": [
                                        {"type": "label", "id": "auto-label", "text": "Auto-start", "style": "heading"},
                                        {"type": "switch", "id": "auto-switch"},
                                        {"type": "checkbox", "id": "alerts-check", "text": "Enable Alerts"},
                                        {"type": "spacer", "id": "spacer-1"},
                                        {"type": "label", "id": "sec-label", "text": "Security", "style": "heading"},
                                        {"type": "dropdown", "id": "security-dropdown", "props": {"options": ["Open", "WPA2", "WPA3"]}},
                                    ],
                                },
                                {
                                    "type": "row",
                                    "id": "action-row",
                                    "widgets": [
                                        {"type": "button", "id": "sync-btn", "text": "Trigger Sync", "style": "cta"},
                                        {"type": "button", "id": "reset-btn", "text": "Reset Demo", "style": "ghost"},
                                        {"type": "button", "id": "details-btn", "text": "Open Details", "style": "ghost"},
                                        {"type": "button", "id": "modal-btn", "text": "Open Modal", "style": "ghost"},
                                    ],
                                },
                                {"type": "label", "id": "brightness-label", "text": "Brightness: 72", "style": "stat-label"},
                                {"type": "slider", "id": "brightness-slider", "props": {"min": 0, "max": 100}},
                                {"type": "bar", "id": "brightness-bar", "props": {"min": 0, "max": 100, "value": 72}},
                                {"type": "arc", "id": "brightness-arc", "props": {"min": 0, "max": 100, "value": 72}},
                            ],
                        },
                        # Profile panel
                        {
                            "type": "panel",
                            "id": "profile-panel",
                            "style": "card",
                            "widgets": [
                                {"type": "label", "id": "profile-heading", "text": "Profile", "style": "heading"},
                                {"type": "label", "id": "name-label", "text": "Node Name", "style": "stat-label"},
                                {"type": "textarea", "id": "profile-name", "text": "Workshop Node"},
                                {"type": "label", "id": "note-label", "text": "Operator Note", "style": "stat-label"},
                                {"type": "textarea", "id": "profile-note", "text": "Ready for provisioning"},
                            ],
                        },
                        # Navigation buttons
                        {
                            "type": "panel",
                            "id": "nav-panel",
                            "style": "card",
                            "widgets": [
                                {"type": "label", "id": "nav-heading", "text": "Widget Gallery", "style": "heading"},
                                {
                                    "type": "row",
                                    "id": "nav-row",
                                    "widgets": [
                                        {"type": "button", "id": "widgets-btn", "text": "Open Widgets", "style": "cta"},
                                        {"type": "button", "id": "table-btn", "text": "Open Table", "style": "ghost"},
                                        {"type": "button", "id": "chart-btn", "text": "Open Chart", "style": "ghost"},
                                    ],
                                },
                                {
                                    "type": "row",
                                    "id": "nav-row-2",
                                    "widgets": [
                                        {"type": "button", "id": "cal-btn", "text": "Open Calendar", "style": "ghost"},
                                        {"type": "button", "id": "tabs-btn", "text": "Open Tabs", "style": "ghost"},
                                        {"type": "button", "id": "menu-btn", "text": "Open Menu", "style": "ghost"},
                                    ],
                                },
                                {
                                    "type": "row",
                                    "id": "nav-row-3",
                                    "widgets": [
                                        {"type": "button", "id": "new-widgets-btn", "text": "New Widgets", "style": "cta", "on_click": "push(new_widgets)"},
                                        {"type": "button", "id": "grid-btn", "text": "Grid Demo", "style": "ghost", "on_click": "push(grid_demo)"},
                                    ],
                                },
                            ],
                        },
                        # Keyboard overlay
                        {"type": "keyboard", "id": "main-keyboard", "props": {"overlay": True, "target": "profile-note"}},
                    ],
                }
            ],
        },
        "widgets": {
            "name": "widgets",
            "title": "Widget Gallery",
            "widgets": [
                {
                    "type": "column",
                    "id": "widgets-column",
                    "widgets": [
                        {
                            "type": "panel",
                            "id": "widgets-hero",
                            "style": "hero",
                            "widgets": [
                                {"type": "label", "id": "widgets-title", "text": "Widget Gallery", "style": "heading"},
                                {"type": "label", "id": "widgets-desc", "text": "Lightweight LVGL widgets can be grouped here without living on the home screen.", "style": "body"},
                            ],
                        },
                        # Roller
                        {
                            "type": "panel",
                            "id": "roller-panel",
                            "style": "card",
                            "widgets": [
                                {"type": "label", "id": "roller-title", "text": "Roller", "style": "heading"},
                                {"type": "roller", "id": "mode-roller", "props": {"visible_row_count": 3, "options": ["Eco", "Balanced", "Boost", "Silent"]}},
                                {"type": "label", "id": "mode-label", "text": "Selected mode: Balanced", "style": "body"},
                            ],
                        },
                        # LED
                        {
                            "type": "panel",
                            "id": "led-panel",
                            "style": "card",
                            "widgets": [
                                {"type": "label", "id": "led-title", "text": "LED", "style": "heading"},
                                {
                                    "type": "row",
                                    "id": "led-row",
                                    "widgets": [
                                        {"type": "led", "id": "demo-led", "props": {"width": 26, "height": 26, "color": "#22d3ee"}},
                                        {"type": "label", "id": "led-label", "text": "LED level: 180", "style": "body"},
                                    ],
                                },
                                {"type": "slider", "id": "led-slider", "props": {"min": 0, "max": 255}},
                            ],
                        },
                        {"type": "button", "id": "widgets-back", "text": "Back", "style": "cta"},
                    ],
                }
            ],
        },
        "table_demo": {
            "name": "table_demo",
            "title": "Table Demo",
            "widgets": [
                {
                    "type": "panel",
                    "id": "table-hero",
                    "style": "hero",
                    "widgets": [
                        {"type": "label", "id": "table-title", "text": "Table Demo", "style": "heading"},
                        {"type": "table", "id": "demo-table", "props": {"column_widths": [150, 150], "rows": [
                            {"first": "Widget", "second": "State"},
                            {"first": "Roller", "second": "Balanced"},
                            {"first": "LED", "second": "180"},
                            {"first": "Camera", "second": "On demand"},
                        ]}},
                        {"type": "button", "id": "table-back", "text": "Back", "style": "cta"},
                    ],
                }
            ],
        },
        "chart_demo": {
            "name": "chart_demo",
            "title": "Chart Demo",
            "widgets": [
                {
                    "type": "panel",
                    "id": "chart-hero",
                    "style": "hero",
                    "widgets": [
                        {"type": "label", "id": "chart-title", "text": "Chart Demo", "style": "heading"},
                        {"type": "chart", "id": "demo-chart", "props": {
                            "chart_type": "line",
                            "point_count": 7,
                            "min": 0,
                            "max": 100,
                            "horizontal_dividers": 5,
                            "vertical_dividers": 7,
                            "series": [
                                {"color": "#22d3ee", "axis": "primary_y", "values": [18, 36, 52, 47, 61, 74, 68]},
                                {"color": "#f59e0b", "axis": "primary_y", "values": [10, 22, 34, 58, 49, 63, 81]},
                            ],
                        }},
                        {"type": "button", "id": "chart-back", "text": "Back", "style": "cta"},
                    ],
                }
            ],
        },
        "calendar_demo": {
            "name": "calendar_demo",
            "title": "Calendar Demo",
            "widgets": [
                {
                    "type": "panel",
                    "id": "cal-hero",
                    "style": "hero",
                    "widgets": [
                        {"type": "label", "id": "cal-title", "text": "Calendar Demo", "style": "heading"},
                        {"type": "calendar", "id": "demo-calendar", "props": {
                            "today": "2026-04-09",
                            "shown_month": "2026-04-01",
                            "highlighted_dates": ["2026-04-02", "2026-04-07", "2026-04-18", "2026-04-26"],
                        }},
                        {"type": "button", "id": "cal-back", "text": "Back", "style": "cta"},
                    ],
                }
            ],
        },
        "tabview_demo": {
            "name": "tabview_demo",
            "title": "Tabview Demo",
            "widgets": [
                {
                    "type": "panel",
                    "id": "tabview-hero",
                    "style": "hero",
                    "widgets": [
                        {"type": "label", "id": "tabview-title", "text": "Tabview Demo", "style": "heading"},
                        {"type": "tabview", "id": "demo-tabview", "props": {
                            "tab_bar_position": "top",
                            "tab_bar_size": 44,
                            "active_tab": 0,
                            "tabs": [
                                {"title": "Status", "widgets": [
                                    {"type": "label", "text": "Device mode: Balanced"},
                                    {"type": "label", "text": "LED level: 180"},
                                ]},
                                {"title": "Controls", "widgets": [
                                    {"type": "slider", "props": {"min": 0, "max": 255}},
                                    {"type": "checkbox", "text": "Enable Alerts"},
                                ]},
                                {"title": "Notes", "widgets": [
                                    {"type": "label", "text": "Tab content can host normal YamUI widgets."},
                                    {"type": "button", "text": "Trigger Sync", "style": "ghost"},
                                ]},
                            ],
                        }},
                        {"type": "button", "id": "tabview-back", "text": "Back", "style": "cta"},
                    ],
                }
            ],
        },
        "menu_demo": {
            "name": "menu_demo",
            "title": "Menu Demo",
            "widgets": [
                {
                    "type": "column",
                    "id": "menu-column",
                    "widgets": [
                        {"type": "label", "id": "menu-title", "text": "Menu Demo", "style": "heading"},
                        {"type": "menu", "id": "demo-menu", "props": {
                            "root_title": "Settings",
                            "header_mode": "top_fixed",
                            "items": [
                                {"title": "Connectivity", "page": {"title": "Connectivity", "widgets": [
                                    {"type": "label", "text": "SSID: YamUI-Guest"},
                                    {"type": "label", "text": "Security: WPA2"},
                                ]}},
                                {"title": "Display", "page": {"title": "Display", "widgets": [
                                    {"type": "label", "text": "Brightness: 72"},
                                    {"type": "label", "text": "Dark theme: false"},
                                ]}},
                                {"title": "Status", "page": {"title": "Status", "widgets": [
                                    {"type": "label", "text": "Sync Count: 0"},
                                    {"type": "label", "text": "Mode: Balanced"},
                                ]}},
                            ],
                        }},
                        {"type": "button", "id": "menu-back", "text": "Back", "style": "cta"},
                    ],
                }
            ],
        },
        "new_widgets": {
            "name": "new_widgets",
            "title": "New Widgets",
            "widgets": [
                {
                    "type": "column",
                    "id": "nw-column",
                    "widgets": [
                        {
                            "type": "panel",
                            "id": "nw-hero",
                            "style": "hero",
                            "widgets": [
                                {"type": "label", "id": "nw-title", "text": "New Widgets", "style": "heading"},
                                {"type": "label", "id": "nw-desc", "text": "Showcase of newly added LVGL widgets: spinner, line, QR code, spinbox, scale, button matrix, message box, span, and more.", "style": "body"},
                            ],
                        },
                        # Spinner
                        {
                            "type": "panel",
                            "id": "spinner-panel",
                            "style": "card",
                            "widgets": [
                                {"type": "label", "id": "spinner-title", "text": "Spinner", "style": "heading"},
                                {"type": "label", "id": "spinner-desc", "text": "Animated loading indicator", "style": "body"},
                                {
                                    "type": "row",
                                    "id": "spinner-row",
                                    "widgets": [
                                        {"type": "spinner", "id": "demo-spinner-1", "props": {"duration": 1000, "arc_sweep": 240}},
                                        {"type": "spinner", "id": "demo-spinner-2", "props": {"duration": 600, "arc_sweep": 300}},
                                        {"type": "spinner", "id": "demo-spinner-3", "props": {"duration": 2000, "arc_sweep": 180}},
                                    ],
                                },
                            ],
                        },
                        # Line
                        {
                            "type": "panel",
                            "id": "line-panel",
                            "style": "card",
                            "widgets": [
                                {"type": "label", "id": "line-title", "text": "Line", "style": "heading"},
                                {"type": "label", "id": "line-desc", "text": "Points connected by line segments", "style": "body"},
                                {"type": "line", "id": "demo-line", "props": {"points": [[0, 0], [40, 30], [80, 5], [120, 35], [160, 10], [200, 25]]}},
                            ],
                        },
                        # QR Code
                        {
                            "type": "panel",
                            "id": "qrcode-panel",
                            "style": "card",
                            "widgets": [
                                {"type": "label", "id": "qr-title", "text": "QR Code", "style": "heading"},
                                {"type": "label", "id": "qr-desc", "text": "Encode any text or URL as a QR code", "style": "body"},
                                {"type": "qrcode", "id": "demo-qr", "props": {"data": "https://github.com/epicgrowers/kc-touch", "size": 150}},
                            ],
                        },
                        # Spinbox
                        {
                            "type": "panel",
                            "id": "spinbox-panel",
                            "style": "card",
                            "widgets": [
                                {"type": "label", "id": "spinbox-title", "text": "Spinbox", "style": "heading"},
                                {"type": "label", "id": "spinbox-desc", "text": "Numeric input with digit selection", "style": "body"},
                                {"type": "spinbox", "id": "demo-spinbox", "props": {"value": 42, "min": -999, "max": 999, "step": 1, "digit_count": 4, "decimal_pos": 0}},
                            ],
                        },
                        # Scale
                        {
                            "type": "panel",
                            "id": "scale-panel",
                            "style": "card",
                            "widgets": [
                                {"type": "label", "id": "scale-title", "text": "Scale", "style": "heading"},
                                {"type": "label", "id": "scale-desc", "text": "Gauge scale with tick marks and labels", "style": "body"},
                                {"type": "scale", "id": "demo-scale", "props": {"mode": "horizontal", "range_min": 0, "range_max": 100, "tick_count": 21, "major_tick_every": 5, "label_show": True}},
                            ],
                        },
                        # Button Matrix
                        {
                            "type": "panel",
                            "id": "btnm-panel",
                            "style": "card",
                            "widgets": [
                                {"type": "label", "id": "btnm-title", "text": "Button Matrix", "style": "heading"},
                                {"type": "label", "id": "btnm-desc", "text": "Grid of buttons from a string array", "style": "body"},
                                {"type": "buttonmatrix", "id": "demo-btnm", "props": {"map": ["1", "2", "3", "\n", "4", "5", "6", "\n", "7", "8", "9", "\n", "*", "0", "#"]}},
                            ],
                        },
                        # Message Box
                        {
                            "type": "panel",
                            "id": "msgbox-panel",
                            "style": "card",
                            "widgets": [
                                {"type": "label", "id": "msgbox-title", "text": "Message Box", "style": "heading"},
                                {"type": "label", "id": "msgbox-desc", "text": "Modal alert or confirm dialog", "style": "body"},
                                {"type": "msgbox", "id": "demo-msgbox", "props": {"title": "Confirm Action", "content_text": "Are you sure you want to proceed?", "close_button": True, "buttons": ["Cancel", "OK"]}},
                            ],
                        },
                        # Span (Rich Text)
                        {
                            "type": "panel",
                            "id": "span-panel",
                            "style": "card",
                            "widgets": [
                                {"type": "label", "id": "span-title", "text": "Span (Rich Text)", "style": "heading"},
                                {"type": "label", "id": "span-desc", "text": "Mixed-style text in a single widget", "style": "body"},
                                {"type": "span", "id": "demo-span", "props": {"mode": "break", "spans": [{"text": "Temperature: "}, {"text": "22C ", "style": "stat-value"}, {"text": "Humidity: "}, {"text": "48%", "style": "stat-value"}]}},
                            ],
                        },
                        {"type": "button", "id": "nw-back", "text": "Back", "style": "cta", "on_click": "pop()"},
                    ],
                }
            ],
        },
        "grid_demo": {
            "name": "grid_demo",
            "title": "Grid Demo",
            "widgets": [
                {
                    "type": "column",
                    "id": "grid-column",
                    "widgets": [
                        {
                            "type": "panel",
                            "id": "grid-hero",
                            "style": "hero",
                            "widgets": [
                                {"type": "label", "id": "grid-title", "text": "Grid Layout", "style": "heading"},
                                {"type": "label", "id": "grid-desc", "text": "CSS Grid-style layout with fractional units, spanning, and alignment.", "style": "body"},
                            ],
                        },
                        # Grid: 3-column dashboard
                        {
                            "type": "panel",
                            "id": "grid-dashboard",
                            "style": "card",
                            "props": {},
                            "layout": {"type": "grid", "columns": ["1fr", "1fr", "1fr"], "rows": ["auto", "auto"], "column_gap": 8, "row_gap": 8},
                            "widgets": [
                                {"type": "label", "id": "g-temp", "text": "22 C", "style": "stat-value", "grid_cell": {"col": 0, "row": 0}},
                                {"type": "label", "id": "g-humid", "text": "48%", "style": "stat-value", "grid_cell": {"col": 1, "row": 0}},
                                {"type": "label", "id": "g-press", "text": "1012 hPa", "style": "stat-value", "grid_cell": {"col": 2, "row": 0}},
                                {"type": "label", "id": "g-temp-l", "text": "Temperature", "style": "stat-label", "grid_cell": {"col": 0, "row": 1}},
                                {"type": "label", "id": "g-humid-l", "text": "Humidity", "style": "stat-label", "grid_cell": {"col": 1, "row": 1}},
                                {"type": "label", "id": "g-press-l", "text": "Pressure", "style": "stat-label", "grid_cell": {"col": 2, "row": 1}},
                            ],
                        },
                        # Grid: 2-column with spanning
                        {
                            "type": "panel",
                            "id": "grid-span-demo",
                            "style": "card",
                            "layout": {"type": "grid", "columns": ["1fr", "1fr"], "rows": ["auto", "auto", "auto"], "column_gap": 8, "row_gap": 8},
                            "widgets": [
                                {"type": "label", "id": "g-header", "text": "Status Overview", "style": "heading", "grid_cell": {"col": 0, "row": 0, "col_span": 2}},
                                {"type": "bar", "id": "g-bar", "props": {"min": 0, "max": 100, "value": 72}, "grid_cell": {"col": 0, "row": 1}},
                                {"type": "arc", "id": "g-arc", "props": {"min": 0, "max": 100, "value": 48}, "grid_cell": {"col": 1, "row": 1}},
                                {"type": "label", "id": "g-footer", "text": "All systems operational", "style": "body", "grid_cell": {"col": 0, "row": 2, "col_span": 2}},
                            ],
                        },
                        {"type": "button", "id": "grid-back", "text": "Back", "style": "cta", "on_click": "pop()"},
                    ],
                }
            ],
        },
    },
}


def get_template_project() -> Project:
    """Return a hydrated Project instance for reuse across endpoints."""

    return Project.model_validate(TEMPLATE_PROJECT_DATA)
