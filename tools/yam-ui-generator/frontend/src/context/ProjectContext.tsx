import { createContext, Dispatch, ReactNode, SetStateAction, useCallback, useContext, useEffect, useMemo, useState } from "react";
import {
  AssetReference,
  ProjectModel,
  StyleTokenModel,
  TranslationLocaleModel,
  TranslationStore,
  ValidationIssue,
  WidgetNode,
  WidgetPath,
} from "../types/yamui";
import { createWidget } from "../utils/widgetTemplates";
import { fetchAssetCatalog, fetchTemplateProject } from "../utils/api";

export interface EditorTarget {
  type: "screen" | "component";
  id: string;
}

export type AssetFilterState = {
  query: string;
  tags: string[];
  targets: string[];
  kinds: AssetReference["kind"][];
};

export type PendingAssetUpload = {
  id: string;
  fileName: string;
  status: "uploading" | "error";
  error?: string;
};

export type AssetTagDraftMap = Record<string, string>;
export type AssetTagBusyMap = Record<string, boolean>;

type TranslationBindingRequest = {
  path: WidgetPath;
  suggestedKey?: string;
};

type TranslationFocusRequest = {
  key: string;
  origin?: string;
};

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
  assetCatalog: AssetReference[];
  assetCatalogBusy: boolean;
  assetCatalogError: string | null;
  assetCatalogLoadedAt: number | null;
  loadAssetCatalog: (options?: { force?: boolean }) => Promise<void>;
  setAssetCatalog: Dispatch<SetStateAction<AssetReference[]>>;
  setAssetCatalogError: Dispatch<SetStateAction<string | null>>;
  assetFilters: AssetFilterState;
  setAssetFilters: Dispatch<SetStateAction<AssetFilterState>>;
  resetAssetFilters: () => void;
  pendingAssetUploads: PendingAssetUpload[];
  setPendingAssetUploads: Dispatch<SetStateAction<PendingAssetUpload[]>>;
  assetTagDrafts: AssetTagDraftMap;
  setAssetTagDrafts: Dispatch<SetStateAction<AssetTagDraftMap>>;
  assetTagBusyMap: AssetTagBusyMap;
  setAssetTagBusyMap: Dispatch<SetStateAction<AssetTagBusyMap>>;
  translationBindingRequest: TranslationBindingRequest | null;
  requestTranslationBinding: (path: WidgetPath, options?: { suggestedKey?: string }) => void;
  clearTranslationBindingRequest: () => void;
  translationFocusRequest: TranslationFocusRequest | null;
  requestTranslationFocus: (key: string, options?: { origin?: string }) => void;
  clearTranslationFocusRequest: () => void;
  setTranslations: (next: TranslationStore) => void;
  addTranslationLocale: (locale: string, label?: string) => void;
  removeTranslationLocale: (locale: string) => void;
  setTranslationLocaleLabel: (locale: string, label: string) => void;
  addTranslationKey: (key: string) => void;
  renameTranslationKey: (currentKey: string, nextKey: string) => boolean;
  deleteTranslationKey: (key: string) => void;
  updateTranslationValue: (locale: string, key: string, value: string) => void;
}

const DEFAULT_ASSET_FILTERS: AssetFilterState = {
  query: "",
  tags: [],
  targets: [],
  kinds: [],
};

const ASSET_FILTERS_STORAGE_KEY = "yamui_asset_filters_v1";
const DEFAULT_LOCALE_CODE = "en";

const DEFAULT_LOCALE_LABELS: Record<string, string> = {
  en: "English",
};

const DEFAULT_PROJECT: ProjectModel = {
  app: {},
  state: {},
  translations: {
    [DEFAULT_LOCALE_CODE]: {
      label: DEFAULT_LOCALE_LABELS[DEFAULT_LOCALE_CODE],
      entries: {},
      metadata: {},
    },
  },
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
  normalizeTranslations(project);
  return project;
}

function ensureTranslationLocale(project: ProjectModel, locale: string): TranslationLocaleModel {
  if (!project.translations[locale]) {
    project.translations[locale] = {
      label: DEFAULT_LOCALE_LABELS[locale] ?? locale,
      entries: {},
      metadata: {},
    };
  } else {
    const bucket = project.translations[locale]!;
    bucket.entries = bucket.entries ?? {};
    bucket.metadata = bucket.metadata ?? {};
  }
  return project.translations[locale]!;
}

