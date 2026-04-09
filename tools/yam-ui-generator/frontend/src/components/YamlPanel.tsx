import { useEffect, useRef, useState, type CSSProperties, type ReactNode } from "react";
import { dump as dumpYaml } from "js-yaml";
import { ProjectModel, ValidationIssue, WidgetNode } from "../types/yamui";
import { exportProject as exportProjectApi, renderPreviewContract } from "../utils/api";
import { useProject } from "../context/ProjectContext";
import { emitTelemetry } from "../utils/telemetry";
import { extractTranslationKey, getPrimaryLocale } from "../utils/translation";

interface Props {
  project: ProjectModel;
  issues: ValidationIssue[];
  onIssues: (issues: ValidationIssue[]) => void;
  style?: CSSProperties;
}

type PanelMode = "yaml" | "live";

type PreviewFinding = {
  path: string;
  message: string;
  severity: "error" | "warning";
  source: "local" | "backend";
};

const PREVIEW_REFRESH_DELAY_MS = 180;

const countWidgets = (project: ProjectModel): number => {
  let total = 0;
  const visit = (widgets?: WidgetNode[]) => {
    (widgets ?? []).forEach((widget) => {
      total += 1;
      visit(widget.widgets);
    });
  };
  Object.values(project.screens).forEach((screen) => visit(screen.widgets));
  Object.values(project.components).forEach((component) => visit(component.widgets));
  return total;
};

const summarizeProject = (project: ProjectModel) => ({
  screens: Object.keys(project.screens ?? {}).length,
  components: Object.keys(project.components ?? {}).length,
  styles: Object.keys(project.styles ?? {}).length,
  widgets: countWidgets(project),
});

const nowMs = (): number => (typeof performance !== "undefined" ? performance.now() : Date.now());

function toCssValue(value: unknown): string | number | undefined {
  if (typeof value === "string" || typeof value === "number") {
    return value;
  }
  return undefined;
}

function toCssStringValue(value: unknown): string | undefined {
  return typeof value === "string" ? value : undefined;
}

function styleFromToken(project: ProjectModel, tokenName?: string): CSSProperties {
  if (!tokenName) {
    return {};
  }
  const token = project.styles?.[tokenName];
  if (!token || typeof token.value !== "object" || token.value === null) {
    return {};
  }
  const source = token.value as Record<string, unknown>;
  return {
    backgroundColor: toCssStringValue(source.backgroundColor),
    color: toCssStringValue(source.color),
    fontSize: toCssValue(source.fontSize),
    fontWeight: toCssValue(source.fontWeight),
    borderRadius: toCssValue(source.borderRadius),
    borderColor: toCssStringValue(source.borderColor),
    borderWidth: toCssValue(source.borderWidth),
    borderStyle: toCssStringValue(source.borderStyle),
    padding: toCssValue(source.padding),
    gap: toCssValue(source.gap),
    width: toCssValue(source.width),
    height: toCssValue(source.height),
    minWidth: toCssValue(source.minWidth),
    minHeight: toCssValue(source.minHeight),
    maxWidth: toCssValue(source.maxWidth),
    maxHeight: toCssValue(source.maxHeight),
    opacity: typeof source.opacity === "number" ? source.opacity : undefined,
    letterSpacing: toCssValue(source.letterSpacing),
    lineHeight: toCssValue(source.lineHeight),
  };
}

function resolveWidgetText(widget: WidgetNode, project: ProjectModel): string {
  const raw = typeof widget.text === "string" ? widget.text : typeof widget.props?.text === "string" ? widget.props.text : "";
  const key = extractTranslationKey(raw);
  if (!key) {
    return raw;
  }
  const primaryLocale = getPrimaryLocale(project);
  if (!primaryLocale) {
    return raw;
  }
  return project.translations?.[primaryLocale]?.entries?.[key] ?? raw;
}

