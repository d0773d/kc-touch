import { useEffect, useRef, useState, type MouseEvent as ReactMouseEvent } from "react";
import ProjectToolbar from "./components/ProjectToolbar";
import WidgetPalette from "./components/WidgetPalette";
import ScreenManager from "./components/ScreenManager";
import ProjectSettings from "./components/ProjectSettings";
import ComponentManager from "./components/ComponentManager";
import Canvas from "./components/Canvas";
import PropertyInspector from "./components/PropertyInspector";
import YamlPanel from "./components/YamlPanel";
import StyleManager from "./components/StyleManager";
import AssetManager from "./components/AssetManager";
import TranslationManager from "./components/TranslationManager";
import IssueAccelerators from "./components/IssueAccelerators";
import DeviceConnection from "./components/DeviceConnection";
import Modal from "./components/Modal";
import { useProject } from "./context/ProjectContext";
import { ValidationIssue } from "./types/yamui";

const MIN_PALETTE_WIDTH = 220;
const MIN_INSPECTOR_WIDTH = 260;
const MIN_CENTER_WIDTH = 360;
const MIN_YAML_HEIGHT = 200;

type ResizeHandle = "palette" | "inspector" | "yaml";
type SidebarTab = "build" | "project";
type ProjectSection = "settings" | "assets" | "device" | null;
type ModalSection = "components" | "styles" | "translations" | null;

export default function App(): JSX.Element {
  const { project } = useProject();
  const [issues, setIssues] = useState<ValidationIssue[]>([]);
  const shellRef = useRef<HTMLDivElement>(null);
  const mainColumnRef = useRef<HTMLDivElement>(null);
  const [paletteWidth, setPaletteWidth] = useState(280);
  const [inspectorWidth, setInspectorWidth] = useState(320);
  const [yamlHeight, setYamlHeight] = useState(280);
  const [activeResize, setActiveResize] = useState<ResizeHandle | null>(null);
  const [sidebarTab, setSidebarTab] = useState<SidebarTab>("build");
  const [issuesExpanded, setIssuesExpanded] = useState(false);
  const [yamlExpanded, setYamlExpanded] = useState(false);
  const [openProjectSection, setOpenProjectSection] = useState<ProjectSection>("settings");
  const [modalSection, setModalSection] = useState<ModalSection>(null);

  const toggleSection = (section: ProjectSection) =>
    setOpenProjectSection((prev) => (prev === section ? null : section));

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

  const errorCount = issues.filter((i) => i.severity === "error").length;
  const warningCount = issues.filter((i) => i.severity === "warning").length;

  return (
    <div className="app-shell">
      <ProjectToolbar onIssues={setIssues} />
      <div className="app-body" ref={shellRef}>
        <section
          className="panel palette"
          style={{ width: paletteWidth, minWidth: MIN_PALETTE_WIDTH }}
        >
          <div className="sidebar-tabs">
            <button
              className={`sidebar-tab ${sidebarTab === "build" ? "sidebar-tab--active" : ""}`}
              onClick={() => setSidebarTab("build")}
            >
              Build
            </button>
            <button
              className={`sidebar-tab ${sidebarTab === "project" ? "sidebar-tab--active" : ""}`}
              onClick={() => setSidebarTab("project")}
            >
              Project
            </button>
          </div>
          {sidebarTab === "build" ? (
            <>
              <WidgetPalette />
              <ScreenManager />
            </>
          ) : (
            <div className="accordion-sidebar">
              <details open={openProjectSection === "settings"} onToggle={(e) => { if ((e.target as HTMLDetailsElement).open) toggleSection("settings"); }}>
                <summary className="accordion-header">Settings</summary>
                <div className="accordion-body"><ProjectSettings /></div>
              </details>
              <button className="accordion-launcher" onClick={() => setModalSection("components")}>
                Components
              </button>
              <button className="accordion-launcher" onClick={() => setModalSection("styles")}>
                Styles
              </button>
              <details open={openProjectSection === "assets"} onToggle={(e) => { if ((e.target as HTMLDetailsElement).open) toggleSection("assets"); }}>
                <summary className="accordion-header">Assets</summary>
                <div className="accordion-body"><AssetManager /></div>
              </details>
              <button className="accordion-launcher" onClick={() => setModalSection("translations")}>
                Translations
              </button>
              <details open={openProjectSection === "device"} onToggle={(e) => { if ((e.target as HTMLDetailsElement).open) toggleSection("device"); }}>
                <summary className="accordion-header">Device</summary>
                <div className="accordion-body"><DeviceConnection /></div>
              </details>
            </div>
          )}
        </section>
        <div
          className="resize-handle resize-handle--vertical"
          role="separator"
          aria-label="Resize widget palette"
          title="Drag to resize the palette"
          onMouseDown={startResize("palette")}
        />
        <div className="app-main" ref={mainColumnRef}>
          <div
            className={`issues-bar ${issuesExpanded ? "issues-bar--expanded" : ""}`}
            onClick={() => !issuesExpanded && setIssuesExpanded(true)}
          >
            <div className="issues-bar__summary">
              <span className="issues-bar__label">Issues</span>
              <span className={`issues-bar__badge ${errorCount > 0 ? "issues-bar__badge--error" : ""}`}>
                {errorCount} errors
              </span>
              <span className={`issues-bar__badge ${warningCount > 0 ? "issues-bar__badge--warning" : ""}`}>
                {warningCount} warnings
              </span>
              <button
                className="issues-bar__toggle"
                onClick={(e) => { e.stopPropagation(); setIssuesExpanded(!issuesExpanded); }}
                title={issuesExpanded ? "Collapse issues" : "Expand issues"}
              >
                {issuesExpanded ? "\u25B2" : "\u25BC"}
              </button>
            </div>
            {issuesExpanded && <IssueAccelerators issues={issues} />}
          </div>
          <Canvas issues={issues} style={{ flex: 1 }} />
          {yamlExpanded ? (
            <>
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
              <button
                className="yaml-collapse-btn"
                onClick={() => setYamlExpanded(false)}
                title="Collapse YAML panel"
              >
                Hide YAML \u25BC
              </button>
            </>
          ) : (
            <button
              className="yaml-expand-btn"
              onClick={() => setYamlExpanded(true)}
              title="Show YAML panel"
            >
              YAML / Preview \u25B2
            </button>
          )}
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
      {modalSection === "components" && (
        <Modal title="Components" onClose={() => setModalSection(null)} width="90vw">
          <ComponentManager />
        </Modal>
      )}
      {modalSection === "styles" && (
        <Modal title="Styles" onClose={() => setModalSection(null)} width="90vw">
          <StyleManager />
        </Modal>
      )}
      {modalSection === "translations" && (
        <Modal title="Translations" onClose={() => setModalSection(null)} width="90vw">
          <TranslationManager issues={issues} />
        </Modal>
      )}
    </div>
  );
}