function normalizeTranslations(project: ProjectModel): void {
  if (!project.translations || typeof project.translations !== "object") {
    project.translations = {};
  }
  Object.keys(project.translations).forEach((locale) => ensureTranslationLocale(project, locale));
  if (Object.keys(project.translations).length === 0) {
    ensureTranslationLocale(project, DEFAULT_LOCALE_CODE);
  }
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
  const [assetCatalog, setAssetCatalog] = useState<AssetReference[]>([]);
  const [assetCatalogBusy, setAssetCatalogBusy] = useState(false);
  const [assetCatalogError, setAssetCatalogError] = useState<string | null>(null);
  const [assetCatalogLoadedAt, setAssetCatalogLoadedAt] = useState<number | null>(null);
  const [assetFilters, setAssetFilters] = useState<AssetFilterState>(() => {
    if (typeof window === "undefined") {
      return DEFAULT_ASSET_FILTERS;
    }
    try {
      const stored = window.localStorage.getItem(ASSET_FILTERS_STORAGE_KEY);
      if (!stored) {
        return DEFAULT_ASSET_FILTERS;
      }
      const parsed = JSON.parse(stored) as Partial<AssetFilterState>;
      return {
        query: typeof parsed.query === "string" ? parsed.query : DEFAULT_ASSET_FILTERS.query,
        tags: Array.isArray(parsed.tags) ? parsed.tags : DEFAULT_ASSET_FILTERS.tags,
        targets: Array.isArray(parsed.targets) ? parsed.targets : DEFAULT_ASSET_FILTERS.targets,
        kinds: Array.isArray(parsed.kinds) ? parsed.kinds : DEFAULT_ASSET_FILTERS.kinds,
      };
    } catch (error) {
      console.warn("Unable to parse stored asset filters", error);
      return DEFAULT_ASSET_FILTERS;
    }
  });
    useEffect(() => {
      if (typeof window === "undefined") {
        return;
      }
      try {
        window.localStorage.setItem(ASSET_FILTERS_STORAGE_KEY, JSON.stringify(assetFilters));
      } catch (error) {
        console.warn("Unable to persist asset filters", error);
      }
    }, [assetFilters]);
  const [pendingAssetUploads, setPendingAssetUploads] = useState<PendingAssetUpload[]>([]);
  const [assetTagDrafts, setAssetTagDrafts] = useState<AssetTagDraftMap>({});
  const [assetTagBusyMap, setAssetTagBusyMap] = useState<AssetTagBusyMap>({});
  const [translationBindingRequest, setTranslationBindingRequest] = useState<TranslationBindingRequest | null>(null);
  const [translationFocusRequest, setTranslationFocusRequest] = useState<TranslationFocusRequest | null>(null);

  useEffect(() => {
    setStyleEditorSelection((current) => {
      if (current && project.styles[current]) {
        return current;
      }
      const fallback = Object.keys(project.styles)[0] ?? null;
      return fallback ?? null;
    });
  }, [project.styles, setStyleEditorSelection]);

  const loadAssetCatalog = useCallback(
    async ({ force }: { force?: boolean } = {}) => {
      if (assetCatalogBusy && !force) {
        return;
      }
      setAssetCatalogBusy(true);
      try {
        const catalog = await fetchAssetCatalog(project);
        setAssetCatalog(catalog);
        setAssetCatalogError(null);
        setAssetCatalogLoadedAt(Date.now());
      } catch (error) {
        setAssetCatalogError(error instanceof Error ? error.message : "Unable to load asset catalog");
      } finally {
        setAssetCatalogBusy(false);
      }
    },
    [assetCatalogBusy, project]
  );

  const resetAssetFilters = useCallback(() => {
    setAssetFilters(DEFAULT_ASSET_FILTERS);
  }, []);

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

  const requestTranslationBinding = useCallback(
    (path: WidgetPath, options?: { suggestedKey?: string }) => {
      setTranslationBindingRequest({ path: [...path], suggestedKey: options?.suggestedKey });
    },
    []
  );

  const clearTranslationBindingRequest = useCallback(() => {
    setTranslationBindingRequest(null);
  }, []);

  const requestTranslationFocus = useCallback((key: string, options?: { origin?: string }) => {
    const trimmed = key.trim();
    if (!trimmed) {
      return;
    }
    setTranslationFocusRequest({ key: trimmed, origin: options?.origin });
  }, []);

  const clearTranslationFocusRequest = useCallback(() => {
    setTranslationFocusRequest(null);
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

  const setTranslations = useCallback((nextTranslations: TranslationStore) => {
    setProjectState((prev) => {
      const next = cloneProject(prev);
      next.translations = deepClone(nextTranslations);
      return normalizeProjectInPlace(next);
    });
  }, []);

  const addTranslationLocale = useCallback((locale: string, label?: string) => {
    const code = locale.trim();
    if (!code) {
      return;
    }
    setProjectState((prev) => {
      if (prev.translations[code]) {
        return prev;
      }
      const next = cloneProject(prev);
      const bucket = ensureTranslationLocale(next, code);
      const trimmedLabel = label?.trim();
      bucket.label = trimmedLabel && trimmedLabel.length ? trimmedLabel : bucket.label;
      return normalizeProjectInPlace(next);
    });
  }, []);

  const removeTranslationLocale = useCallback((locale: string) => {
    const code = locale.trim();
    if (!code) {
      return;
    }
    setProjectState((prev) => {
      if (!prev.translations[code] || Object.keys(prev.translations).length <= 1) {
        return prev;
      }
      const next = cloneProject(prev);
      delete next.translations[code];
      return normalizeProjectInPlace(next);
    });
  }, []);

  const setTranslationLocaleLabel = useCallback((locale: string, label: string) => {
    const code = locale.trim();
    if (!code) {
      return;
    }
    setProjectState((prev) => {
      if (!prev.translations[code]) {
        return prev;
      }
      const next = cloneProject(prev);
      const bucket = ensureTranslationLocale(next, code);
      const trimmed = label.trim();
      bucket.label = trimmed.length ? trimmed : undefined;
      return normalizeProjectInPlace(next);
    });
  }, []);

  const addTranslationKey = useCallback((key: string) => {
    const name = key.trim();
    if (!name) {
      return;
    }
    setProjectState((prev) => {
      if (!Object.keys(prev.translations).length) {
        return prev;
      }
      const next = cloneProject(prev);
      Object.keys(next.translations).forEach((locale) => {
        const bucket = ensureTranslationLocale(next, locale);
        if (!(name in bucket.entries)) {
          bucket.entries[name] = "";
        }
      });
      return normalizeProjectInPlace(next);
    });
  }, []);

  const deleteTranslationKey = useCallback((key: string) => {
    const name = key.trim();
    if (!name) {
      return;
    }
    setProjectState((prev) => {
      const next = cloneProject(prev);
      let mutated = false;
      Object.values(next.translations).forEach((bucket) => {
        if (!bucket?.entries) {
          return;
        }
        if (bucket.entries[name] !== undefined) {
          delete bucket.entries[name];
          mutated = true;
        }
      });
      if (!mutated) {
        return prev;
      }
      return normalizeProjectInPlace(next);
    });
  }, []);

  const updateTranslationValue = useCallback((locale: string, key: string, value: string) => {
    const code = locale.trim();
    const name = key.trim();
    if (!code || !name) {
      return;
    }
    setProjectState((prev) => {
      if (!prev.translations[code]) {
        return prev;
      }
      const next = cloneProject(prev);
      const bucket = ensureTranslationLocale(next, code);
      bucket.entries[name] = value;
      return normalizeProjectInPlace(next);
    });
  }, []);

  const renameTranslationKey = useCallback((currentKey: string, nextKey: string) => {
    const current = currentKey.trim();
    const target = nextKey.trim();
    if (!current || !target || current === target) {
      return false;
    }
    let renamed = false;
    setProjectState((prev) => {
      if (Object.values(prev.translations).some((bucket) => bucket?.entries && target in bucket.entries)) {
        return prev;
      }
      const next = cloneProject(prev);
      let mutated = false;
      Object.values(next.translations).forEach((bucket) => {
        if (!bucket?.entries) {
          return;
        }
        if (!(current in bucket.entries)) {
          return;
        }
        bucket.entries[target] = bucket.entries[current]!;
        delete bucket.entries[current];
        mutated = true;
      });
      if (!mutated) {
        return prev;
      }
      renamed = true;
      return normalizeProjectInPlace(next);
    });
    return renamed;
  }, []);

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
      assetCatalog,
      assetCatalogBusy,
      assetCatalogError,
      assetCatalogLoadedAt,
      loadAssetCatalog,
      setAssetCatalog,
      setAssetCatalogError,
      assetFilters,
      setAssetFilters,
      resetAssetFilters,
      pendingAssetUploads,
      setPendingAssetUploads,
      assetTagDrafts,
      setAssetTagDrafts,
      assetTagBusyMap,
      setAssetTagBusyMap,
      translationBindingRequest,
      requestTranslationBinding,
      clearTranslationBindingRequest,
      translationFocusRequest,
      requestTranslationFocus,
      clearTranslationFocusRequest,
      setTranslations,
      addTranslationLocale,
      removeTranslationLocale,
      setTranslationLocaleLabel,
      addTranslationKey,
      renameTranslationKey,
      deleteTranslationKey,
      updateTranslationValue,
    }),
    [
      project,
      setProject,
      editorTarget,
      selectedPath,
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
      assetCatalog,
      assetCatalogBusy,
      assetCatalogError,
      assetCatalogLoadedAt,
      loadAssetCatalog,
      setAssetCatalog,
      setAssetCatalogError,
      assetFilters,
      setAssetFilters,
      resetAssetFilters,
      pendingAssetUploads,
      setPendingAssetUploads,
      assetTagDrafts,
      setAssetTagDrafts,
      assetTagBusyMap,
      setAssetTagBusyMap,
      translationBindingRequest,
      requestTranslationBinding,
      clearTranslationBindingRequest,
      translationFocusRequest,
      requestTranslationFocus,
      clearTranslationFocusRequest,
      setTranslations,
      addTranslationLocale,
      removeTranslationLocale,
      setTranslationLocaleLabel,
      addTranslationKey,
      renameTranslationKey,
      deleteTranslationKey,
      updateTranslationValue,
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
