import { useEffect, useMemo, useState } from "react";
import { useProject } from "../context/ProjectContext";
import { WidgetMetadata, WidgetType } from "../types/yamui";
import { fetchPalette } from "../utils/api";

const FALLBACK_PALETTE: WidgetMetadata[] = [
  {
    type: "row",
    category: "layout",
    icon: "mdi-view-row",
    description: "Horizontal layout container",
    accepts_children: true,
    allowed_children: ["label", "button", "img", "textarea", "switch", "slider", "component"],
  },
  {
    type: "column",
    category: "layout",
    icon: "mdi-view-column",
    description: "Vertical layout container",
    accepts_children: true,
    allowed_children: ["label", "button", "img", "textarea", "switch", "slider", "component"],
  },
  {
    type: "panel",
    category: "layout",
    icon: "mdi-card-outline",
    description: "Panel with optional header",
    accepts_children: true,
    allowed_children: ["row", "column", "list", "component"],
  },
  {
    type: "spacer",
    category: "layout",
    icon: "mdi-dots-horizontal",
    description: "Flexible spacer used inside layouts",
    accepts_children: false,
    allowed_children: [],
  },
  {
    type: "list",
    category: "layout",
    icon: "mdi-view-list",
    description: "Scrollable list container",
    accepts_children: true,
    allowed_children: ["row", "column", "component"],
  },
  {
    type: "label",
    category: "ui",
    icon: "mdi-format-text",
    description: "Displays static or bound text",
    accepts_children: false,
    allowed_children: [],
  },
  {
    type: "button",
    category: "ui",
    icon: "mdi-gesture-tap",
    description: "Interactive button with actions",
    accepts_children: false,
    allowed_children: [],
  },
  {
    type: "img",
    category: "ui",
    icon: "mdi-image",
    description: "Bitmap or vector image",
    accepts_children: false,
    allowed_children: [],
  },
  {
    type: "textarea",
    category: "ui",
    icon: "mdi-form-textarea",
    description: "Multi-line text entry",
    accepts_children: false,
    allowed_children: [],
  },
  {
    type: "switch",
    category: "ui",
    icon: "mdi-toggle-switch",
    description: "Binary toggle switch",
    accepts_children: false,
    allowed_children: [],
  },
  {
    type: "slider",
    category: "ui",
    icon: "mdi-tune-variant",
    description: "Analog slider",
    accepts_children: false,
    allowed_children: [],
  },
];

export default function WidgetPalette(): JSX.Element {
  const [widgets, setWidgets] = useState<WidgetMetadata[]>(FALLBACK_PALETTE);
  const [query, setQuery] = useState("");
  const { project } = useProject();

  useEffect(() => {
    let cancelled = false;

    const hydratePalette = async () => {
      try {
        const palette = await fetchPalette();
        if (!cancelled) {
          setWidgets(palette.length ? palette : FALLBACK_PALETTE);
        }
      } catch {
        if (!cancelled) {
          setWidgets(FALLBACK_PALETTE);
        }
      }
    };

    hydratePalette();
    return () => {
      cancelled = true;
    };
  }, []);

  const componentEntries = useMemo<WidgetMetadata[]>(
    () =>
      Object.keys(project.components).map((name) => ({
        type: "component",
        category: "component",
        icon: "component",
        description: `Component ${name}`,
        accepts_children: false,
        allowed_children: [],
        componentName: name,
      })),
    [project.components]
  );

  const filtered = useMemo(() => {
    const palette = [...widgets, ...componentEntries];
    if (!query) {
      return palette;
    }
    return palette.filter((item) => {
      if (item.type.toLowerCase().includes(query.toLowerCase())) {
        return true;
      }
      if (item.componentName?.toLowerCase().includes(query.toLowerCase())) {
        return true;
      }
      return false;
    });
  }, [widgets, componentEntries, query]);

  const handleDragStart = (event: React.DragEvent<HTMLDivElement>, type: WidgetType, componentName?: string) => {
    event.dataTransfer.setData("application/x-widget-type", type);
    if (componentName) {
      event.dataTransfer.setData("application/x-component-name", componentName);
    }
    event.dataTransfer.effectAllowed = "copy";
  };

  return (
    <section>
      <p className="section-title">Widget Palette</p>
      <input
        className="input-field"
        placeholder="Search widgets"
        value={query}
        onChange={(event) => setQuery(event.target.value)}
      />
      <div style={{ marginTop: "12px", overflowY: "auto", maxHeight: "40vh" }}>
        {filtered.map((widget) => (
          <div
            key={widget.type === "component" ? `component-${widget.componentName}` : widget.type}
            className="widget-card"
            draggable
            onDragStart={(event) => handleDragStart(event, widget.type, widget.componentName)}
            title={widget.description}
          >
            <strong>{widget.type === "component" ? widget.componentName ?? "component" : widget.type}</strong>
            <p style={{ margin: "4px 0 0", fontSize: "0.8rem", color: "#4b5563" }}>{widget.description}</p>
          </div>
        ))}
        {!filtered.length && <p style={{ color: "#94a3b8" }}>No widgets match that filter.</p>}
      </div>
    </section>
  );
}
