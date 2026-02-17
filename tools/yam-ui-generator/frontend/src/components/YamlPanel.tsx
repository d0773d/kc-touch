import { useEffect, useRef, useState, type CSSProperties } from "react";
import { dump as dumpYaml } from "js-yaml";
import { ProjectModel, ValidationIssue, WidgetNode } from "../types/yamui";
import { exportProject as exportProjectApi } from "../utils/api";
import { useProject } from "../context/ProjectContext";
import { emitTelemetry } from "../utils/telemetry";

interface Props {
  project: ProjectModel;
  issues: ValidationIssue[];
  onIssues: (issues: ValidationIssue[]) => void;
  style?: CSSProperties;
}

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

export default function YamlPanel({ project, issues, onIssues, style }: Props): JSX.Element {
  const [yamlText, setYamlText] = useState("screens: {}\n");
  const [status, setStatus] = useState<"pending" | "synced" | "error">("pending");
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

  return (
    <section className="panel yaml-panel" style={style}>
      <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center" }}>
        <p className="section-title">YAML Preview</p>
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
    </section>
  );
}
