import { useCallback, useEffect, useMemo, useState } from "react";
import { EditorTarget, useProject } from "../context/ProjectContext";
import { ProjectModel, ValidationIssue, WidgetNode, WidgetPath } from "../types/yamui";
import { emitTelemetry } from "../utils/telemetry";

interface Props {
  issues: ValidationIssue[];
}

type IssueDestination =
  | { kind: "widget"; target: EditorTarget; widgetPath: WidgetPath | null }
  | { kind: "style"; styleName: string };

interface ActionableIssue {
  issue: ValidationIssue;
  destination: IssueDestination;
}

const severityRank: Record<ValidationIssue["severity"], number> = {
  error: 0,
  warning: 1,
};

const MAX_VISIBLE = 4;

const trimPathSegments = (path: string): string[] => {
  const trimmed = path.startsWith("/") ? path.slice(1) : path;
  return trimmed.split("/").filter(Boolean);
};

const parseIssueDestination = (path: string): IssueDestination | null => {
  const segments = trimPathSegments(path);
  if (!segments.length) {
    return null;
  }
  const scope = segments[0];
  if ((scope === "screens" || scope === "components") && segments[1]) {
    const target: EditorTarget = {
      type: scope === "screens" ? "screen" : "component",
      id: decodeURIComponent(segments[1]),
    };
    if (segments.length <= 2) {
      return { kind: "widget", target, widgetPath: null };
    }
    const widgetPath: WidgetPath = [];
    let index = 2;
    while (segments[index] === "widgets" && index + 1 < segments.length) {
      const numeric = Number(segments[index + 1]);
      if (Number.isNaN(numeric)) {
        break;
      }
      widgetPath.push(numeric);
      index += 2;
    }
    return { kind: "widget", target, widgetPath };
  }
  if (scope === "styles" && segments[1]) {
    return { kind: "style", styleName: decodeURIComponent(segments[1]) };
  }
  return null;
};

const getRootWidgets = (project: ProjectModel, target: EditorTarget): WidgetNode[] => {
  if (target.type === "screen") {
    return project.screens[target.id]?.widgets ?? [];
  }
  return project.components[target.id]?.widgets ?? [];
};

const findWidgetByPath = (project: ProjectModel, target: EditorTarget, path: WidgetPath | null): WidgetNode | undefined => {
  if (!path || !path.length) {
    return undefined;
  }
  let collection = getRootWidgets(project, target);
  let node: WidgetNode | undefined;
  for (const index of path) {
    node = collection[index];
    if (!node) {
      return undefined;
    }
    collection = node.widgets ?? [];
  }
  return node;
};

const describeIssueLocation = (entry: ActionableIssue, project: ProjectModel): string => {
  if (entry.destination.kind === "style") {
    return `Style • ${entry.destination.styleName}`;
  }
  const { target, widgetPath } = entry.destination;
  const base = target.type === "screen" ? `Screen • ${target.id}` : `Component • ${target.id}`;
  if (!widgetPath || widgetPath.length === 0) {
    return base;
  }
  const widget = findWidgetByPath(project, target, widgetPath);
  if (!widget) {
    return `${base} • widgets[${widgetPath.join("/")}]`;
  }
  const widgetLabelParts = [widget.type];
  if (widget.id) {
    widgetLabelParts.push(`#${widget.id}`);
  }
  return `${base} • ${widgetLabelParts.join(" ")}`;
};