function previewNode(
  widget: WidgetNode,
  project: ProjectModel,
  findings: PreviewFinding[],
  path: string,
  key: string
): ReactNode {
  const baseStyle: CSSProperties = {
    ...styleFromToken(project, widget.style),
  };
  const children = (widget.widgets ?? []).map((child, index) =>
    previewNode(child, project, findings, `${path}/widgets/${index}`, `${key}-${index}`)
  );

  switch (widget.type) {
    case "row":
      return (
        <div key={key} className="live-preview__row" style={baseStyle}>
          {children}
        </div>
      );
    case "column":
    case "list":
      return (
        <div key={key} className="live-preview__column" style={baseStyle}>
          {children}
        </div>
      );
    case "panel":
      return (
        <section key={key} className="live-preview__panel" style={baseStyle}>
          {children}
        </section>
      );
    case "spacer": {
      const size = typeof widget.props?.size === "number" ? widget.props.size : 8;
      return <div key={key} style={{ ...baseStyle, minHeight: size }} aria-hidden="true" />;
    }
    case "label":
      return (
        <p key={key} className="live-preview__label" style={baseStyle}>
          {resolveWidgetText(widget, project) || "Label"}
        </p>
      );
    case "button":
      return (
        <button key={key} className="button secondary live-preview__button" type="button" style={baseStyle}>
          {resolveWidgetText(widget, project) || "Button"}
        </button>
      );
    case "img": {
      const src = typeof widget.src === "string" ? widget.src : "";
      return (
        <figure key={key} className="live-preview__image" style={baseStyle}>
          {src ? <img src={src} alt={widget.id ?? "preview image"} /> : <span>Image</span>}
        </figure>
      );
    }
    case "textarea":
      return <textarea key={key} className="text-field" readOnly value={resolveWidgetText(widget, project) || ""} style={baseStyle} />;
    case "switch":
      return (
        <label key={key} className="live-preview__switch" style={baseStyle}>
          <input type="checkbox" readOnly checked={Boolean(widget.props?.checked)} />
          <span>{resolveWidgetText(widget, project) || "Switch"}</span>
        </label>
      );
    case "slider":
      return (
        <input
          key={key}
          type="range"
          className="live-preview__slider"
          min={0}
          max={100}
          readOnly
          value={typeof widget.props?.value === "number" ? widget.props.value : 50}
          style={baseStyle}
        />
      );
    case "component": {
      const componentName =
        typeof widget.props?.component === "string"
          ? widget.props.component
          : typeof widget.props?.name === "string"
            ? widget.props.name
            : "";
      if (componentName && !project.components?.[componentName]) {
        findings.push({
          path,
          message: `Component "${componentName}" is not defined`,
          severity: "error",
          source: "local",
        });
      }
      return (
        <div key={key} className="live-preview__component" style={baseStyle}>
          <strong>{componentName || "Component"}</strong>
          {children.length > 0 ? children : <span className="field-hint">No child widgets</span>}
        </div>
      );
    }
    case "checkbox":
      return (
        <label key={key} className="live-preview__checkbox" style={baseStyle}>
          <input type="checkbox" readOnly checked={Boolean(widget.props?.checked)} />
          <span>{resolveWidgetText(widget, project) || "Checkbox"}</span>
        </label>
      );
    case "dropdown":
      return (
        <select key={key} className="live-preview__dropdown" style={baseStyle} disabled>
          {(typeof widget.props?.options === "string"
            ? widget.props.options.split("\n")
            : Array.isArray(widget.props?.options) ? widget.props.options : ["Option 1", "Option 2", "Option 3"]
          ).map((opt: string, i: number) => (
            <option key={i}>{opt}</option>
          ))}
        </select>
      );
    case "roller":
      return (
        <div key={key} className="live-preview__roller" style={{ ...baseStyle, border: "1px solid #ccc", borderRadius: 6, padding: "4px 8px", textAlign: "center" }}>
          {resolveWidgetText(widget, project) || "Roller"}
        </div>
      );
    case "bar":
      return (
        <div key={key} className="live-preview__bar" style={{ ...baseStyle, background: "#e0e0e0", borderRadius: 4, height: 12, overflow: "hidden" }}>
          <div style={{ width: `${typeof widget.props?.value === "number" ? widget.props.value : 50}%`, height: "100%", background: "#4fc3f7", borderRadius: 4 }} />
        </div>
      );
    case "arc":
      return (
        <div key={key} className="live-preview__arc" style={{ ...baseStyle, display: "flex", alignItems: "center", justifyContent: "center" }}>
          <svg width="60" height="60" viewBox="0 0 60 60">
            <circle cx="30" cy="30" r="24" fill="none" stroke="#e0e0e0" strokeWidth="6" />
            <circle cx="30" cy="30" r="24" fill="none" stroke="#4fc3f7" strokeWidth="6"
              strokeDasharray={`${((typeof widget.props?.value === "number" ? widget.props.value : 50) / 100) * 150.8} 150.8`}
              strokeLinecap="round" transform="rotate(-90 30 30)" />
            <text x="30" y="34" textAnchor="middle" fontSize="12" fill="#333">
              {typeof widget.props?.value === "number" ? widget.props.value : 50}%
            </text>
          </svg>
        </div>
      );
    case "keyboard":
      return (
        <div key={key} className="live-preview__keyboard" style={{ ...baseStyle, background: "#f0f0f0", borderRadius: 6, padding: 8, textAlign: "center", fontSize: 11, color: "#666" }}>
          ⌨ Keyboard
        </div>
      );
    case "led":
      return (
        <div key={key} className="live-preview__led" style={{ ...baseStyle, display: "inline-flex", alignItems: "center", gap: 6 }}>
          <span style={{ width: 14, height: 14, borderRadius: "50%", background: (widget.props?.color as string) ?? "#4caf50", display: "inline-block", boxShadow: `0 0 6px ${(widget.props?.color as string) ?? "#4caf50"}` }} />
          <span>{resolveWidgetText(widget, project) || "LED"}</span>
        </div>
      );
    case "chart":
      return (
        <div key={key} className="live-preview__chart" style={{ ...baseStyle, border: "1px solid #e0e0e0", borderRadius: 6, padding: 12, textAlign: "center", color: "#999" }}>
          📊 Chart
        </div>
      );
    case "calendar":
      return (
        <div key={key} className="live-preview__calendar" style={{ ...baseStyle, border: "1px solid #e0e0e0", borderRadius: 6, padding: 12, textAlign: "center", color: "#999" }}>
          📅 Calendar
        </div>
      );
    case "table":
      return (
        <div key={key} className="live-preview__table" style={{ ...baseStyle, border: "1px solid #e0e0e0", borderRadius: 6, padding: 12, textAlign: "center", color: "#999" }}>
          📋 Table
        </div>
      );
    case "tabview":
      return (
        <div key={key} className="live-preview__tabview" style={baseStyle}>
          <div style={{ display: "flex", borderBottom: "2px solid #e0e0e0", marginBottom: 4 }}>
            {(widget.widgets ?? []).map((tab, i) => (
              <span key={i} style={{ padding: "4px 12px", fontSize: 12, borderBottom: i === 0 ? "2px solid #4fc3f7" : "none", color: i === 0 ? "#4fc3f7" : "#999" }}>
                {(tab.props?.title as string) ?? tab.id ?? `Tab ${i + 1}`}
              </span>
            ))}
          </div>
          {children[0] ?? <span style={{ color: "#999", fontSize: 12 }}>Tab content</span>}
        </div>
      );
    case "menu":
      return (
        <div key={key} className="live-preview__menu" style={{ ...baseStyle, border: "1px solid #e0e0e0", borderRadius: 6 }}>
          {children.length > 0 ? children : <span style={{ padding: 8, display: "block", color: "#999", fontSize: 12 }}>Menu</span>}
        </div>
      );
    case "spinner":
      return (
        <div key={key} className="live-preview__spinner" style={{ ...baseStyle, display: "inline-flex", alignItems: "center", justifyContent: "center", width: 48, height: 48 }}>
          <svg width="32" height="32" viewBox="0 0 32 32" style={{ animation: "spin 1s linear infinite" }}>
            <circle cx="16" cy="16" r="12" fill="none" stroke="#e0e0e0" strokeWidth="3" />
            <circle cx="16" cy="16" r="12" fill="none" stroke="#4fc3f7" strokeWidth="3" strokeDasharray="50 26" strokeLinecap="round" />
          </svg>
        </div>
      );
    case "line":
      return (
        <div key={key} className="live-preview__line" style={{ ...baseStyle, padding: 4 }}>
          <svg width="100%" height="24" viewBox="0 0 200 24">
            <polyline points="0,20 60,4 140,20 200,4" fill="none" stroke="#999" strokeWidth="2" />
          </svg>
        </div>
      );
    case "qrcode":
      return (
        <div key={key} className="live-preview__qrcode" style={{ ...baseStyle, display: "inline-flex", flexDirection: "column", alignItems: "center", border: "1px solid #e0e0e0", borderRadius: 6, padding: 12, gap: 4 }}>
          <span style={{ fontSize: 28 }}>&#9641;</span>
          <span style={{ fontSize: 11, color: "#999" }}>QR Code</span>
        </div>
      );
    case "spinbox":
      return (
        <div key={key} className="live-preview__spinbox" style={{ ...baseStyle, display: "inline-flex", alignItems: "center", gap: 4 }}>
          <button style={{ width: 24, height: 24, fontSize: 14, border: "1px solid #ccc", borderRadius: 4, background: "#f5f5f5" }}>-</button>
          <span style={{ padding: "2px 8px", border: "1px solid #ccc", borderRadius: 4, fontFamily: "monospace", minWidth: 48, textAlign: "center" }}>
            {typeof widget.props?.value === "number" ? widget.props.value : 0}
          </span>
          <button style={{ width: 24, height: 24, fontSize: 14, border: "1px solid #ccc", borderRadius: 4, background: "#f5f5f5" }}>+</button>
        </div>
      );
    case "scale":
      return (
        <div key={key} className="live-preview__scale" style={{ ...baseStyle, border: "1px solid #e0e0e0", borderRadius: 6, padding: 12, textAlign: "center", color: "#999" }}>
          <svg width="100%" height="40" viewBox="0 0 200 40">
            {Array.from({ length: 11 }, (_, i) => {
              const x = 10 + i * 18;
              const isMajor = i % 5 === 0;
              return <line key={i} x1={x} y1={isMajor ? 4 : 12} x2={x} y2={28} stroke="#999" strokeWidth={isMajor ? 2 : 1} />;
            })}
            <line x1="10" y1="28" x2="190" y2="28" stroke="#ccc" strokeWidth="1" />
          </svg>
        </div>
      );
    case "buttonmatrix": {
      const map = Array.isArray(widget.props?.map) ? (widget.props.map as string[]) : ["Btn1", "Btn2", "Btn3"];
      const rows: string[][] = [[]];
      map.forEach((item) => {
        if (item === "\n") rows.push([]);
        else rows[rows.length - 1].push(item);
      });
      return (
        <div key={key} className="live-preview__buttonmatrix" style={{ ...baseStyle, display: "flex", flexDirection: "column", gap: 2 }}>
          {rows.map((row, ri) => (
            <div key={ri} style={{ display: "flex", gap: 2 }}>
              {row.map((label, ci) => (
                <button key={ci} style={{ flex: 1, padding: "4px 8px", fontSize: 11, border: "1px solid #ccc", borderRadius: 4, background: "#f5f5f5" }}>{label}</button>
              ))}
            </div>
          ))}
        </div>
      );
    }
    case "imagebutton":
      return (
        <div key={key} className="live-preview__imagebutton" style={{ ...baseStyle, display: "inline-flex", alignItems: "center", gap: 6, border: "1px solid #ccc", borderRadius: 6, padding: "6px 12px", background: "#fafafa" }}>
          <span style={{ fontSize: 16 }}>&#128444;</span>
          <span style={{ fontSize: 12, color: "#666" }}>Image Button</span>
        </div>
      );
    case "msgbox":
      return (
        <div key={key} className="live-preview__msgbox" style={{ ...baseStyle, border: "1px solid #e0e0e0", borderRadius: 10, boxShadow: "0 4px 16px rgba(0,0,0,0.1)", maxWidth: 280, background: "#fff" }}>
          <div style={{ padding: "10px 14px", borderBottom: "1px solid #eee", fontWeight: 600, fontSize: 13 }}>
            {(widget.props?.title as string) ?? "Alert"}
          </div>
          <div style={{ padding: "10px 14px", fontSize: 12, color: "#555" }}>
            {(widget.props?.content_text as string) ?? "Message content"}
          </div>
          <div style={{ padding: "8px 14px", display: "flex", gap: 6, justifyContent: "flex-end", borderTop: "1px solid #eee" }}>
            {(Array.isArray(widget.props?.buttons) ? (widget.props.buttons as string[]) : ["OK"]).map((btn, i) => (
              <button key={i} style={{ padding: "4px 12px", fontSize: 11, border: "1px solid #ccc", borderRadius: 4, background: "#f5f5f5" }}>{btn}</button>
            ))}
          </div>
        </div>
      );
    case "tileview":
      return (
        <div key={key} className="live-preview__tileview" style={{ ...baseStyle, border: "1px solid #e0e0e0", borderRadius: 6, overflow: "hidden" }}>
          <div style={{ padding: 4, fontSize: 10, color: "#999", background: "#f8f8f8", borderBottom: "1px solid #eee" }}>Tileview</div>
          {children.length > 0 ? children[0] : <div style={{ padding: 12, color: "#999", fontSize: 12 }}>Tile content</div>}
        </div>
      );
    case "win":
      return (
        <div key={key} className="live-preview__win" style={{ ...baseStyle, border: "1px solid #ccc", borderRadius: 8, overflow: "hidden" }}>
          <div style={{ padding: "6px 10px", background: "#f0f0f0", borderBottom: "1px solid #ddd", fontSize: 12, fontWeight: 600 }}>
            {(widget.props?.title as string) ?? "Window"}
          </div>
          <div style={{ padding: 8 }}>
            {children.length > 0 ? children : <span style={{ color: "#999", fontSize: 12 }}>Window content</span>}
          </div>
        </div>
      );
    case "span":
      return (
        <p key={key} className="live-preview__span" style={baseStyle}>
          {Array.isArray(widget.props?.spans)
            ? (widget.props.spans as Array<{ text?: string }>).map((s, i) => (
                <span key={i}>{s.text ?? ""}</span>
              ))
            : "Rich text span"}
        </p>
      );
    case "animimg":
      return (
        <div key={key} className="live-preview__animimg" style={{ ...baseStyle, display: "inline-flex", alignItems: "center", gap: 6, border: "1px solid #e0e0e0", borderRadius: 6, padding: "6px 12px" }}>
          <span style={{ fontSize: 16 }}>&#9654;</span>
          <span style={{ fontSize: 12, color: "#666" }}>Animated Image</span>
        </div>
      );
    default:
      findings.push({
        path,
        message: `Unsupported widget type "${(widget as { type?: string }).type ?? "unknown"}"`,
        severity: "warning",
        source: "local",
      });
      return (
        <div key={key} className="live-preview__unknown" style={baseStyle}>
          Unsupported widget
        </div>
      );
  }
}

