import { createContext, ReactNode, useCallback, useContext, useEffect, useMemo, useState } from "react";
import { ProjectModel, StyleTokenModel, ValidationIssue, WidgetNode, WidgetPath } from "../types/yamui";
import { createWidget } from "../utils/widgetTemplates";
import { fetchTemplateProject } from "../utils/api";

export interface EditorTarget {
  type: "screen" | "component";
  id: string;
}

interface ProjectContextValue {
  project: ProjectModel;
  setProject: (next: ProjectModel) => void;
  editorTarget: EditorTarget;
  setEditorTarget: (target: EditorTarget) => void;
  selectedPath: WidgetPath | null;
  selectWidget: (path: WidgetPath | null) => void;
  addWidget: (
    type: WidgetNode["type"],
    parentPath?: WidgetPath,
    initial?: Partial<WidgetNode>,
    index?: number
  ) => void;
  updateWidget: (path: WidgetPath, partial: Partial<WidgetNode>) => void;
  removeWidget: (path: WidgetPath) => void;
  moveWidget: (from: WidgetPath, toParent: WidgetPath, index: number) => void;
  addScreen: (name?: string) => void;
  removeScreen: (name: string) => void;
  duplicateScreen: (name: string) => void;
  lastExport?: { yaml: string; issues: ValidationIssue[] };
  setLastExport: (payload: { yaml: string; issues: ValidationIssue[] }) => void;
  loadTemplateProject: () => Promise<void>;
  saveStyleToken: (token: StyleTokenModel, previousName?: string) => void;
  deleteStyleToken: (name: string) => void;
  styleEditorSelection: string | null;
  setStyleEditorSelection: (name: string | null) => void;
}

const DEFAULT_PROJECT: ProjectModel = {
  app: {},
  state: {},
  styles: {},
  components: {},
  screens: {
    main: {
      name: "main",
      title: "Main Screen",
      initial: true,
      widgets: [],
      metadata: {},
    },
  },
};

const DEFAULT_TARGET: EditorTarget = { type: "screen", id: "main" };

const ProjectContext = createContext<ProjectContextValue | undefined>(undefined);

function generateWidgetId(): string {
  if (typeof crypto !== "undefined" && "randomUUID" in crypto) {
    return `widget_${crypto.randomUUID().slice(0, 8)}`;
  }
  return `widget_${Math.random().toString(36).slice(2, 10)}`;
}

function normalizeProjectInPlace(project: ProjectModel): ProjectModel {
  const seen = new Set<string>();

  const normalizeList = (widgets?: WidgetNode[]) => {
    if (!widgets) {
      return;
    }
    widgets.forEach((widget) => {
      if (!widget.id || seen.has(widget.id)) {
        widget.id = generateWidgetId();
      }
      seen.add(widget.id);
      normalizeList(widget.widgets);
    });
  };

  Object.values(project.screens).forEach((screen) => normalizeList(screen.widgets));
  Object.values(project.components).forEach((component) => normalizeList(component.widgets));
  return project;
}

function pathsEqual(a: WidgetPath, b: WidgetPath): boolean {
  if (a.length !== b.length) {
    return false;
  }
  return a.every((value, index) => value === b[index]);
}

function deepClone<T>(value: T): T {
  if (typeof structuredClone === "function") {
    return structuredClone(value);
  }
  return JSON.parse(JSON.stringify(value)) as T;
}

function cloneProject(project: ProjectModel): ProjectModel {
  return deepClone(project);
}

function visitAllWidgets(project: ProjectModel, visitor: (widget: WidgetNode) => void): void {
  const visit = (widgets?: WidgetNode[]) => {
    if (!widgets) {
      return;
    }
    widgets.forEach((widget) => {
      visitor(widget);
      visit(widget.widgets);
    });
  };
  Object.values(project.screens).forEach((screen) => visit(screen.widgets));
  Object.values(project.components).forEach((component) => visit(component.widgets));
}

function renameStyleReferences(project: ProjectModel, from: string, to: string): void {
  if (from === to) {
    return;
  }
  visitAllWidgets(project, (widget) => {
    if (widget.style === from) {
      widget.style = to;
    }
  });
}

function clearStyleReferences(project: ProjectModel, styleName: string): void {
  visitAllWidgets(project, (widget) => {
    if (widget.style === styleName) {
      delete widget.style;
    }
  });
}

function getWidgetsRef(project: ProjectModel, target: EditorTarget): WidgetNode[] {
  if (target.type === "screen") {
    return project.screens[target.id]?.widgets ?? [];
  }
  return project.components[target.id]?.widgets ?? [];
}

function setWidgetsRef(project: ProjectModel, target: EditorTarget, widgets: WidgetNode[]): void {
  if (target.type === "screen") {
    if (!project.screens[target.id]) {
      throw new Error(`Screen ${target.id} not found`);
    }
    project.screens[target.id]!.widgets = widgets;
    return;
  }
  if (!project.components[target.id]) {
    throw new Error(`Component ${target.id} not found`);
  }
  project.components[target.id]!.widgets = widgets;
}