export default function IssueAccelerators({ issues }: Props): JSX.Element {
  const { project, setEditorTarget, selectWidget, setStyleEditorSelection } = useProject();
  const [activeIndex, setActiveIndex] = useState(0);

  const actionableIssues = useMemo<ActionableIssue[]>(
    () =>
      issues
        .map((issue) => {
          const destination = parseIssueDestination(issue.path);
          if (!destination) {
            return null;
          }
          return { issue, destination };
        })
        .filter((value): value is ActionableIssue => Boolean(value))
        .sort((a, b) => severityRank[a.issue.severity] - severityRank[b.issue.severity]),
    [issues]
  );

  const nonActionableCount = issues.length - actionableIssues.length;

  const visibleEntries = useMemo<Array<{ entry: ActionableIssue; index: number }>>(() => {
    if (!actionableIssues.length) {
      return [] as Array<{ entry: ActionableIssue; index: number }>;
    }
    if (actionableIssues.length <= MAX_VISIBLE) {
      return actionableIssues.map((entry, index) => ({ entry, index }));
    }
    const seen = new Set<number>();
    const ordered: Array<{ entry: ActionableIssue; index: number }> = [];
    if (actionableIssues[activeIndex]) {
      ordered.push({ entry: actionableIssues[activeIndex], index: activeIndex });
      seen.add(activeIndex);
    }
    for (let i = 0; i < actionableIssues.length && ordered.length < MAX_VISIBLE; i += 1) {
      if (seen.has(i)) {
        continue;
      }
      ordered.push({ entry: actionableIssues[i], index: i });
      seen.add(i);
    }
    return ordered;
  }, [actionableIssues, activeIndex]);

  useEffect(() => {
    setActiveIndex(0);
  }, [issues]);

  const focusEntry = useCallback(
    (index: number) => {
      const entry = actionableIssues[index];
      if (!entry) {
        return;
      }
      if (entry.destination.kind === "style") {
        setStyleEditorSelection(entry.destination.styleName);
        if (typeof document !== "undefined") {
          document.getElementById("style-manager")?.scrollIntoView({ behavior: "smooth", block: "start" });
        }
        emitTelemetry("issues", "issue_jump_style", {
          severity: entry.issue.severity,
          path: entry.issue.path,
          style: entry.destination.styleName,
        });
        setActiveIndex(index);
        return;
      }
      const { target, widgetPath } = entry.destination;
      const exists = target.type === "screen"
        ? Boolean(project.screens[target.id])
        : Boolean(project.components[target.id]);
      if (!exists) {
        return;
      }
      setEditorTarget(target);
      selectWidget(widgetPath && widgetPath.length ? widgetPath : null);
      emitTelemetry("issues", "issue_jump_widget", {
        severity: entry.issue.severity,
        path: entry.issue.path,
        target: `${target.type}:${target.id}`,
        hasWidgetPath: Boolean(widgetPath && widgetPath.length),
      });
      setActiveIndex(index);
    },
    [actionableIssues, project.components, project.screens, selectWidget, setEditorTarget, setStyleEditorSelection]
  );

  const cycleIssues = useCallback(
    (delta: number) => {
      if (!actionableIssues.length) {
        return;
      }
      let nextIndex = activeIndex + delta;
      if (nextIndex < 0) {
        nextIndex = actionableIssues.length - 1;
      }
      if (nextIndex >= actionableIssues.length) {
        nextIndex = 0;
      }
      focusEntry(nextIndex);
      emitTelemetry("issues", "issue_cycle", { nextIndex, total: actionableIssues.length });
    },
    [actionableIssues.length, activeIndex, focusEntry]
  );

  useEffect(() => {
    const handleKeyDown = (event: KeyboardEvent) => {
      if (!event.altKey || actionableIssues.length === 0) {
        return;
      }
      if (event.key === "ArrowRight") {
        event.preventDefault();
        cycleIssues(1);
      } else if (event.key === "ArrowLeft") {
        event.preventDefault();
        cycleIssues(-1);
      }
    };
    window.addEventListener("keydown", handleKeyDown);
    return () => window.removeEventListener("keydown", handleKeyDown);
  }, [actionableIssues.length, cycleIssues]);

  const errorCount = issues.filter((issue) => issue.severity === "error").length;
  const warningCount = issues.filter((issue) => issue.severity === "warning").length;

  return (
    <section className="panel issue-accelerators">
      <div className="issue-accelerators__header">
        <div>
          <p className="section-title" style={{ marginBottom: 4 }}>
            Issue Accelerators
          </p>
          <span className="issue-accelerators__hint">Alt + ←/→ jumps between actionable issues.</span>
        </div>
        <div className="issue-accelerators__counts">
          <span className={`issue-count error ${errorCount ? "" : "is-muted"}`}>{errorCount} error{errorCount === 1 ? "" : "s"}</span>
          <span className={`issue-count warning ${warningCount ? "" : "is-muted"}`}>{warningCount} warning{warningCount === 1 ? "" : "s"}</span>
        </div>
      </div>
      {issues.length === 0 && <p className="issue-accelerators__empty">Validation is clean. Keep building!</p>}
      {issues.length > 0 && visibleEntries.length === 0 && (
        <p className="issue-accelerators__empty">No actionable issues for the canvas. Check the YAML panel for project-level findings.</p>
      )}
      {visibleEntries.length > 0 && (
        <div className="issue-accelerators__list">
          {visibleEntries.map((item) => (
            <button
              key={`${item.entry.issue.path}-${item.entry.issue.message}`}
              type="button"
              className={`issue-accelerator severity-${item.entry.issue.severity} ${activeIndex === item.index ? "is-active" : ""}`}
              onClick={() => focusEntry(item.index)}
            >
              <div className="issue-accelerator__body">
                <strong>{item.entry.issue.message}</strong>
                <span className="issue-accelerator__location">{describeIssueLocation(item.entry, project)}</span>
              </div>
              <span className="issue-accelerator__cta">Focus</span>
            </button>
          ))}
        </div>
      )}
      {actionableIssues.length > MAX_VISIBLE && (
        <span className="issue-accelerators__hint">{actionableIssues.length - MAX_VISIBLE} more tracked in YAML preview.</span>
      )}
      {nonActionableCount > 0 && (
        <span className="issue-accelerators__hint">{nonActionableCount} issue{nonActionableCount === 1 ? "" : "s"} require YAML or style updates.</span>
      )}
    </section>
  );
}
