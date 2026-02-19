import { useEffect, useRef, useState, type MouseEvent as ReactMouseEvent } from "react";
import ProjectToolbar from "./components/ProjectToolbar";
import WidgetPalette from "./components/WidgetPalette";
import ScreenManager from "./components/ScreenManager";
import ComponentManager from "./components/ComponentManager";
import Canvas from "./components/Canvas";
import PropertyInspector from "./components/PropertyInspector";
import YamlPanel from "./components/YamlPanel";
import StyleManager from "./components/StyleManager";
import AssetManager from "./components/AssetManager";
import TranslationManager from "./components/TranslationManager";
import IssueAccelerators from "./components/IssueAccelerators";
import { useProject } from "./context/ProjectContext";
import { ValidationIssue } from "./types/yamui";

const MIN_PALETTE_WIDTH = 220;
const MIN_INSPECTOR_WIDTH = 260;
const MIN_CENTER_WIDTH = 360;
const MIN_YAML_HEIGHT = 200;

type ResizeHandle = "palette" | "inspector" | "yaml";

export default function App(): JSX.Element {
  const { project } = useProject();
  const [issues, setIssues] = useState<ValidationIssue[]>([]);
  const shellRef = useRef<HTMLDivElement>(null);
  const mainColumnRef = useRef<HTMLDivElement>(null);
  const [paletteWidth, setPaletteWidth] = useState(280);
  const [inspectorWidth, setInspectorWidth] = useState(320);
  const [yamlHeight, setYamlHeight] = useState(280);
  const [activeResize, setActiveResize] = useState<ResizeHandle | null>(null);

  useEffect(() => {
    if (!activeResize) {
      return undefined;
    }
    const handleMove = (event: MouseEvent) => {
      if (!shellRef.current) {
        return;
      }
      const shellRect = shellRef.current.getBoundingClientRect();
      if (activeResize === "palette") {
        const proposed = event.clientX - shellRect.left;
        const maxWidth = shellRect.width - inspectorWidth - MIN_CENTER_WIDTH - 32;
        const clamped = Math.max(MIN_PALETTE_WIDTH, Math.min(proposed, maxWidth));
        setPaletteWidth(clamped);
        return;
      }
      if (activeResize === "inspector") {
        const proposed = shellRect.right - event.clientX;
        const maxWidth = shellRect.width - paletteWidth - MIN_CENTER_WIDTH - 32;
        const clamped = Math.max(MIN_INSPECTOR_WIDTH, Math.min(proposed, maxWidth));
        setInspectorWidth(clamped);
        return;
      }
      if (activeResize === "yaml" && mainColumnRef.current) {
        const columnRect = mainColumnRef.current.getBoundingClientRect();
        const proposed = columnRect.bottom - event.clientY;
        const maxHeight = columnRect.height - 200;
        const clamped = Math.max(MIN_YAML_HEIGHT, Math.min(proposed, maxHeight));
        setYamlHeight(clamped);
      }
    };

    const stop = () => setActiveResize(null);
    window.addEventListener("mousemove", handleMove);
    window.addEventListener("mouseup", stop);
    return () => {
      window.removeEventListener("mousemove", handleMove);
      window.removeEventListener("mouseup", stop);
    };
  }, [activeResize, inspectorWidth, paletteWidth]);

  const startResize = (type: ResizeHandle) => (event: ReactMouseEvent) => {
    event.preventDefault();
    setActiveResize(type);
  };

  return (
    <div className="app-shell">
      <ProjectToolbar onIssues={setIssues} />
      <div className="app-body" ref={shellRef}>
        <section
          className="panel palette"
          style={{ width: paletteWidth, minWidth: MIN_PALETTE_WIDTH }}
        >
          <WidgetPalette />
          <ScreenManager />
          <ComponentManager />
          <StyleManager />
          <AssetManager />
          <TranslationManager issues={issues} />
        </section>
        <div
          className="resize-handle resize-handle--vertical"
          role="separator"
          aria-label="Resize widget palette"
          title="Drag to resize the palette"
          onMouseDown={startResize("palette")}
        />
        <div className="app-main" ref={mainColumnRef}>
          <IssueAccelerators issues={issues} />
          <Canvas issues={issues} style={{ flex: 1 }} />
          <div
            className="resize-handle resize-handle--horizontal"
            role="separator"
            aria-label="Resize YAML panel"
            title="Drag to resize YAML preview"
            onMouseDown={startResize("yaml")}
          />
          <YamlPanel
            project={project}
            issues={issues}
            onIssues={setIssues}
            style={{ height: yamlHeight, minHeight: MIN_YAML_HEIGHT }}
          />
        </div>
        <div
          className="resize-handle resize-handle--vertical"
          role="separator"
          aria-label="Resize inspector"
          title="Drag to resize the inspector"
          onMouseDown={startResize("inspector")}
        />
        <PropertyInspector
          issues={issues}
          style={{ width: inspectorWidth, minWidth: MIN_INSPECTOR_WIDTH }}
        />
      </div>
    </div>
  );
}
