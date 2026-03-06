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
    backgroundColor: toCssValue(source.backgroundColor),
    color: toCssValue(source.color),
    fontSize: toCssValue(source.fontSize),
    fontWeight: toCssValue(source.fontWeight),
    borderRadius: toCssValue(source.borderRadius),
    borderColor: toCssValue(source.borderColor),
    borderWidth: toCssValue(source.borderWidth),
    borderStyle: toCssValue(source.borderStyle),
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
          message: `Component \"${componentName}\" is not defined`, 
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
    default:
      findings.push({
        path,
        message: `Unsupported widget type \"${(widget as { type?: string }).type ?? "unknown"}\"`,
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