function buildLivePreview(project: ProjectModel): { view: ReactNode; findings: PreviewFinding[] } {
  const findings: PreviewFinding[] = [];
  const screenEntries = Object.entries(project.screens ?? {});
  if (!screenEntries.length) {
    findings.push({ path: "/screens", message: "No screens available for preview", severity: "warning", source: "local" });
    return {
      findings,
      view: <p className="field-hint">No screens configured</p>,
    };
  }

  const initialName =
    screenEntries.find(([, screen]) => screen.initial)?.[0] ??
    (typeof project.app?.initial_screen === "string" ? (project.app.initial_screen as string) : "") ??
    screenEntries[0]![0];

  const selectedEntry = screenEntries.find(([name]) => name === initialName) ?? screenEntries[0]!;
  const [screenName, screen] = selectedEntry;

  const nodes = (screen.widgets ?? []).map((widget, index) =>
    previewNode(widget, project, findings, `/screens/${screenName}/widgets/${index}`, `${widget.id ?? widget.type}-${index}`)
  );

  return {
    findings,
    view: (
      <div className="live-preview__surface">
        <header className="live-preview__header">
          <strong>{screen.title || screen.name || "Screen"}</strong>
          <span className="field-hint">initial: {screenName}</span>
        </header>
        <div className="live-preview__content">{nodes.length ? nodes : <p className="field-hint">Screen has no widgets</p>}</div>
      </div>
    ),
  };
}