function accessByPath(root: WidgetNode[], path: WidgetPath): {
  collection: WidgetNode[];
  node?: WidgetNode;
} {
  if (path.length === 0) {
    return { collection: root };
  }
  let collection = root;
  let node: WidgetNode | undefined;
  for (let i = 0; i < path.length; i += 1) {
    const index = path[i];
    node = collection[index];
    if (!node) {
      return { collection, node: undefined };
    }
    if (i === path.length - 1) {
      return { collection, node };
    }
    if (!node.widgets) {
      node.widgets = [];
    }
    collection = node.widgets;
  }
  return { collection, node };
}

export function ProjectProvider({ children }: { children: ReactNode }): JSX.Element {
  const [project, setProjectState] = useState<ProjectModel>(() => normalizeProjectInPlace(cloneProject(DEFAULT_PROJECT)));
  const [editorTarget, setEditorTargetState] = useState<EditorTarget>(DEFAULT_TARGET);
  const [selectedPath, setSelectedPath] = useState<WidgetPath | null>(null);
  const [lastExport, setLastExportState] = useState<{ yaml: string; issues: ValidationIssue[] } | undefined>(undefined);
  const [styleEditorSelection, setStyleEditorSelection] = useState<string | null>(null);

  useEffect(() => {
    setStyleEditorSelection((current) => {
      if (current && project.styles[current]) {
        return current;
      }
      const fallback = Object.keys(project.styles)[0] ?? null;
      return fallback ?? null;
    });
  }, [project.styles, setStyleEditorSelection]);

  const setProject = useCallback((next: ProjectModel) => {
    const normalized = normalizeProjectInPlace(cloneProject(next));
    setProjectState(normalized);
    setEditorTargetState((current) => {
      if (current.type === "screen" && normalized.screens[current.id]) {
        return current;
      }
      if (current.type === "component" && normalized.components[current.id]) {
        return current;
      }
      const firstScreen = Object.keys(normalized.screens)[0];
      if (firstScreen) {
        return { type: "screen", id: firstScreen };
      }
      return current;
    });
  }, []);

  const selectWidget = useCallback((path: WidgetPath | null) => {
    setSelectedPath(path);
  }, []);

  const mutateWidgets = useCallback(
    (mutator: (widgets: WidgetNode[]) => WidgetNode[]) => {
      setProjectState((prev) => {
        const next = cloneProject(prev);
        const widgets = getWidgetsRef(next, editorTarget);
        const mutated = mutator(deepClone(widgets));
        setWidgetsRef(next, editorTarget, mutated);
        return normalizeProjectInPlace(next);
      });
    },
    [editorTarget]
  );

  const addWidget = useCallback(
    (type: WidgetNode["type"], parentPath?: WidgetPath, initial?: Partial<WidgetNode>, index?: number) => {
      mutateWidgets((widgets) => {
        const newWidget = { ...createWidget(type), ...initial };
        const targetIndex = typeof index === "number" && index >= 0 ? index : undefined;
        if (!parentPath || parentPath.length === 0) {
          const clone = [...widgets];
          clone.splice(targetIndex ?? clone.length, 0, newWidget);
          return clone;
        }
        const { node } = accessByPath(widgets, parentPath);
        if (!node) {
          return widgets;
        }
        const nextChildren = [...(node.widgets ?? [])];
        nextChildren.splice(targetIndex ?? nextChildren.length, 0, newWidget);
        node.widgets = nextChildren;
        return widgets;
      });
    },
    [mutateWidgets]
  );

  const updateWidget = useCallback(
    (path: WidgetPath, partial: Partial<WidgetNode>) => {
      mutateWidgets((widgets) => {
        const { node } = accessByPath(widgets, path);
        if (!node) {
          return widgets;
        }
        Object.assign(node, partial);
        return widgets;
      });
    },
    [mutateWidgets]
  );

  const removeWidget = useCallback(
    (path: WidgetPath) => {
      mutateWidgets((widgets) => {
        if (path.length === 0) {
          return widgets;
        }
        const parentPath = path.slice(0, -1);
        const targetIndex = path[path.length - 1];
        if (parentPath.length === 0) {
          return widgets.filter((_, index) => index !== targetIndex);
        }
        const { node } = accessByPath(widgets, parentPath);
        if (!node) {
          return widgets;
        }
        node.widgets = (node.widgets ?? []).filter((_, index) => index !== targetIndex);
        return widgets;
      });
      setSelectedPath(null);
    },
    [mutateWidgets]
  );

  const moveWidget = useCallback(
    (from: WidgetPath, toParent: WidgetPath, index: number) => {
      if (from.length === 0) {
        return;
      }
      mutateWidgets((widgets) => {
        const fromParentPath = from.slice(0, -1);
        const fromIndex = from[from.length - 1];
        const targetParentPath = toParent ?? [];

        const { collection: fromCollection } = accessByPath(widgets, fromParentPath);
        const [moved] = fromCollection.splice(fromIndex, 1);
        if (!moved) {
          return widgets;
        }

        const { collection: toCollection } = accessByPath(widgets, targetParentPath);
        let nextIndex = index;
        if (pathsEqual(fromParentPath, targetParentPath) && fromIndex < index) {
          nextIndex -= 1;
        }
        if (nextIndex < 0) {
          nextIndex = 0;
        }
        if (nextIndex > toCollection.length) {
          nextIndex = toCollection.length;
        }
        toCollection.splice(nextIndex, 0, moved);
        return widgets;
      });
    },
    [mutateWidgets]
  );

  const addScreen = useCallback((name?: string) => {
    setProjectState((prev) => {
      const next = cloneProject(prev);
      const screenName = name ?? `screen_${Object.keys(prev.screens).length + 1}`;
      next.screens[screenName] = {
        name: screenName,
        widgets: [],
        metadata: {},
        initial: Object.values(prev.screens).every((screen) => !screen.initial),
      };
      return normalizeProjectInPlace(next);
    });
  }, []);

  const removeScreen = useCallback((name: string) => {
    setProjectState((prev) => {
      if (Object.keys(prev.screens).length === 1) {
        return prev;
      }
      const next = cloneProject(prev);
      delete next.screens[name];
      if (editorTarget.type === "screen" && editorTarget.id === name) {
        const fallback = Object.keys(next.screens)[0];
        setEditorTargetState({ type: "screen", id: fallback });
      }
      return normalizeProjectInPlace(next);
    });
  }, [editorTarget.id, editorTarget.type]);

  const duplicateScreen = useCallback((name: string) => {
    setProjectState((prev) => {
      const source = prev.screens[name];
      if (!source) {
        return prev;
      }
      const next = cloneProject(prev);
      const cloneName = `${name}_copy`;
      next.screens[cloneName] = deepClone(source);
      next.screens[cloneName]!.name = cloneName;
      next.screens[cloneName]!.initial = false;
      return normalizeProjectInPlace(next);
    });
  }, []);

  const setLastExport = useCallback((payload: { yaml: string; issues: ValidationIssue[] }) => {
    setLastExportState(payload);
  }, []);

  const loadTemplateProject = useCallback(async () => {
    const template = await fetchTemplateProject();
    setProject(template);
    setLastExportState(undefined);
  }, [setProject, setLastExportState]);

  const saveStyleToken = useCallback(
    (token: StyleTokenModel, previousName?: string) => {
      setProjectState((prev) => {
        const next = cloneProject(prev);
        const currentKey = previousName && previousName in next.styles ? previousName : token.name;
        if (currentKey !== token.name) {
          renameStyleReferences(next, currentKey, token.name);
          delete next.styles[currentKey];
        }
        next.styles[token.name] = deepClone(token);
        setStyleEditorSelection(token.name);
        return normalizeProjectInPlace(next);
      });
    },
    [setStyleEditorSelection]
  );

  const deleteStyleToken = useCallback((name: string) => {
    setProjectState((prev) => {
      if (!prev.styles[name]) {
        return prev;
      }
      const next = cloneProject(prev);
      delete next.styles[name];
      clearStyleReferences(next, name);
      const fallback = Object.keys(next.styles)[0] ?? null;
      setStyleEditorSelection(fallback ?? null);
      return normalizeProjectInPlace(next);
    });
  }, [setStyleEditorSelection]);

  const value = useMemo<ProjectContextValue>(
    () => ({
      project,
      setProject,
      editorTarget,
      setEditorTarget: setEditorTargetState,
      selectedPath,
      selectWidget,
      addWidget,
      updateWidget,
      removeWidget,
      moveWidget,
      addScreen,
      removeScreen,
      duplicateScreen,
      lastExport,
      setLastExport,
      loadTemplateProject,
      saveStyleToken,
      deleteStyleToken,
      styleEditorSelection,
      setStyleEditorSelection,
    }),
    [
      project,
      setProject,
      editorTarget,
      selectedPath,
      selectWidget,
      addWidget,
      updateWidget,
      removeWidget,
      moveWidget,
      addScreen,
      removeScreen,
      duplicateScreen,
      lastExport,
      setLastExport,
      loadTemplateProject,
      saveStyleToken,
      deleteStyleToken,
      styleEditorSelection,
      setStyleEditorSelection,
    ]
  );

  return <ProjectContext.Provider value={value}>{children}</ProjectContext.Provider>;
}

export function useProject(): ProjectContextValue {
  const ctx = useContext(ProjectContext);
  if (!ctx) {
    throw new Error("useProject must be used within ProjectProvider");
  }
  return ctx;
}
