import { Fragment, useCallback, useEffect, useMemo, useRef, useState, type CSSProperties } from "react";
import { useProject } from "../context/ProjectContext";
import { ValidationIssue, WidgetNode, WidgetPath, WidgetType } from "../types/yamui";
import { buildTranslationExpression, extractTranslationKey, suggestTranslationKey } from "../utils/translation";

const CONTAINER_TYPES: WidgetType[] = ["row", "column", "panel", "list"];

interface CanvasProps {
  issues: ValidationIssue[];
  style?: CSSProperties;
}

export default function Canvas({ issues, style }: CanvasProps): JSX.Element {
  const {
    project,
    editorTarget,
    selectedPath,
    selectWidget,
    addWidget,
    removeWidget,
    moveWidget,
    styleEditorSelection,
    setStyleEditorSelection,
    requestTranslationBinding,
  } = useProject();
  const [activeDrop, setActiveDrop] = useState<string | null>(null);
  const [expandedState, setExpandedState] = useState<Record<string, boolean>>({});
  const [styleUsageIndex, setStyleUsageIndex] = useState(0);
  const widgetRefs = useRef<Map<string, HTMLDivElement | null>>(new Map());
  const translationKeySet = useMemo(() => {
    const set = new Set<string>();
    Object.values(project.translations ?? {}).forEach((locale) => {
      Object.keys(locale?.entries ?? {}).forEach((key) => set.add(key));
    });
    return set;
  }, [project.translations]);
  const focusStyleToken = (styleName: string) => {
    setStyleEditorSelection(styleName);
    const target = document.getElementById("style-manager");
    if (target) {
      target.scrollIntoView({ behavior: "smooth", block: "start" });
    }
  };

  const registerWidgetRef = useCallback((key: string, node: HTMLDivElement | null) => {
    if (node) {
      widgetRefs.current.set(key, node);
    } else {
      widgetRefs.current.delete(key);
    }
  }, []);

  const rootWidgets = useMemo(() => {
    if (editorTarget.type === "screen") {
      return project.screens[editorTarget.id]?.widgets ?? [];
    }
    return project.components[editorTarget.id]?.widgets ?? [];
  }, [project, editorTarget]);

  const basePath = useMemo(
    () => (editorTarget.type === "screen" ? `/screens/${editorTarget.id}/widgets` : `/components/${editorTarget.id}/widgets`),
    [editorTarget]
  );

  const pathToKey = (path: WidgetPath): string => (path.length ? path.join("-") : "root");

  const widgetPathToIssuePath = (path: WidgetPath): string => {
    if (!path.length) {
      return basePath;
    }
    const suffix = path.map((index) => `widgets/${index}`).join("/");
    return `${basePath}/${suffix}`;
  };

  const getWidgetIssues = (path: WidgetPath): ValidationIssue[] => {
    const issuePath = widgetPathToIssuePath(path);
    return issues.filter(
      (issue) => issue.path === issuePath || issue.path.startsWith(`${issuePath}/`)
    );
  };

  const getChildrenAtPath = (path: WidgetPath): WidgetNode[] => {
    if (path.length === 0) {
      return rootWidgets;
    }
    let collection = rootWidgets;
    for (const index of path) {
      const node = collection[index];
      if (!node) {
        return [];
      }
      collection = node.widgets ?? [];
    }
    return collection;
  };

  const getWidgetAtPath = useCallback((path: WidgetPath): WidgetNode | undefined => {
    if (path.length === 0) {
      return undefined;
    }
    let collection = rootWidgets;
    let node: WidgetNode | undefined;
    for (const index of path) {
      node = collection[index];
      if (!node) {
        return undefined;
      }
      collection = node.widgets ?? [];
    }
    return node;
  }, [rootWidgets]);

  const isAncestorPath = (ancestor: WidgetPath, target: WidgetPath): boolean => {
    if (ancestor.length === 0) {
      return false;
    }
    if (ancestor.length > target.length) {
      return false;
    }
    return ancestor.every((value, index) => target[index] === value);
  };

  const isExpanded = (id: string | undefined): boolean => {
    if (!id) {
      return true;
    }
    return expandedState[id] ?? true;
  };

  const toggleExpanded = (id: string | undefined) => {
    if (!id) {
      return;
    }
    setExpandedState((prev) => ({
      ...prev,
      [id]: !(prev[id] ?? true),
    }));
  };

  const ensurePathVisible = useCallback(
    (path: WidgetPath) => {
      if (path.length === 0) {
        return;
      }
      setExpandedState((prev) => {
        let changed = false;
        const nextState = { ...prev };
        for (let depth = 0; depth < path.length - 1; depth += 1) {
          const ancestorPath = path.slice(0, depth + 1);
          const ancestor = getWidgetAtPath(ancestorPath);
          const ancestorId = ancestor?.id;
          if (ancestorId && nextState[ancestorId] === false) {
            nextState[ancestorId] = true;
            changed = true;
          }
        }
        return changed ? nextState : prev;
      });
    },
    [getWidgetAtPath]
  );

  const styleUsageMatches = useMemo(() => {
    if (!styleEditorSelection) {
      return [] as Array<{ path: WidgetPath; widget: WidgetNode }>;
    }
    const matches: Array<{ path: WidgetPath; widget: WidgetNode }> = [];
    const visit = (widgets: WidgetNode[] | undefined, parentPath: WidgetPath) => {
      (widgets ?? []).forEach((widget, index) => {
        const currentPath = [...parentPath, index];
        if (widget.style === styleEditorSelection) {
          matches.push({ path: currentPath, widget });
        }
        if (widget.widgets && widget.widgets.length) {
          visit(widget.widgets, currentPath);
        }
      });
    };
    visit(rootWidgets, []);
    return matches;
  }, [rootWidgets, styleEditorSelection]);

  const selectionBreadcrumbs = useMemo(() => {
    if (!selectedPath || !selectedPath.length) {
      return [] as Array<{ label: string; path: WidgetPath }>;
    }
    const crumbs: Array<{ label: string; path: WidgetPath }> = [];
    for (let depth = 0; depth < selectedPath.length; depth += 1) {
      const currentPath = selectedPath.slice(0, depth + 1);
      const node = getWidgetAtPath(currentPath);
      if (!node) {
        break;
      }
      const label = node.id ? `${node.type} #${node.id}` : node.type;
      crumbs.push({ label, path: currentPath });
    }
    return crumbs;
  }, [getWidgetAtPath, selectedPath]);

  useEffect(() => {
    setStyleUsageIndex(0);
  }, [styleEditorSelection, styleUsageMatches.length]);

  const lastSelectedPathRef = useRef<WidgetPath | null>(null);

  useEffect(() => {
    if (!selectedPath) {
      lastSelectedPathRef.current = null;
      return;
    }
    if (lastSelectedPathRef.current === selectedPath) {
      return;
    }
    lastSelectedPathRef.current = selectedPath;
    ensurePathVisible(selectedPath);
    const key = pathToKey(selectedPath);
    const node = widgetRefs.current.get(key);
    if (!node) {
      return;
    }
    node.scrollIntoView({ block: "center", behavior: "smooth" });
    node.classList.add("widget-node--pulse");
    const timer = window.setTimeout(() => node.classList.remove("widget-node--pulse"), 600);
    return () => {
      window.clearTimeout(timer);
      node.classList.remove("widget-node--pulse");
    };
  }, [ensurePathVisible, selectedPath]);

  const focusStyleUsage = useCallback(
    (delta: number) => {
      if (!styleUsageMatches.length) {
        return;
      }
      let nextIndex = styleUsageIndex + delta;
      if (nextIndex < 0) {
        nextIndex = styleUsageMatches.length - 1;
      }
      if (nextIndex >= styleUsageMatches.length) {
        nextIndex = 0;
      }
      const match = styleUsageMatches[nextIndex];
      ensurePathVisible(match.path);
      selectWidget(match.path);
      setStyleUsageIndex(nextIndex);
    },
    [ensurePathVisible, selectWidget, styleUsageIndex, styleUsageMatches]
  );

  const jumpToFirstUsage = useCallback(() => {
    if (!styleUsageMatches.length) {
      return;
    }
    const match = styleUsageMatches[0];
    ensurePathVisible(match.path);
    selectWidget(match.path);
    setStyleUsageIndex(0);
  }, [ensurePathVisible, selectWidget, styleUsageMatches]);

  const handleDrop = (
    event: React.DragEvent<HTMLDivElement>,
    parentPath: WidgetPath = [],
    insertIndex?: number
  ) => {
    event.preventDefault();
    setActiveDrop(null);

    const widgetPathData = event.dataTransfer.getData("application/x-widget-path");
    const widgetType = event.dataTransfer.getData("application/x-widget-type");
    const effectiveParent = parentPath ?? [];
    const fallbackIndex =
      typeof insertIndex === "number" && insertIndex >= 0
        ? insertIndex
        : getChildrenAtPath(effectiveParent).length;

    if (widgetPathData) {
      try {
        const sourcePath = JSON.parse(widgetPathData) as WidgetPath;
        if (isAncestorPath(sourcePath, effectiveParent)) {
          return;
        }
        moveWidget(sourcePath, effectiveParent, fallbackIndex);
      } catch (error) {
        console.error("Failed to parse widget path", error);
      }
      return;
    }

    if (widgetType) {
      const componentName = event.dataTransfer.getData("application/x-component-name");
      const initial = widgetType === "component" && componentName
        ? { props: { component: componentName } }
        : undefined;
      addWidget(widgetType as WidgetType, effectiveParent, initial, fallbackIndex);
    }
  };

  const handleDragOver = (event: React.DragEvent<HTMLDivElement>, id: string) => {
    event.preventDefault();
    if (event.dataTransfer) {
      const allowed = event.dataTransfer.effectAllowed;
      const shouldCopy = allowed === "copy" || allowed === "copyLink" || allowed === "copyMove";
      event.dataTransfer.dropEffect = shouldCopy ? "copy" : "move";
    }
    setActiveDrop(id);
  };

  const handleWidgetDragStart = (event: React.DragEvent<HTMLDivElement>, path: WidgetPath) => {
    event.dataTransfer.setData("application/x-widget-path", JSON.stringify(path));
    event.dataTransfer.effectAllowed = "move";
  };

  const nudgeWidget = (path: WidgetPath, delta: number) => {
    const nextIndex = path[path.length - 1] + delta;
    if (nextIndex < 0) {
      return;
    }
    const parentPath = path.slice(0, -1);
    moveWidget(path, parentPath, nextIndex);
  };

  const handleConvertWidgetText = useCallback(
    (widget: WidgetNode, path: WidgetPath) => {
      if (!widget.text) {
        return;
      }
      const suggestion = suggestTranslationKey(widget.text, translationKeySet);
      selectWidget(path);
      requestTranslationBinding(path, { suggestedKey: suggestion });
    },
    [requestTranslationBinding, selectWidget, translationKeySet]
  );

  const handleCopyTranslationExpression = useCallback(async (event: React.MouseEvent, key: string) => {
    event.stopPropagation();
    try {
      await navigator.clipboard.writeText(buildTranslationExpression(key));
    } catch (error) {
      console.warn("Unable to copy translation reference", error);
    }
  }, []);

  const handleRevealTranslations = useCallback((event: React.MouseEvent) => {
    event.stopPropagation();
    if (typeof window === "undefined") {
      return;
    }
    const target = document.getElementById("translation-manager");
    target?.scrollIntoView({ behavior: "smooth", block: "start" });
  }, []);

  const renderDropZone = (parentPath: WidgetPath, index: number, large = false) => {
    const key = `${pathToKey(parentPath)}-${index}`;
    return (
      <div
        key={`drop-${key}`}
        className={`drop-zone ${large ? "drop-zone--large" : "drop-zone--inline"} ${activeDrop === key ? "active" : ""}`}
        onDragOver={(event) => handleDragOver(event, key)}
        onDragLeave={() => setActiveDrop((current) => (current === key ? null : current))}
        onDrop={(event) => handleDrop(event, parentPath, index)}
      >
        {large && <span>Drop widget here</span>}
      </div>
    );
  };

  const renderChildren = (children: WidgetNode[] | undefined, parentPath: WidgetPath) => {
    const list = children ?? [];
    if (list.length === 0) {
      return (
        <div className="widget-children widget-children--empty">
          {renderDropZone(parentPath, 0, true)}
          <p className="widget-empty-hint">Drop child widgets here</p>
        </div>
      );
    }
    return (
      <div className="widget-children">
        {list.map((child, index) => (
          <Fragment key={`${child.id ?? [...parentPath, index].join("-")}-branch`}>
            {renderDropZone(parentPath, index)}
            {renderWidget(child, [...parentPath, index])}
          </Fragment>
        ))}
        {renderDropZone(parentPath, list.length)}
      </div>
    );
  };

  const renderWidget = (widget: WidgetNode, path: WidgetPath) => {
    const widgetId = widget.id ?? path.join("-");
    const pathKey = pathToKey(path);
    const isSelected = selectedPath?.join("-") === path.join("-");
    const isSelectionAncestor = Boolean(
      selectedPath &&
        selectedPath.length > path.length &&
        path.every((value, index) => selectedPath[index] === value)
    );
    const canNest = CONTAINER_TYPES.includes(widget.type);
    const showChildren = canNest && isExpanded(widgetId);
    const widgetIssues = getWidgetIssues(path);
    const hasError = widgetIssues.some((issue) => issue.severity === "error");
    const hasWarning = !hasError && widgetIssues.some((issue) => issue.severity === "warning");
    const usesSelectedStyle = Boolean(styleEditorSelection && widget.style === styleEditorSelection);
    const nodeClasses = ["widget-node"];
    if (isSelected) {
      nodeClasses.push("selected");
    }
    if (hasError) {
      nodeClasses.push("has-error");
    } else if (hasWarning) {
      nodeClasses.push("has-warning");
    }
    if (usesSelectedStyle) {
      nodeClasses.push("uses-selected-style");
    }
    if (isSelectionAncestor) {
      nodeClasses.push("is-ancestor");
    }
    const translationKey = widget.text ? extractTranslationKey(widget.text) : null;

    return (
      <div
        key={widgetId}
        className={nodeClasses.join(" ")}
        draggable
        ref={(node) => registerWidgetRef(pathKey, node)}
        onDragStart={(event) => handleWidgetDragStart(event, path)}
        onClick={(event) => {
          event.stopPropagation();
          selectWidget(path);
        }}
      >
        <header className="widget-node__header">
          <div className="widget-node__title">
            {canNest && (
              <button
                type="button"
                className="toggle-children"
                aria-label={showChildren ? "Collapse children" : "Expand children"}
                onClick={(event) => {
                  event.stopPropagation();
                  toggleExpanded(widgetId);
                }}
              >
                {showChildren ? "−" : "+"}
              </button>
            )}
            <span className="drag-handle" aria-hidden="true">⋮⋮</span>
            <div>
              <strong>{widget.type}</strong>
              {widget.id && <span className="widget-node__id">{`#${widget.id}`}</span>}
            </div>
          </div>
          {usesSelectedStyle && widget.style && (
            <button
              type="button"
              className="widget-node__badge style-match style-match-button"
              onClick={(event) => {
                event.stopPropagation();
                selectWidget(path);
                focusStyleToken(widget.style!);
              }}
            >
              🎯 {widget.style}
            </button>
          )}
          {widgetIssues.length > 0 && (
            <span className={`widget-node__badge ${hasError ? "error" : "warning"}`}>
              {hasError ? "!" : "⚠"} {widgetIssues.length}
            </span>
          )}
          <div className="widget-node__actions">
            <button
              className="button secondary"
              onClick={(event) => {
                event.stopPropagation();
                nudgeWidget(path, -1);
              }}
            >
              ↑
            </button>
            <button
              className="button secondary"
              onClick={(event) => {
                event.stopPropagation();
                nudgeWidget(path, 1);
              }}
            >
              ↓
            </button>
            <button
              className="button secondary"
              onClick={(event) => {
                event.stopPropagation();
                removeWidget(path);
              }}
            >
              Delete
            </button>
          </div>
        </header>
        {widget.text && (
          <div className="widget-node__text-block">
            <p className="widget-node__text">{widget.text}</p>
            <div className="widget-node__translation">
              {translationKey ? (
                <>
                  <span className="widget-node__translation-label">
                    Key <code>{translationKey}</code>
                  </span>
                  <div className="widget-node__translation-actions">
                    <button type="button" className="button tertiary" onClick={(event) => handleCopyTranslationExpression(event, translationKey)}>
                      Copy
                    </button>
                    <button type="button" className="button tertiary" onClick={(event) => handleRevealTranslations(event)}>
                      Open translations
                    </button>
                  </div>
                </>
              ) : (
                <button
                  type="button"
                  className="button tertiary"
                  onClick={(event) => {
                    event.stopPropagation();
                    handleConvertWidgetText(widget, path);
                  }}
                >
                  Convert text to translation
                </button>
              )}
            </div>
          </div>
        )}
        {usesSelectedStyle && widget.style && (
          <p className="widget-style-note">Linked to style &ldquo;{widget.style}&rdquo;.</p>
        )}
        {canNest && showChildren && renderChildren(widget.widgets, path)}
      </div>
    );
  };

  const currentStyleUsage = styleUsageMatches[styleUsageIndex] ?? null;

  return (
    <section className="panel canvas" style={style}>
      <p className="section-title">Canvas</p>
      {selectionBreadcrumbs.length > 0 && (
        <div className="selection-breadcrumbs">
          <span className="selection-breadcrumbs__label">Selection</span>
          {selectionBreadcrumbs.map((crumb, index) => (
            <Fragment key={crumb.path.join("-")}>
              <button type="button" className="selection-breadcrumbs__chip" onClick={() => selectWidget(crumb.path)}>
                {crumb.label}
              </button>
              {index < selectionBreadcrumbs.length - 1 && <span className="selection-breadcrumbs__divider">›</span>}
            </Fragment>
          ))}
        </div>
      )}
      {styleEditorSelection && (
        <div className="style-usage-tracker">
          <div className="style-usage-tracker__meta">
            <span className="style-usage-tracker__label">Style focus</span>
            <strong>{styleEditorSelection}</strong>
            {styleUsageMatches.length ? (
              <button type="button" className="style-usage-tracker__count" onClick={jumpToFirstUsage}>
                {styleUsageIndex + 1}/{styleUsageMatches.length} matches
              </button>
            ) : (
              <span className="style-usage-tracker__count style-usage-tracker__count--static">Not used on this {editorTarget.type}</span>
            )}
            {currentStyleUsage?.widget?.id && <span className="style-usage-tracker__widget">#{currentStyleUsage.widget.id}</span>}
          </div>
          <div className="style-usage-tracker__nav">
            <button type="button" className="button tertiary" onClick={() => focusStyleUsage(-1)} disabled={!styleUsageMatches.length}>
              Previous
            </button>
            <button type="button" className="button tertiary" onClick={() => focusStyleUsage(1)} disabled={!styleUsageMatches.length}>
              Next
            </button>
          </div>
        </div>
      )}
      <div className="canvas-surface">
        {rootWidgets.length === 0 ? (
          <div className="canvas-empty">
            {renderDropZone([], 0, true)}
            <p className="widget-empty-hint">Drag widgets from the palette to get started.</p>
          </div>
        ) : (
          <div className="widget-tree">
            {rootWidgets.map((widget, index) => (
              <Fragment key={`root-${widget.id ?? index}`}>
                {renderDropZone([], index)}
                {renderWidget(widget, [index])}
              </Fragment>
            ))}
            {renderDropZone([], rootWidgets.length)}
          </div>
        )}
      </div>
    </section>
  );
}