export default function YamlPanel({ project, issues, onIssues, style }: Props): JSX.Element {
  const [yamlText, setYamlText] = useState("screens: {}\n");
  const [status, setStatus] = useState<"pending" | "synced" | "error">("pending");
  const [mode, setMode] = useState<PanelMode>("yaml");
  const [previewStatus, setPreviewStatus] = useState<"refreshing" | "synced" | "error">("refreshing");
  const [previewUpdatedAt, setPreviewUpdatedAt] = useState<number | null>(null);
  const [previewFindings, setPreviewFindings] = useState<PreviewFinding[]>([]);
  const [previewView, setPreviewView] = useState<ReactNode>(null);
  const [previewErrorDetails, setPreviewErrorDetails] = useState<string | null>(null);
  const [previewRenderDurationMs, setPreviewRenderDurationMs] = useState<number | null>(null);
  const { lastExport, setLastExport } = useProject();
  const lastExportRef = useRef(lastExport);
  const telemetryReadyRef = useRef(false);

  useEffect(() => {
    lastExportRef.current = lastExport;
    if (lastExport) {
      setYamlText(lastExport.yaml);
      onIssues(lastExport.issues);
      setStatus("synced");
    }
  }, [lastExport, onIssues]);

  useEffect(() => {
    if (telemetryReadyRef.current) {
      return;
    }
    emitTelemetry("preview", "yaml_preview_panel_ready", summarizeProject(project));
    telemetryReadyRef.current = true;
  }, [project]);

  useEffect(() => {
    let cancelled = false;
    setStatus("pending");

    const sync = async () => {
      const stats = summarizeProject(project);
      const startedAt = nowMs();
      emitTelemetry("preview", "yaml_preview_sync_request", stats);
      try {
        const response = await exportProjectApi(project);
        emitTelemetry("preview", "yaml_preview_sync_success", {
          durationMs: Math.round(nowMs() - startedAt),
          issues: response.issues.length,
        });
        if (!cancelled) {
          setYamlText(response.yaml);
          onIssues(response.issues);
          setLastExport(response);
          setStatus("synced");
        }
      } catch (error) {
        const fallback = lastExportRef.current;
        emitTelemetry("preview", "yaml_preview_sync_error", {
          durationMs: Math.round(nowMs() - startedAt),
          message: error instanceof Error ? error.message : "Unknown error",
          fallbackUsed: Boolean(fallback),
        });
        if (!cancelled) {
          if (fallback) {
            setYamlText(fallback.yaml);
            onIssues(fallback.issues);
            setStatus("synced");
          } else {
            setYamlText(dumpYaml(project));
            setStatus("error");
          }
        }
      }
    };

    sync();

    return () => {
      cancelled = true;
    };
  }, [project, onIssues, setLastExport]);

  useEffect(() => {
    let cancelled = false;
    setPreviewStatus("refreshing");
    const timer = window.setTimeout(() => {
      void (async () => {
        const startedAt = nowMs();
        const stats = summarizeProject(project);
        try {
          const next = buildLivePreview(project);
          let combinedFindings: PreviewFinding[] = [...next.findings];
          let diagnostics: string | null = null;
          try {
            const response = await renderPreviewContract(project);
            const backendFindings: PreviewFinding[] = response.findings.map((issue) => ({
              path: issue.path,
              message: issue.message,
              severity: issue.severity,
              source: "backend",
            }));
            combinedFindings = [...combinedFindings, ...backendFindings];
          } catch (error) {
            const message = error instanceof Error ? error.message : "Unknown backend preview validation error";
            diagnostics = message;
            combinedFindings.push({
              path: "/preview/render",
              message: "Backend preview validation unavailable",
              severity: "warning",
              source: "backend",
            });
          }
          const durationMs = Math.round(nowMs() - startedAt);
          const uniqueFindings = Array.from(
            new Map(combinedFindings.map((finding) => [`${finding.path}-${finding.severity}-${finding.message}`, finding])).values()
          );
          if (cancelled) {
            return;
          }
          setPreviewView(next.view);
          setPreviewFindings(uniqueFindings);
          setPreviewUpdatedAt(Date.now());
          setPreviewRenderDurationMs(durationMs);
          setPreviewErrorDetails(diagnostics);
          const nextStatus = uniqueFindings.length ? "error" : "synced";
          setPreviewStatus(nextStatus);
          emitTelemetry("preview", "live_preview_render_success", {
            durationMs,
            findings: uniqueFindings.length,
            localFindings: next.findings.length,
            widgets: stats.widgets,
            mode: nextStatus,
          });
        } catch (error) {
          const durationMs = Math.round(nowMs() - startedAt);
          const message = error instanceof Error ? error.message : "Unknown preview render error";
          const stack = error instanceof Error && error.stack ? error.stack : "";
          if (cancelled) {
            return;
          }
          setPreviewFindings([
            {
              path: "/preview",
              message,
              severity: "error",
              source: "local",
            },
          ]);
          setPreviewView(<p className="field-hint warning-text">Preview failed to render.</p>);
          setPreviewRenderDurationMs(durationMs);
          setPreviewErrorDetails(stack || message);
          setPreviewStatus("error");
          emitTelemetry("preview", "live_preview_render_error", {
            durationMs,
            message,
            widgets: stats.widgets,
          });
        }
      })();
    }, PREVIEW_REFRESH_DELAY_MS);

    return () => {
      cancelled = true;
      window.clearTimeout(timer);
    };
  }, [project]);

  return (
    <section className="panel yaml-panel" style={style}>
      <div className="yaml-panel__header">
        <p className="section-title">Preview</p>
        <div className="yaml-panel__tabs">
          <button
            type="button"
            className={`button secondary ${mode === "yaml" ? "is-active" : ""}`}
            onClick={() => setMode("yaml")}
          >
            YAML
          </button>
          <button
            type="button"
            className={`button secondary ${mode === "live" ? "is-active" : ""}`}
            onClick={() => setMode("live")}
          >
            Live
          </button>
        </div>
      </div>

      {mode === "yaml" ? (
        <>
          <div className="yaml-panel__status-row">
            <span className="field-hint">YAML sync status</span>
            <span style={{ fontSize: "0.8rem", color: status === "error" ? "#f43f5e" : "#94a3b8" }}>{status}</span>
          </div>
          <textarea className="yaml-textarea" value={yamlText} readOnly />
          <div className="issue-list" style={{ marginTop: 12 }}>
            {issues.map((issue) => (
              <div key={`${issue.path}-${issue.message}`} className={`issue-item ${issue.severity}`}>
                <strong>{issue.severity.toUpperCase()}</strong> {issue.message} <em>{issue.path}</em>
              </div>
            ))}
            {!issues.length && <p style={{ color: "#22c55e" }}>No validation issues</p>}
          </div>
        </>
      ) : (
        <>
          <div className="yaml-panel__status-row">
            <span className="field-hint">Live preview status</span>
            <span style={{ fontSize: "0.8rem", color: previewStatus === "error" ? "#f43f5e" : "#94a3b8" }}>
              {previewStatus}
            </span>
          </div>
          <div className="live-preview">{previewView}</div>
          <div className="yaml-panel__status-row">
            <span className="field-hint">Updated</span>
            <span className="field-hint">{previewUpdatedAt ? new Date(previewUpdatedAt).toLocaleTimeString() : "—"}</span>
          </div>
          <div className="yaml-panel__status-row">
            <span className="field-hint">Render time</span>
            <span className="field-hint">{previewRenderDurationMs !== null ? `${previewRenderDurationMs}ms` : "—"}</span>
          </div>
          <div className="issue-list" style={{ marginTop: 12 }}>
            {previewFindings.map((finding) => (
              <div key={`${finding.path}-${finding.severity}-${finding.source}-${finding.message}`} className={`issue-item ${finding.severity}`}>
                <strong>{finding.source === "backend" ? "PREVIEW API" : "PREVIEW"}</strong> {finding.message} <em>{finding.path}</em>
              </div>
            ))}
            {!previewFindings.length && <p style={{ color: "#22c55e" }}>Preview rendered without findings</p>}
          </div>
          {previewErrorDetails && (
            <details className="live-preview__diagnostics">
              <summary>Diagnostics</summary>
              <pre>{previewErrorDetails}</pre>
            </details>
          )}
        </>
      )}
    </section>
  );
}
