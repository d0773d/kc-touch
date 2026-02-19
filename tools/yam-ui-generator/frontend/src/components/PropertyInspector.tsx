import { useCallback, useEffect, useMemo, useRef, useState, type CSSProperties, type ChangeEvent, type DragEvent, type KeyboardEvent } from "react";
import { useProject } from "../context/ProjectContext";
import { AssetReference, ProjectModel, StyleCategory, StylePreview, StyleTokenModel, ValidationIssue, WidgetNode, WidgetPath } from "../types/yamui";
import { previewStyle, updateAssetTags } from "../utils/api";
import { buildTranslationExpression, extractTranslationKey, getPrimaryLocale, suggestTranslationKey } from "../utils/translation";
import { emitTelemetry } from "../utils/telemetry";
import StylePreviewCard from "./StylePreviewCard";
import AssetUploadQueue from "./AssetUploadQueue";
import { useAssetUploads } from "../hooks/useAssetUploads";

function findWidget(root: WidgetNode[], path: WidgetPath): WidgetNode | undefined {
  let current: WidgetNode | undefined;
  let nodes = root;
  for (let i = 0; i < path.length; i += 1) {
    current = nodes[path[i]];
    if (!current) {
      return undefined;
    }
    nodes = current.widgets ?? [];
  }
  return current;
}

function formatAssetTarget(target: string): string {
  const [scope, name] = target.split(":");
  if (!name) {
    return target;
  }
  if (scope === "screen") {
    return `Screen • ${name}`;
  }
  if (scope === "component") {
    return `Component • ${name}`;
  }
  return target;
}

const JSON_FIELDS: Array<keyof Pick<WidgetNode, "props" | "events" | "bindings" | "accessibility">> = [
  "props",
  "events",
  "bindings",
  "accessibility",
];

const EVENT_TEMPLATES = [
  {
    label: "Push Screen",
    value: { on_click: ["push(other_screen)"] },
  },
  {
    label: "Open Modal",
    value: { on_click: ["modal(ComponentName)"] },
  },
  {
    label: "Set State",
    value: { on_click: ["set(state.path, value)"] },
  },
];

const RECENT_STYLES_KEY = "yamui_recent_styles_v1";
const STYLE_CATEGORY_OPTIONS: Array<StyleCategory | "all"> = ["all", "color", "surface", "text", "spacing", "shadow"];

const ASSET_KIND_OPTIONS: AssetReference["kind"][] = ["image", "video", "audio", "font", "binary", "unknown"];

type AssetFilterGroup = "tags" | "targets" | "kinds";

const formatBytes = (size?: number): string => {
  if (typeof size !== "number" || Number.isNaN(size)) {
    return "—";
  }
  if (size < 1024) {
    return `${size} B`;
  }
  const units = ["KB", "MB", "GB", "TB"];
  let value = size / 1024;
  let unitIndex = 0;
  while (value >= 1024 && unitIndex < units.length - 1) {
    value /= 1024;
    unitIndex += 1;
  }
  return `${value.toFixed(value >= 10 ? 0 : 1)} ${units[unitIndex]}`;
};

const normalizeTagList = (value: string): string[] => {
  return value
    .split(",")
    .map((tag) => tag.trim())
    .filter(Boolean);
};

type EventsMap = Record<string, string[]>;
type BindingsMap = Record<string, string>;
type PropsMap = Record<string, unknown>;
type AccessibilityMap = Record<string, string>;

const parseEventsJson = (raw: string): { value: EventsMap; valid: boolean } => {
  if (!raw.trim()) {
    return { value: {}, valid: true };
  }
  try {
    const parsed = JSON.parse(raw);
    if (!parsed || typeof parsed !== "object" || Array.isArray(parsed)) {
      return { value: {}, valid: false };
    }
    const map: EventsMap = {};
    Object.entries(parsed as Record<string, unknown>).forEach(([key, handlers]) => {
      const bucket = Array.isArray(handlers)
        ? handlers
        : typeof handlers === "string"
          ? [handlers]
          : [];
      const normalized = bucket.map((entry) => (typeof entry === "string" ? entry.trim() : ""));
      if (!normalized.length) {
        normalized.push("");
      }
      map[key] = normalized;
    });
    return { value: map, valid: true };
  } catch {
    return { value: {}, valid: false };
  }
};

const normalizeEventsMap = (input: EventsMap): EventsMap => {
  const next: EventsMap = {};
  Object.entries(input).forEach(([key, handlers]) => {
    const name = key.trim();
    if (!name) {
      return;
    }
    const list = Array.isArray(handlers) ? handlers : [];
    const cleaned = list.map((entry) => (typeof entry === "string" ? entry : ""));
    if (!cleaned.length) {
      cleaned.push("");
    }
    next[name] = cleaned;
  });
  return next;
};

const parseBindingsJson = (raw: string): { value: BindingsMap; valid: boolean } => {
  if (!raw.trim()) {
    return { value: {}, valid: true };
  }
  try {
    const parsed = JSON.parse(raw);
    if (!parsed || typeof parsed !== "object" || Array.isArray(parsed)) {
      return { value: {}, valid: false };
    }
    const map: BindingsMap = {};
    Object.entries(parsed as Record<string, unknown>).forEach(([key, value]) => {
      map[key] = typeof value === "string" ? value : JSON.stringify(value);
    });
    return { value: map, valid: true };
  } catch {
    return { value: {}, valid: false };
  }
};

const normalizeBindingsMap = (input: BindingsMap): BindingsMap => {
  const next: BindingsMap = {};
  Object.entries(input).forEach(([key, value]) => {
    const name = key.trim();
    if (!name) {
      return;
    }
    next[name] = value;
  });
  return next;
};

const parsePropsJson = (raw: string): { value: PropsMap; valid: boolean } => {
  if (!raw.trim()) {
    return { value: {}, valid: true };
  }
  try {
    const parsed = JSON.parse(raw);
    if (!parsed || typeof parsed !== "object" || Array.isArray(parsed)) {
      return { value: {}, valid: false };
    }
    return { value: parsed as PropsMap, valid: true };
  } catch {
    return { value: {}, valid: false };
  }
};

const normalizePropsMap = (input: PropsMap): PropsMap => {
  const next: PropsMap = {};
  Object.entries(input).forEach(([key, value]) => {
    const name = key.trim();
    if (!name) {
      return;
    }
    next[name] = value;
  });
  return next;
};

const parseAccessibilityJson = (raw: string): { value: AccessibilityMap; valid: boolean } => {
  if (!raw.trim()) {
    return { value: {}, valid: true };
  }
  try {
    const parsed = JSON.parse(raw);
    if (!parsed || typeof parsed !== "object" || Array.isArray(parsed)) {
      return { value: {}, valid: false };
    }
    const map: AccessibilityMap = {};
    Object.entries(parsed as Record<string, unknown>).forEach(([key, value]) => {
      map[key] = typeof value === "string" ? value : JSON.stringify(value);
    });
    return { value: map, valid: true };
  } catch {
    return { value: {}, valid: false };
  }
};

const normalizeAccessibilityMap = (input: AccessibilityMap): AccessibilityMap => {
  const next: AccessibilityMap = {};
  Object.entries(input).forEach(([key, value]) => {
    const name = key.trim();
    if (!name) {
      return;
    }
    next[name] = value;
  });
  return next;
};

const generateUniqueKey = (base: string, existing: Set<string>): string => {
  if (!existing.has(base)) {
    return base;
  }
  let suffix = 2;
  while (existing.has(`${base}_${suffix}`)) {
    suffix += 1;
  }
  return `${base}_${suffix}`;
};

function collectAssetSuggestions(project: ProjectModel): string[] {
  const set = new Set<string>();
  const visit = (widgets?: WidgetNode[]) => {
    if (!widgets) {
      return;
    }
    widgets.forEach((widget) => {
      if (widget.src) {
        set.add(widget.src);
      }
      visit(widget.widgets);
    });
  };
  Object.values(project.screens).forEach((screen) => visit(screen.widgets));
  Object.values(project.components).forEach((component) => visit(component.widgets));
  return Array.from(set);
}

function getStyleColor(token: StyleTokenModel, key: "backgroundColor" | "color", fallback: string): string {
  const value = token.value?.[key];
  return typeof value === "string" ? value : fallback;
}

const widgetPathsEqual = (a: WidgetPath | null | undefined, b: WidgetPath | null | undefined): boolean => {
  if (!a || !b) {
    return false;
  }
  if (a.length !== b.length) {
    return false;
  }
  return a.every((value, index) => b[index] === value);
};

interface PropertyInspectorProps {
  issues: ValidationIssue[];
  style?: CSSProperties;
}

export default function PropertyInspector({ issues, style }: PropertyInspectorProps): JSX.Element {
  const {
    project,
    editorTarget,
    selectedPath,
    updateWidget,
    addTranslationKey,
    updateTranslationValue,
    translationBindingRequest,
    clearTranslationBindingRequest,
    requestTranslationFocus,
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
    pendingAssetUploads: pendingUploads,
    assetTagDrafts: tagDrafts,
    setAssetTagDrafts: setTagDrafts,
    assetTagBusyMap: tagBusyMap,
    setAssetTagBusyMap: setTagBusyMap,
  } = useProject();
  const [jsonErrors, setJsonErrors] = useState<Record<string, string>>({});
  const [formState, setFormState] = useState({
    id: "",
    text: "",
    style: "",
    src: "",
    props: "{}",
    events: "{}",
    bindings: "{}",
    accessibility: "{}",
  });
  const [recentStyles, setRecentStyles] = useState<string[]>(() => {
    if (typeof window === "undefined") {
      return [];
    }
    try {
      const stored = window.localStorage.getItem(RECENT_STYLES_KEY);
      return stored ? (JSON.parse(stored) as string[]) : [];
    } catch {
      return [];
    }
  });
  const [styleCategoryFilter, setStyleCategoryFilter] = useState<StyleCategory | "all">("all");
  const [stylePreviewData, setStylePreviewData] = useState<StylePreview | null>(null);
  const [stylePreviewError, setStylePreviewError] = useState<string | null>(null);
  const [stylePreviewBusy, setStylePreviewBusy] = useState(false);
  const [assetModalOpen, setAssetModalOpen] = useState(false);
  const [shortcutMessage, setShortcutMessage] = useState<string | null>(null);
  const [isDroppingAsset, setIsDroppingAsset] = useState(false);
  const [translationFormOpen, setTranslationFormOpen] = useState(false);
  const [translationKeyDraft, setTranslationKeyDraft] = useState("");
  const [translationFormError, setTranslationFormError] = useState<string | null>(null);
  const structuredEvents = useMemo(() => parseEventsJson(formState.events), [formState.events]);
  const structuredBindings = useMemo(() => parseBindingsJson(formState.bindings), [formState.bindings]);
  const structuredProps = useMemo(() => parsePropsJson(formState.props), [formState.props]);
  const structuredAccessibility = useMemo(() => parseAccessibilityJson(formState.accessibility), [formState.accessibility]);
  const translationKeySet = useMemo(() => {
    const set = new Set<string>();
    Object.values(project.translations ?? {}).forEach((locale) => {
      Object.keys(locale?.entries ?? {}).forEach((key) => set.add(key));
    });
    return set;
  }, [project.translations]);
  const primaryLocale = useMemo(() => getPrimaryLocale(project), [project]);
  const currentTranslationKey = useMemo(() => extractTranslationKey(formState.text), [formState.text]);
  const canConvertTextToTranslation = Boolean(formState.text.trim().length && !currentTranslationKey);

  const assetFileInputRef = useRef<HTMLInputElement | null>(null);
  const translationKeyInputRef = useRef<HTMLInputElement | null>(null);
  const { uploadFiles, dismissPendingUpload } = useAssetUploads();

  const trackAssetEvent = useCallback((event: string, payload?: Record<string, unknown>) => {
    emitTelemetry("assets", event, payload);
  }, []);

  useEffect(() => {
    if (typeof window === "undefined") {
      return;
    }
    try {
      window.localStorage.setItem(RECENT_STYLES_KEY, JSON.stringify(recentStyles));
    } catch (error) {
      console.warn("Unable to persist recent styles", error);
    }
  }, [recentStyles]);

  useEffect(() => {
    setRecentStyles((prev) => {
      const filtered = prev.filter((styleKey) => Boolean(project.styles?.[styleKey]));
      return filtered.length === prev.length ? prev : filtered;
    });
  }, [project.styles]);

  const selectedWidget = useMemo(() => {
    if (!selectedPath) {
      return undefined;
    }
    const root = editorTarget.type === "screen"
      ? project.screens[editorTarget.id]?.widgets ?? []
      : project.components[editorTarget.id]?.widgets ?? [];
    return findWidget(root, selectedPath);
  }, [project, editorTarget, selectedPath]);

  const selectedStyleToken = useMemo(() => {
    const styleName = formState.style.trim();
    if (!styleName) {
      return undefined;
    }
    return project.styles?.[styleName];
  }, [formState.style, project.styles]);

  const styleValuePairs = useMemo(() => {
    if (!selectedStyleToken) {
      return [] as Array<[string, unknown]>;
    }
    return Object.entries(selectedStyleToken.value ?? {}).slice(0, 3);
  }, [selectedStyleToken]);

  useEffect(() => {
    let cancelled = false;
    if (!selectedStyleToken) {
      setStylePreviewData(null);
      setStylePreviewError(null);
      setStylePreviewBusy(false);
      return;
    }
    setStylePreviewBusy(true);
    previewStyle(selectedStyleToken)
      .then((preview) => {
        if (!cancelled) {
          setStylePreviewData(preview);
          setStylePreviewError(null);
        }
      })
      .catch((error) => {
        if (!cancelled) {
          setStylePreviewData(null);
          setStylePreviewError(error instanceof Error ? error.message : "Unable to preview style");
        }
      })
      .finally(() => {
        if (!cancelled) {
          setStylePreviewBusy(false);
        }
      });
    return () => {
      cancelled = true;
    };
  }, [selectedStyleToken]);

  const handleFocusStyle = () => {
    const styleKey = formState.style.trim();
    if (!styleKey || !project.styles?.[styleKey]) {
      return;
    }
    setStyleEditorSelection(styleKey);
    const scrollIntoViewWithRetry = (elementId: string, attempt = 0) => {
      const node = document.getElementById(elementId);
      if (node) {
        node.scrollIntoView({ behavior: "smooth", block: "start" });
        return;
      }
      if (attempt < 5) {
        window.setTimeout(() => scrollIntoViewWithRetry(elementId, attempt + 1), 80);
      }
    };
    scrollIntoViewWithRetry("style-manager");
    window.setTimeout(() => scrollIntoViewWithRetry("style-editor-panel"), 120);
  };

  const styleOptions = useMemo(() => Object.keys(project.styles ?? {}), [project.styles]);
  const assetOptions = useMemo(() => collectAssetSuggestions(project), [project]);
  const assetGrid = useMemo(() => assetOptions.slice(0, 12), [assetOptions]);
  const filteredAssetCatalog = useMemo(() => {
    if (!assetCatalog.length) {
      return [] as AssetReference[];
    }
    const query = assetFilters.query.trim().toLowerCase();
    return assetCatalog.filter((asset) => {
      if (assetFilters.kinds.length && !assetFilters.kinds.includes(asset.kind)) {
        return false;
      }
      if (assetFilters.tags.length) {
        const assetTags = asset.tags.map((tag) => tag.toLowerCase());
        const matchesAllTags = assetFilters.tags.every((tag) => assetTags.includes(tag));
        if (!matchesAllTags) {
          return false;
        }
      }
      if (assetFilters.targets.length) {
        const matchesTarget = asset.targets.some((target) => assetFilters.targets.includes(target));
        if (!matchesTarget) {
          return false;
        }
      }
      if (!query) {
        return true;
      }
      const haystack = `${asset.label} ${asset.path} ${asset.tags.join(" ")} ${asset.targets.join(" ")}`.toLowerCase();
      return haystack.includes(query);
    });
  }, [assetCatalog, assetFilters]);
  const visibleAssetCatalog = useMemo(() => filteredAssetCatalog.slice(0, 60), [filteredAssetCatalog]);
  const filtersActive = assetFilters.tags.length + assetFilters.targets.length + assetFilters.kinds.length > 0;
  const assetHintOptions = useMemo(() => {
    if (assetCatalog.length) {
      return assetCatalog.map((asset) => asset.path);
    }
    return assetOptions;
  }, [assetCatalog, assetOptions]);
  const basePath = useMemo(
    () => (editorTarget.type === "screen" ? `/screens/${editorTarget.id}/widgets` : `/components/${editorTarget.id}/widgets`),
    [editorTarget]
  );
  const selectedIssuePath = useMemo(() => {
    if (!selectedPath) {
      return null;
    }
    const suffix = selectedPath.map((index) => `widgets/${index}`).join("/");
    return suffix ? `${basePath}/${suffix}` : basePath;
  }, [basePath, selectedPath]);
  const widgetIssues = useMemo(() => {
    if (!selectedIssuePath) {
      return [];
    }
    return issues.filter(
      (issue) => issue.path === selectedIssuePath || issue.path.startsWith(`${selectedIssuePath}/`)
    );
  }, [issues, selectedIssuePath]);

  const recordRecentStyle = useCallback((name: string) => {
    setRecentStyles((prev) => {
      const next = [name, ...prev.filter((value) => value !== name)];
      return next.slice(0, 6);
    });
  }, []);

  const applyStyleValue = useCallback(
    (value: string) => {
      setFormState((prev) => ({ ...prev, style: value }));
      if (!selectedPath) {
        return;
      }
      updateWidget(selectedPath, { style: value });
      if (project.styles?.[value]) {
        recordRecentStyle(value);
        setStyleEditorSelection(value);
      }
    },
    [project.styles, recordRecentStyle, selectedPath, setStyleEditorSelection, updateWidget]
  );

  const applyAssetValue = useCallback(
    (value: string) => {
      setFormState((prev) => ({ ...prev, src: value }));
      if (!selectedPath) {
        return;
      }
      updateWidget(selectedPath, { src: value });
    },
    [selectedPath, updateWidget]
  );

  const applyEventMapUpdate = useCallback(
    (mutator: (current: EventsMap) => EventsMap) => {
      const current = { ...parseEventsJson(formState.events).value };
      const next = normalizeEventsMap(mutator(current));
      const serialized = JSON.stringify(next, null, 2);
      setFormState((prev) => ({ ...prev, events: serialized }));
      if (selectedPath) {
        updateWidget(selectedPath, { events: next });
      }
      setJsonErrors((prev) => ({ ...prev, events: "" }));
    },
    [formState.events, selectedPath, updateWidget]
  );

  const applyBindingMapUpdate = useCallback(
    (mutator: (current: BindingsMap) => BindingsMap) => {
      const current = { ...parseBindingsJson(formState.bindings).value };
      const next = normalizeBindingsMap(mutator(current));
      const serialized = JSON.stringify(next, null, 2);
      setFormState((prev) => ({ ...prev, bindings: serialized }));
      if (selectedPath) {
        updateWidget(selectedPath, { bindings: next });
      }
      setJsonErrors((prev) => ({ ...prev, bindings: "" }));
    },
    [formState.bindings, selectedPath, updateWidget]
  );

  const applyPropsMapUpdate = useCallback(
    (mutator: (current: PropsMap) => PropsMap) => {
      const current = { ...parsePropsJson(formState.props).value };
      const next = normalizePropsMap(mutator(current));
      const serialized = JSON.stringify(next, null, 2);
      setFormState((prev) => ({ ...prev, props: serialized }));
      if (selectedPath) {
        updateWidget(selectedPath, { props: next });
      }
      setJsonErrors((prev) => ({ ...prev, props: "" }));
    },
    [formState.props, selectedPath, updateWidget]
  );

  const applyAccessibilityMapUpdate = useCallback(
    (mutator: (current: AccessibilityMap) => AccessibilityMap) => {
      const current = { ...parseAccessibilityJson(formState.accessibility).value };
      const next = normalizeAccessibilityMap(mutator(current));
      const serialized = JSON.stringify(next, null, 2);
      setFormState((prev) => ({ ...prev, accessibility: serialized }));
      if (selectedPath) {
        updateWidget(selectedPath, { accessibility: next });
      }
      setJsonErrors((prev) => ({ ...prev, accessibility: "" }));
    },
    [formState.accessibility, selectedPath, updateWidget]
  );

  const handleAddEventEntry = useCallback(() => {
    applyEventMapUpdate((prev) => {
      const existing = new Set(Object.keys(prev));
      const key = generateUniqueKey("on_click", existing);
      return { ...prev, [key]: [""] };
    });
  }, [applyEventMapUpdate]);

  const handleRenameEvent = useCallback(
    (currentName: string, nextName: string) => {
      applyEventMapUpdate((prev) => {
        if (!prev[currentName]) {
          return prev;
        }
        const clone = { ...prev };
        const handlers = clone[currentName];
        delete clone[currentName];
        const trimmed = nextName.trim();
        if (!trimmed) {
          return clone;
        }
        if (clone[trimmed]) {
          const existing = new Set(Object.keys(clone));
          const unique = generateUniqueKey(trimmed, existing);
          clone[unique] = handlers;
          return clone;
        }
        clone[trimmed] = handlers;
        return clone;
      });
    },
    [applyEventMapUpdate]
  );

  const handleRemoveEvent = useCallback(
    (name: string) => {
      applyEventMapUpdate((prev) => {
        if (!prev[name]) {
          return prev;
        }
        const clone = { ...prev };
        delete clone[name];
        return clone;
      });
    },
    [applyEventMapUpdate]
  );

  const handleAddEventAction = useCallback(
    (name: string) => {
      applyEventMapUpdate((prev) => {
        if (!prev[name]) {
          return prev;
        }
        return { ...prev, [name]: [...prev[name], ""] };
      });
    },
    [applyEventMapUpdate]
  );

  const handleUpdateEventAction = useCallback(
    (name: string, index: number, value: string) => {
      applyEventMapUpdate((prev) => {
        if (!prev[name]) {
          return prev;
        }
        const nextHandlers = [...prev[name]];
        nextHandlers[index] = value;
        return { ...prev, [name]: nextHandlers };
      });
    },
    [applyEventMapUpdate]
  );

  const handleRemoveEventAction = useCallback(
    (name: string, index: number) => {
      applyEventMapUpdate((prev) => {
        if (!prev[name]) {
          return prev;
        }
        const nextHandlers = prev[name].filter((_, handlerIndex) => handlerIndex !== index);
        if (!nextHandlers.length) {
          const clone = { ...prev };
          delete clone[name];
          return clone;
        }
        return { ...prev, [name]: nextHandlers };
      });
    },
    [applyEventMapUpdate]
  );

  const handleAddBinding = useCallback(() => {
    applyBindingMapUpdate((prev) => {
      const existing = new Set(Object.keys(prev));
      const key = generateUniqueKey("data", existing);
      return { ...prev, [key]: "" };
    });
  }, [applyBindingMapUpdate]);

  const handleRenameBinding = useCallback(
    (currentName: string, nextName: string) => {
      applyBindingMapUpdate((prev) => {
        if (!prev[currentName]) {
          return prev;
        }
        const clone = { ...prev };
        const value = clone[currentName];
        delete clone[currentName];
        const trimmed = nextName.trim();
        if (!trimmed) {
          return clone;
        }
        if (clone[trimmed]) {
          const existing = new Set(Object.keys(clone));
          const unique = generateUniqueKey(trimmed, existing);
          clone[unique] = value;
          return clone;
        }
        clone[trimmed] = value;
        return clone;
      });
    },
    [applyBindingMapUpdate]
  );

  const handleUpdateBindingValue = useCallback(
    (name: string, value: string) => {
      applyBindingMapUpdate((prev) => ({ ...prev, [name]: value }));
    },
    [applyBindingMapUpdate]
  );

  const handleRemoveBinding = useCallback(
    (name: string) => {
      applyBindingMapUpdate((prev) => {
        if (!(name in prev)) {
          return prev;
        }
        const clone = { ...prev };
        delete clone[name];
        return clone;
      });
    },
    [applyBindingMapUpdate]
  );

  const handleAddProp = useCallback(() => {
    applyPropsMapUpdate((prev) => {
      const existing = new Set(Object.keys(prev));
      const key = generateUniqueKey("prop", existing);
      return { ...prev, [key]: "" };
    });
  }, [applyPropsMapUpdate]);

  const handleRenameProp = useCallback(
    (currentName: string, nextName: string) => {
      applyPropsMapUpdate((prev) => {
        if (!(currentName in prev)) {
          return prev;
        }
        const clone = { ...prev };
        const value = clone[currentName];
        delete clone[currentName];
        const trimmed = nextName.trim();
        if (!trimmed) {
          return clone;
        }
        if (clone[trimmed] !== undefined) {
          const existing = new Set(Object.keys(clone));
          const unique = generateUniqueKey(trimmed, existing);
          clone[unique] = value;
          return clone;
        }
        clone[trimmed] = value;
        return clone;
      });
    },
    [applyPropsMapUpdate]
  );

  const handleUpdatePropValue = useCallback(
    (name: string, value: unknown) => {
      applyPropsMapUpdate((prev) => ({ ...prev, [name]: value }));
    },
    [applyPropsMapUpdate]
  );

  const handleRemoveProp = useCallback(
    (name: string) => {
      applyPropsMapUpdate((prev) => {
        if (!(name in prev)) {
          return prev;
        }
        const clone = { ...prev };
        delete clone[name];
        return clone;
      });
    },
    [applyPropsMapUpdate]
  );

  const handleAddAccessibilityEntry = useCallback(() => {
    applyAccessibilityMapUpdate((prev) => {
      const existing = new Set(Object.keys(prev));
      const key = generateUniqueKey("aria_label", existing);
      return { ...prev, [key]: "" };
    });
  }, [applyAccessibilityMapUpdate]);

  const handleRenameAccessibilityEntry = useCallback(
    (currentName: string, nextName: string) => {
      applyAccessibilityMapUpdate((prev) => {
        if (!(currentName in prev)) {
          return prev;
        }
        const clone = { ...prev };
        const value = clone[currentName];
        delete clone[currentName];
        const trimmed = nextName.trim();
        if (!trimmed) {
          return clone;
        }
        if (clone[trimmed] !== undefined) {
          const existing = new Set(Object.keys(clone));
          const unique = generateUniqueKey(trimmed, existing);
          clone[unique] = value;
          return clone;
        }
        clone[trimmed] = value;
        return clone;
      });
    },
    [applyAccessibilityMapUpdate]
  );

  const handleUpdateAccessibilityValue = useCallback(
    (name: string, value: string) => {
      applyAccessibilityMapUpdate((prev) => ({ ...prev, [name]: value }));
    },
    [applyAccessibilityMapUpdate]
  );

  const handleRemoveAccessibilityEntry = useCallback(
    (name: string) => {
      applyAccessibilityMapUpdate((prev) => {
        if (!(name in prev)) {
          return prev;
        }
        const clone = { ...prev };
        delete clone[name];
        return clone;
      });
    },
    [applyAccessibilityMapUpdate]
  );

  const handleAddFilterTag = useCallback((tag: string) => {
    const normalized = tag.trim().toLowerCase();
    if (!normalized) {
      return;
    }
    setAssetFilters((prev) => {
      if (prev.tags.includes(normalized)) {
        return prev;
      }
      trackAssetEvent("asset_filter_add", { type: "tag", value: normalized });
      return { ...prev, tags: [...prev.tags, normalized] };
    });
  }, [setAssetFilters, trackAssetEvent]);

  const handleAddTargetFilter = useCallback((target: string) => {
    if (!target) {
      return;
    }
    setAssetFilters((prev) => {
      if (prev.targets.includes(target)) {
        return prev;
      }
      trackAssetEvent("asset_filter_add", { type: "target", value: target });
      return { ...prev, targets: [...prev.targets, target] };
    });
  }, [setAssetFilters, trackAssetEvent]);

  const handleRemoveFilter = useCallback((group: AssetFilterGroup, value: string) => {
    setAssetFilters((prev) => {
      const nextGroup = prev[group].filter((entry) => entry !== value);
      return { ...prev, [group]: nextGroup };
    });
    trackAssetEvent("asset_filter_remove", { group, value });
  }, [setAssetFilters, trackAssetEvent]);

  const handleToggleKindFilter = useCallback((kind: AssetReference["kind"]) => {
    setAssetFilters((prev) => {
      const exists = prev.kinds.includes(kind);
      const nextKinds = exists ? prev.kinds.filter((value) => value !== kind) : [...prev.kinds, kind];
      trackAssetEvent("asset_filter_toggle", { type: "kind", value: kind, active: !exists });
      return { ...prev, kinds: nextKinds };
    });
  }, [setAssetFilters, trackAssetEvent]);

  const handleClearFilters = useCallback(() => {
    resetAssetFilters();
    trackAssetEvent("asset_filters_cleared");
  }, [resetAssetFilters, trackAssetEvent]);

  const handleSearchQueryChange = useCallback((value: string) => {
    setAssetFilters((prev) => ({ ...prev, query: value }));
  }, [setAssetFilters]);

  const handleFilesUpload = useCallback(async (files: File[]) => {
    if (!files.length) {
      return;
    }
    await uploadFiles(files);
  }, [uploadFiles]);

  const handleFilePickerClick = useCallback(() => {
    trackAssetEvent("asset_upload_picker_opened");
    assetFileInputRef.current?.click();
  }, [trackAssetEvent]);

  const handleFileInputChange = useCallback((event: ChangeEvent<HTMLInputElement>) => {
    const files = event.target.files ? Array.from(event.target.files) : [];
    if (files.length) {
      void handleFilesUpload(files);
    }
    event.target.value = "";
  }, [handleFilesUpload]);

  const handleAssetDragOver = useCallback((event: DragEvent<HTMLDivElement>) => {
    event.preventDefault();
    setIsDroppingAsset(true);
  }, [setIsDroppingAsset]);

  const handleAssetDragLeave = useCallback((event: DragEvent<HTMLDivElement>) => {
    event.preventDefault();
    setIsDroppingAsset(false);
  }, [setIsDroppingAsset]);

  const handleAssetDrop = useCallback((event: DragEvent<HTMLDivElement>) => {
    event.preventDefault();
    setIsDroppingAsset(false);
    const files = Array.from(event.dataTransfer?.files ?? []);
    if (files.length) {
      void handleFilesUpload(files);
    }
  }, [handleFilesUpload, setIsDroppingAsset]);

  const handleDismissUpload = useCallback((id: string) => {
    dismissPendingUpload(id);
  }, [dismissPendingUpload]);

  const commitAssetTags = useCallback(
    async (asset: AssetReference, rawValue: string) => {
      const tags = normalizeTagList(rawValue).map((tag) => (tag.startsWith("#") ? tag.slice(1) : tag));
      setTagBusyMap((prev) => ({ ...prev, [asset.id]: true }));
      try {
        const updated = await updateAssetTags(asset.path, tags, project);
        setAssetCatalog((prev) => prev.map((item) => (item.id === updated.id ? updated : item)));
        setTagDrafts((prev) => ({ ...prev, [asset.id]: updated.tags.join(", ") }));
        trackAssetEvent("asset_tags_updated", { assetId: asset.id, tagCount: tags.length });
      } catch (error) {
        setAssetCatalogError(error instanceof Error ? error.message : "Unable to update tags");
      } finally {
        setTagBusyMap((prev) => ({ ...prev, [asset.id]: false }));
      }
    },
    [project, setAssetCatalog, setAssetCatalogError, setTagBusyMap, setTagDrafts, trackAssetEvent]
  );

  const handleTagInputChange = useCallback((assetId: string, value: string) => {
    setTagDrafts((prev) => ({ ...prev, [assetId]: value }));
  }, [setTagDrafts]);

  const handleTagInputKeyDown = useCallback((event: KeyboardEvent<HTMLInputElement>, asset: AssetReference) => {
    if (event.key === "Enter" && !event.shiftKey) {
      event.preventDefault();
      event.stopPropagation();
      const value = (event.target as HTMLInputElement).value;
      void commitAssetTags(asset, value);
    }
  }, [commitAssetTags]);

  const handleAssetModalRefresh = useCallback(() => {
    trackAssetEvent("asset_catalog_refresh_click");
    void loadAssetCatalog();
  }, [loadAssetCatalog, trackAssetEvent]);

  const filteredStyles = useMemo(() => {
    const entries = Object.entries(project.styles ?? {});
    const pool = styleCategoryFilter === "all"
      ? entries
      : entries.filter(([, token]) => token.category === styleCategoryFilter);
    return pool.slice(0, 6).map(([key, token]) => ({ key, token }));
  }, [project.styles, styleCategoryFilter]);

  const handleStyleQuickPick = useCallback(
    (name: string) => {
      applyStyleValue(name);
    },
    [applyStyleValue]
  );

  const handleShuffleStyle = () => {
    const fallbackPool = Object.entries(project.styles ?? {}).map(([key, token]) => ({ key, token }));
    const pool = filteredStyles.length ? filteredStyles : fallbackPool;
    if (!pool.length) {
      return;
    }
    const random = pool[Math.floor(Math.random() * pool.length)];
    applyStyleValue(random.key);
  };

  useEffect(() => {
    const handleKeyDown = (event: KeyboardEvent) => {
      if (!(event.metaKey || event.ctrlKey) || !event.shiftKey) {
        return;
      }
      const digit = Number(event.key);
      if (!Number.isInteger(digit)) {
        return;
      }
      const maxIndex = Math.min(6, recentStyles.length);
      if (digit < 1 || digit > maxIndex) {
        return;
      }
      event.preventDefault();
      const targetStyle = recentStyles[digit - 1];
      if (targetStyle) {
        handleStyleQuickPick(targetStyle);
        setShortcutMessage(`Applied ${targetStyle}`);
      }
    };
    window.addEventListener("keydown", handleKeyDown);
    return () => window.removeEventListener("keydown", handleKeyDown);
  }, [handleStyleQuickPick, recentStyles]);

  useEffect(() => {
    if (!shortcutMessage) {
      return;
    }
    const timer = window.setTimeout(() => setShortcutMessage(null), 2000);
    return () => window.clearTimeout(timer);
  }, [shortcutMessage]);

  useEffect(() => {
    if (!selectedWidget) {
      setFormState({ id: "", text: "", style: "", src: "", props: "{}", events: "{}", bindings: "{}", accessibility: "{}" });
      setTranslationFormOpen(false);
      setTranslationKeyDraft("");
      setTranslationFormError(null);
      return;
    }
    setFormState({
      id: selectedWidget.id ?? "",
      text: selectedWidget.text ?? "",
      style: selectedWidget.style ?? "",
      src: selectedWidget.src ?? "",
      props: JSON.stringify(selectedWidget.props ?? {}, null, 2),
      events: JSON.stringify(selectedWidget.events ?? {}, null, 2),
      bindings: JSON.stringify(selectedWidget.bindings ?? {}, null, 2),
      accessibility: JSON.stringify(selectedWidget.accessibility ?? {}, null, 2),
    });
    setTranslationFormOpen(false);
    setTranslationKeyDraft("");
    setTranslationFormError(null);
  }, [selectedWidget]);

  useEffect(() => {
    if (currentTranslationKey) {
      setTranslationFormOpen(false);
      setTranslationFormError(null);
    }
  }, [currentTranslationKey]);

  const openTranslationBindingForm = useCallback(
    (initialKey?: string) => {
      if (!canConvertTextToTranslation) {
        setTranslationFormError(primaryLocale ? "" : "Add a locale before binding text");
        return false;
      }
      const trimmed = initialKey?.trim();
      const nextKey = trimmed && trimmed.length ? trimmed : suggestTranslationKey(formState.text, translationKeySet);
      setTranslationKeyDraft(nextKey);
      setTranslationFormError(null);
      setTranslationFormOpen(true);
      return true;
    },
    [canConvertTextToTranslation, formState.text, primaryLocale, translationKeySet]
  );

  useEffect(() => {
    if (!translationBindingRequest) {
      return;
    }
    if (!selectedPath || !widgetPathsEqual(selectedPath, translationBindingRequest.path)) {
      return;
    }
    openTranslationBindingForm(translationBindingRequest.suggestedKey);
    clearTranslationBindingRequest();
  }, [clearTranslationBindingRequest, openTranslationBindingForm, selectedPath, translationBindingRequest]);

  useEffect(() => {
    if (!translationFormOpen) {
      return;
    }
    const focusInput = () => {
      translationKeyInputRef.current?.focus();
      translationKeyInputRef.current?.select();
    };
    if (typeof window === "undefined") {
      focusInput();
      return;
    }
    const frame = window.requestAnimationFrame(focusInput);
    return () => window.cancelAnimationFrame(frame);
  }, [translationFormOpen]);

  useEffect(() => {
    if (assetModalOpen) {
      trackAssetEvent("asset_modal_opened");
    }
  }, [assetModalOpen, trackAssetEvent]);

  useEffect(() => {
    if (!assetModalOpen) {
      return;
    }
    if (assetCatalogLoadedAt !== null) {
      return;
    }
    loadAssetCatalog();
  }, [assetModalOpen, assetCatalogLoadedAt, loadAssetCatalog]);

  const fieldIssues = (field: string) => {
    if (!selectedIssuePath) {
      return [] as ValidationIssue[];
    }
    const pathBase = `${selectedIssuePath}/${field}`;
    return widgetIssues.filter(
      (issue) => issue.path === pathBase || issue.path.startsWith(`${pathBase}/`)
    );
  };

  const fieldSeverity = (field: string): "error" | "warning" | null => {
    const matches = fieldIssues(field);
    if (!matches.length) {
      return null;
    }
    if (matches.some((issue) => issue.severity === "error")) {
      return "error";
    }
    return "warning";
  };

  const renderFieldLabel = (label: string, field: string) => {
    const severity = fieldSeverity(field);
    const message = fieldIssues(field)[0]?.message;
    return (
      <div className="field-label">
        <span>{label}</span>
        {severity && message && (
          <span className={`field-badge ${severity}`}>
            {severity === "error" ? "!" : "⚠"} {message}
          </span>
        )}
      </div>
    );
  };

  const handleFieldChange = (field: keyof typeof formState, value: string) => {
    if (field === "style") {
      applyStyleValue(value);
      return;
    }
    setFormState((prev) => ({ ...prev, [field]: value }));
    if (!selectedPath || !selectedWidget) {
      return;
    }
    if (JSON_FIELDS.includes(field as keyof WidgetNode)) {
      return;
    }
    updateWidget(selectedPath, { [field]: value } as Partial<WidgetNode>);
  };

  const handleJsonBlur = (field: string) => {
    if (!selectedPath) {
      return;
    }
    try {
      const parsed = JSON.parse(formState[field as keyof typeof formState] || "{}");
      updateWidget(selectedPath, { [field]: parsed } as Partial<WidgetNode>);
      setJsonErrors((prev) => ({ ...prev, [field]: "" }));
    } catch {
      setJsonErrors((prev) => ({ ...prev, [field]: "Invalid JSON" }));
    }
  };

  const applyJsonTemplate = (field: typeof JSON_FIELDS[number], template: Record<string, unknown>) => {
    const formatted = JSON.stringify(template, null, 2);
    setFormState((prev) => ({ ...prev, [field]: formatted }));
    if (selectedPath) {
      updateWidget(selectedPath, { [field]: template } as Partial<WidgetNode>);
      setJsonErrors((prev) => ({ ...prev, [field]: "" }));
    }
  };

  const handleBrowseAssets = useCallback(() => {
    setAssetModalOpen(true);
    trackAssetEvent("asset_modal_open_request");
  }, [trackAssetEvent]);

  const handleCloseAssetModal = useCallback(() => {
    setAssetModalOpen(false);
    trackAssetEvent("asset_modal_closed");
  }, [trackAssetEvent]);

  const handlePickAsset = useCallback((asset: string) => {
    applyAssetValue(asset);
    setAssetModalOpen(false);
    trackAssetEvent("asset_selected", { path: asset });
  }, [applyAssetValue, trackAssetEvent]);

  const handleAssetCardKeyDown = useCallback((event: KeyboardEvent<HTMLDivElement>, assetPath: string) => {
    if (event.key === "Enter" || event.key === " ") {
      event.preventDefault();
      handlePickAsset(assetPath);
    }
  }, [handlePickAsset]);

  const handleOpenAssetManager = useCallback((asset: AssetReference) => {
    setAssetFilters((prev) => ({ ...prev, query: asset.path }));
    setAssetModalOpen(false);
    trackAssetEvent("asset_open_manager_jump", { assetId: asset.id, path: asset.path });
    if (typeof window !== "undefined") {
      window.requestAnimationFrame(() => {
        const node = document.getElementById("asset-manager");
        node?.scrollIntoView({ behavior: "smooth", block: "start" });
      });
    }
  }, [setAssetFilters, trackAssetEvent]);

  const handleStartTranslationBinding = () => {
    openTranslationBindingForm();
  };

  const handleCancelTranslationBinding = () => {
    setTranslationFormOpen(false);
    setTranslationKeyDraft("");
    setTranslationFormError(null);
  };

  const handleCommitTranslationBinding = () => {
    if (!translationFormOpen || !selectedPath) {
      return;
    }
    const key = translationKeyDraft.trim();
    if (!key) {
      setTranslationFormError("Enter a translation key");
      return;
    }
    setTranslationFormError(null);
    addTranslationKey(key);
    if (primaryLocale) {
      const existingValue = project.translations?.[primaryLocale]?.entries?.[key];
      if (!existingValue || !existingValue.trim().length) {
        updateTranslationValue(primaryLocale, key, formState.text.trim());
      }
    }
    handleFieldChange("text", buildTranslationExpression(key));
    setTranslationFormOpen(false);
    setTranslationKeyDraft("");
    setShortcutMessage(`Bound text to ${key}`);
  };

  const handleDetachTranslationBinding = () => {
    if (!currentTranslationKey) {
      return;
    }
    const fallback = primaryLocale
      ? project.translations?.[primaryLocale]?.entries?.[currentTranslationKey] ?? currentTranslationKey
      : currentTranslationKey;
    handleFieldChange("text", fallback);
    setShortcutMessage(`Detached ${currentTranslationKey}`);
  };

  const handleRevealTranslationManager = () => {
    if (!currentTranslationKey) {
      return;
    }
    requestTranslationFocus(currentTranslationKey, { origin: "inspector" });
    if (typeof window === "undefined") {
      return;
    }
    window.requestAnimationFrame(() => {
      const node = document.getElementById("translation-manager");
      node?.scrollIntoView({ behavior: "smooth", block: "start" });
    });
  };

  const handleCopyTranslationBinding = async () => {
    if (!currentTranslationKey) {
      return;
    }
    try {
      await navigator.clipboard.writeText(buildTranslationExpression(currentTranslationKey));
      setShortcutMessage("Copied translation reference");
    } catch (error) {
      console.warn("Unable to copy translation reference", error);
    }
  };

  if (!selectedWidget) {
    return (
      <section className="panel inspector" style={style}>
        <p className="section-title">Inspector</p>
        <p style={{ color: "#94a3b8" }}>Select a widget to edit its properties.</p>
      </section>
    );
  }

  return (
    <section className="panel inspector" style={style}>
      <p className="section-title">Inspector</p>
      {widgetIssues.length > 0 && (
        <div className="inspector-issues">
          {widgetIssues.map((issue) => (
            <span key={`${issue.path}-${issue.message}`} className={`inspector-issue ${issue.severity}`}>
              {issue.severity === "error" ? "!" : "⚠"} {issue.message}
            </span>
          ))}
        </div>
      )}
      <div className="properties-grid">
        <label className={`inspector-field ${fieldSeverity("id") ?? ""}`}>
          {renderFieldLabel("ID", "id")}
          <input className="input-field" value={formState.id} onChange={(event) => handleFieldChange("id", event.target.value)} />
        </label>
        <label className={`inspector-field ${fieldSeverity("text") ?? ""}`}>
          {renderFieldLabel("Text", "text")}
          <input className="input-field" value={formState.text} onChange={(event) => handleFieldChange("text", event.target.value)} />
          <div className="translation-text-tools">
            {currentTranslationKey ? (
              <>
                <div className="translation-text-tools__status">
                  <span>
                    Bound to <code>{currentTranslationKey}</code>
                  </span>
                  {primaryLocale && (
                    <span className="translation-text-tools__preview">
                      {primaryLocale}: {project.translations?.[primaryLocale]?.entries?.[currentTranslationKey] ?? "—"}
                    </span>
                  )}
                </div>
                <div className="translation-text-tools__actions">
                  <button type="button" className="button tertiary" onClick={handleCopyTranslationBinding}>
                    Copy reference
                  </button>
                  <button type="button" className="button tertiary" onClick={handleRevealTranslationManager}>
                    Open translations
                  </button>
                  <button type="button" className="button tertiary" onClick={handleDetachTranslationBinding}>
                    Detach
                  </button>
                </div>
              </>
            ) : translationFormOpen ? (
              <div className="translation-text-tools__form">
                <input
                  ref={translationKeyInputRef}
                  className="input-field translation-text-tools__input"
                  value={translationKeyDraft}
                  onChange={(event) => setTranslationKeyDraft(event.target.value)}
                  placeholder="example.section.label"
                />
                <div className="translation-text-tools__actions">
                  <button type="button" className="button primary" onClick={handleCommitTranslationBinding}>
                    Convert
                  </button>
                  <button type="button" className="button tertiary" onClick={handleCancelTranslationBinding}>
                    Cancel
                  </button>
                </div>
              </div>
            ) : (
              <button
                type="button"
                className="button tertiary"
                onClick={handleStartTranslationBinding}
                disabled={!canConvertTextToTranslation || !primaryLocale}
              >
                Convert text to translation key
              </button>
            )}
            {translationFormError && <span className="field-hint error-text">{translationFormError}</span>}
            {!primaryLocale && (
              <span className="field-hint warning-text">Add a locale before binding text.</span>
            )}
          </div>
        </label>
        <label className={`inspector-field ${fieldSeverity("src") ?? ""}`}>
          {renderFieldLabel("Source / Asset", "src")}
          <div className="input-with-addon">
            <input
              className="input-field"
              value={formState.src}
              list="asset-hints"
              onChange={(event) => handleFieldChange("src", event.target.value)}
            />
            <button type="button" className="button tertiary" onClick={handleBrowseAssets}>
              Browse
            </button>
          </div>
          <span className="field-hint">Browse scans the project for previously used media so you can re-use paths confidently.</span>
        </label>
        <label className={`inspector-field ${fieldSeverity("style") ?? ""}`}>
          {renderFieldLabel("Style", "style")}
          <input
            className="input-field"
            value={formState.style}
            list="style-options"
            placeholder={styleOptions.length ? "Select or type a style name" : "No styles defined"}
            onChange={(event) => handleFieldChange("style", event.target.value)}
          />
          {formState.style && !selectedStyleToken && (
            <span className="field-hint warning-text">Style not found. Manage tokens from the Style Manager.</span>
          )}
          {selectedStyleToken && (
            <>
              <div className="inspector-style-chip">
                <span
                  className="inspector-style-chip__swatch"
                  style={{
                    background: getStyleColor(selectedStyleToken, "backgroundColor", "#e2e8f0"),
                    color: getStyleColor(selectedStyleToken, "color", "#0f172a"),
                  }}
                >
                  Aa
                </span>
                <div className="inspector-style-chip__meta">
                  <strong>{selectedStyleToken.name}</strong>
                  <span>{selectedStyleToken.category}</span>
                  {selectedStyleToken.description && <span>{selectedStyleToken.description}</span>}
                  {selectedStyleToken.tags?.length ? (
                    <span className="inspector-style-chip__tags">{selectedStyleToken.tags.join(", ")}</span>
                  ) : (
                    <span className="inspector-style-chip__tags">No tags</span>
                  )}
                  {styleValuePairs.length > 0 && (
                    <div className="inspector-style-chip__values">
                      {styleValuePairs.map(([key, value]) => (
                        <span key={key}>
                          {key}: {typeof value === "string" || typeof value === "number" ? String(value) : "…"}
                        </span>
                      ))}
                    </div>
                  )}
                  <button type="button" className="button tertiary inspector-style-chip__action" onClick={handleFocusStyle}>
                    Edit in Style Manager
                  </button>
                </div>
              </div>
              <StylePreviewCard
                token={selectedStyleToken}
                preview={stylePreviewData}
                busy={stylePreviewBusy}
                error={stylePreviewError}
                emptyHint="Preview appears once our preview service responds."
                footnote="Swapping widgets keeps this preview in sync."
              />
            </>
          )}
          {Object.keys(project.styles ?? {}).length > 0 && (
            <div className="style-quick-actions">
              <div className="style-quick-actions__controls">
                <span>Quick picks</span>
                <select
                  className="select-field"
                  value={styleCategoryFilter}
                  onChange={(event) => setStyleCategoryFilter(event.target.value as StyleCategory | "all")}
                >
                  {STYLE_CATEGORY_OPTIONS.map((category) => (
                    <option value={category} key={category}>
                      {category === "all" ? "All categories" : category}
                    </option>
                  ))}
                </select>
                <button
                  type="button"
                  className="button tertiary"
                  onClick={handleShuffleStyle}
                  disabled={!Object.keys(project.styles ?? {}).length}
                >
                  Shuffle style
                </button>
              </div>
              <div className="style-chip-row" aria-label="Recent styles">
                {recentStyles.length ? (
                  recentStyles.map((styleKey) => {
                    const label = project.styles?.[styleKey]?.name ?? styleKey;
                    return (
                      <button type="button" key={styleKey} className="style-chip" onClick={() => handleStyleQuickPick(styleKey)}>
                        {label}
                      </button>
                    );
                  })
                ) : (
                  <span className="field-hint">No recent styles yet.</span>
                )}
              </div>
              <div className="style-shortcut-hint">
                {shortcutMessage ? (
                  <span className="shortcut-toast">{shortcutMessage}</span>
                ) : (
                  <span className="field-hint">Press Ctrl/⌘ + Shift + 1-6 to apply recent styles.</span>
                )}
              </div>
              <div className="style-chip-row" aria-label="Suggested styles">
                {filteredStyles.length ? (
                  filteredStyles.map(({ key, token }) => (
                    <button type="button" key={key} className="style-chip" onClick={() => handleStyleQuickPick(key)}>
                      {token.name}
                    </button>
                  ))
                ) : (
                  <span className="field-hint">No styles match that filter.</span>
                )}
              </div>
            </div>
          )}
        </label>
        {JSON_FIELDS.map((field) => (
          <label key={field} className={`inspector-field ${fieldSeverity(field) ?? ""}`}>
            {renderFieldLabel(field.toUpperCase(), field)}
            <textarea
              className="textarea-field"
              rows={3}
              value={formState[field]}
              onChange={(event) => handleFieldChange(field, event.target.value)}
              onBlur={() => handleJsonBlur(field)}
            />
            {jsonErrors[field] && <span style={{ color: "#f43f5e", fontSize: "0.8rem" }}>{jsonErrors[field]}</span>}
            {field === "props" && (
              <div className="structured-editor">
                <div className="structured-editor__header">
                  <span>Guided props</span>
                  <button
                    type="button"
                    className="button tertiary"
                    onClick={handleAddProp}
                    disabled={!structuredProps.valid}
                  >
                    Add prop
                  </button>
                </div>
                {!structuredProps.valid ? (
                  <span className="field-hint warning-text">Fix the JSON above to use the guided editor.</span>
                ) : Object.keys(structuredProps.value).length === 0 ? (
                  <span className="field-hint">No props defined yet.</span>
                ) : (
                  Object.entries(structuredProps.value).map(([propName, propValue]) => (
                    <div key={`prop-${propName}`} className="structured-row">
                      <div className="structured-row__body">
                        <div className="structured-row__item">
                          <input
                            className="input-field input-field--dense"
                            defaultValue={propName}
                            aria-label="Prop name"
                            onBlur={(event) => handleRenameProp(propName, event.target.value)}
                            onKeyDown={(event) => {
                              if (event.key === "Enter") {
                                event.currentTarget.blur();
                              }
                            }}
                          />
                        </div>
                        <div className="structured-row__item structured-row__item--grow">
                          <input
                            className="input-field input-field--dense"
                            value={typeof propValue === "string" ? propValue : JSON.stringify(propValue)}
                            aria-label={`${propName} value`}
                            onChange={(event) => handleUpdatePropValue(propName, event.target.value)}
                          />
                        </div>
                        <button type="button" className="button tertiary" onClick={() => handleRemoveProp(propName)}>
                          Remove
                        </button>
                      </div>
                    </div>
                  ))
                )}
              </div>
            )}
            {field === "events" && (
              <>
                <div className="helper-actions">
                  {EVENT_TEMPLATES.map((template) => (
                    <button
                      type="button"
                      key={template.label}
                      className="button tertiary"
                      onClick={() => applyJsonTemplate("events", template.value)}
                    >
                      {template.label}
                    </button>
                  ))}
                </div>
                <div className="structured-editor">
                  <div className="structured-editor__header">
                    <span>Guided events</span>
                    <button
                      type="button"
                      className="button tertiary"
                      onClick={handleAddEventEntry}
                      disabled={!structuredEvents.valid}
                    >
                      Add event
                    </button>
                  </div>
                  {!structuredEvents.valid ? (
                    <span className="field-hint warning-text">Fix the JSON above to use the guided editor.</span>
                  ) : Object.keys(structuredEvents.value).length === 0 ? (
                    <span className="field-hint">No events defined yet.</span>
                  ) : (
                    Object.entries(structuredEvents.value).map(([eventName, handlers]) => (
                      <div key={`event-${eventName}`} className="structured-row">
                        <div className="structured-row__header">
                          <input
                            className="input-field input-field--dense"
                            defaultValue={eventName}
                            aria-label="Event name"
                            onBlur={(event) => handleRenameEvent(eventName, event.target.value)}
                            onKeyDown={(event) => {
                              if (event.key === "Enter") {
                                event.currentTarget.blur();
                              }
                            }}
                          />
                          <button type="button" className="button tertiary" onClick={() => handleRemoveEvent(eventName)}>
                            Remove
                          </button>
                        </div>
                        <div className="structured-row__body">
                          {handlers.map((handler, index) => (
                            <div key={`${eventName}-handler-${index}`} className="structured-row__item">
                              <input
                                className="input-field input-field--dense"
                                value={handler}
                                aria-label={`${eventName} action ${index + 1}`}
                                onChange={(event) => handleUpdateEventAction(eventName, index, event.target.value)}
                              />
                              <button
                                type="button"
                                className="button tertiary"
                                onClick={() => handleRemoveEventAction(eventName, index)}
                              >
                                Remove
                              </button>
                            </div>
                          ))}
                        </div>
                        <button type="button" className="button tertiary" onClick={() => handleAddEventAction(eventName)}>
                          Add action
                        </button>
                      </div>
                    ))
                  )}
                </div>
              </>
            )}
            {field === "bindings" && (
              <div className="structured-editor">
                <div className="structured-editor__header">
                  <span>Guided bindings</span>
                  <button
                    type="button"
                    className="button tertiary"
                    onClick={handleAddBinding}
                    disabled={!structuredBindings.valid}
                  >
                    Add binding
                  </button>
                </div>
                {!structuredBindings.valid ? (
                  <span className="field-hint warning-text">Fix the JSON above to use the guided editor.</span>
                ) : Object.keys(structuredBindings.value).length === 0 ? (
                  <span className="field-hint">No bindings defined yet.</span>
                ) : (
                  Object.entries(structuredBindings.value).map(([bindingName, expression]) => (
                    <div key={`binding-${bindingName}`} className="structured-row">
                      <div className="structured-row__body">
                        <div className="structured-row__item">
                          <input
                            className="input-field input-field--dense"
                            defaultValue={bindingName}
                            aria-label="Binding name"
                            onBlur={(event) => handleRenameBinding(bindingName, event.target.value)}
                            onKeyDown={(event) => {
                              if (event.key === "Enter") {
                                event.currentTarget.blur();
                              }
                            }}
                          />
                        </div>
                        <div className="structured-row__item structured-row__item--grow">
                          <input
                            className="input-field input-field--dense"
                            value={expression}
                            aria-label={`${bindingName} expression`}
                            onChange={(event) => handleUpdateBindingValue(bindingName, event.target.value)}
                          />
                        </div>
                        <button type="button" className="button tertiary" onClick={() => handleRemoveBinding(bindingName)}>
                          Remove
                        </button>
                      </div>
                    </div>
                  ))
                )}
              </div>
            )}
            {field === "accessibility" && (
              <div className="structured-editor">
                <div className="structured-editor__header">
                  <span>Guided accessibility</span>
                  <button
                    type="button"
                    className="button tertiary"
                    onClick={handleAddAccessibilityEntry}
                    disabled={!structuredAccessibility.valid}
                  >
                    Add entry
                  </button>
                </div>
                {!structuredAccessibility.valid ? (
                  <span className="field-hint warning-text">Fix the JSON above to use the guided editor.</span>
                ) : Object.keys(structuredAccessibility.value).length === 0 ? (
                  <span className="field-hint">No accessibility hints yet.</span>
                ) : (
                  Object.entries(structuredAccessibility.value).map(([name, value]) => (
                    <div key={`accessibility-${name}`} className="structured-row">
                      <div className="structured-row__body">
                        <div className="structured-row__item">
                          <input
                            className="input-field input-field--dense"
                            defaultValue={name}
                            aria-label="Accessibility key"
                            onBlur={(event) => handleRenameAccessibilityEntry(name, event.target.value)}
                            onKeyDown={(event) => {
                              if (event.key === "Enter") {
                                event.currentTarget.blur();
                              }
                            }}
                          />
                        </div>
                        <div className="structured-row__item structured-row__item--grow">
                          <input
                            className="input-field input-field--dense"
                            value={value}
                            aria-label={`${name} value`}
                            onChange={(event) => handleUpdateAccessibilityValue(name, event.target.value)}
                          />
                        </div>
                        <button type="button" className="button tertiary" onClick={() => handleRemoveAccessibilityEntry(name)}>
                          Remove
                        </button>
                      </div>
                    </div>
                  ))
                )}
              </div>
            )}
          </label>
        ))}
      </div>
      <datalist id="style-options">
        {styleOptions.map((styleName) => (
          <option value={styleName} key={styleName} />
        ))}
      </datalist>
      <datalist id="asset-hints">
        {assetHintOptions.map((asset) => (
          <option value={asset} key={asset} />
        ))}
      </datalist>
      {assetModalOpen && (
        <div className="modal-overlay asset-modal" role="dialog" aria-modal="true">
          <div className="modal asset-modal__panel">
            <div className="modal__header asset-modal__header">
              <div>
                <p className="section-title" style={{ marginBottom: 0 }}>
                  Asset Library
                </p>
                <span className="field-hint">Powered by the new asset catalog service.</span>
              </div>
              <div className="asset-modal__header-actions">
                <span className="asset-modal__sync">
                  {assetCatalogLoadedAt ? `Synced ${new Date(assetCatalogLoadedAt).toLocaleTimeString()}` : "Scan runs on demand"}
                </span>
                <button type="button" className="button tertiary" onClick={handleCloseAssetModal}>
                  Close
                </button>
              </div>
            </div>
            <div className="modal__body asset-modal__body">
              <div className="asset-modal__actions">
                <div className="asset-modal__search-row">
                  <input
                    type="search"
                    className="input-field"
                    placeholder={assetCatalog.length ? "Search by filename, tags, or targets" : "Search activates once assets sync"}
                    value={assetFilters.query}
                    disabled={!assetCatalog.length && !assetCatalogBusy}
                    onChange={(event) => handleSearchQueryChange(event.target.value)}
                  />
                  <button type="button" className="button tertiary" onClick={handleAssetModalRefresh} disabled={assetCatalogBusy}>
                    {assetCatalogBusy ? "Scanning…" : assetCatalogLoadedAt ? "Refresh catalog" : "Scan project"}
                  </button>
                </div>
                <div
                  className={`asset-upload-dropzone ${isDroppingAsset ? "is-dragging" : ""}`}
                  onDragOver={handleAssetDragOver}
                  onDragLeave={handleAssetDragLeave}
                  onDrop={handleAssetDrop}
                >
                  <div>
                    <strong>Drop files here</strong>
                    <p>
                      or
                      {" "}
                      <button type="button" className="button link-button" onClick={handleFilePickerClick}>
                        browse your computer
                      </button>
                      {" "}
                      to upload.
                    </p>
                  </div>
                  <span className="field-hint">We store assets under YAMUI_ASSET_ROOT and refresh the catalog automatically.</span>
                  <input
                    ref={assetFileInputRef}
                    type="file"
                    multiple
                    className="asset-upload-input"
                    onChange={handleFileInputChange}
                  />
                </div>
              </div>
              <AssetUploadQueue uploads={pendingUploads} onDismiss={handleDismissUpload} />
              {assetCatalogBusy && <span className="field-hint">Scanning project for asset references…</span>}
              {assetCatalogError && <span className="field-hint warning-text">{assetCatalogError}</span>}
              <div className="asset-filter-bar">
                <div className="asset-filter-chips">
                  {assetFilters.tags.map((tag) => (
                    <button type="button" key={`tag-${tag}`} className="filter-chip" onClick={() => handleRemoveFilter("tags", tag)}>
                      Tag #{tag}
                      <span aria-hidden="true">×</span>
                    </button>
                  ))}
                  {assetFilters.targets.map((target) => (
                    <button type="button" key={`target-${target}`} className="filter-chip" onClick={() => handleRemoveFilter("targets", target)}>
                      {formatAssetTarget(target)}
                      <span aria-hidden="true">×</span>
                    </button>
                  ))}
                  {filtersActive ? (
                    <button type="button" className="button tertiary" onClick={handleClearFilters}>
                      Clear filters
                    </button>
                  ) : (
                    <span className="field-hint">Use search, tags, targets, or type toggles to refine results.</span>
                  )}
                </div>
                <div className="asset-kind-toggle" role="group" aria-label="Filter by asset type">
                  {ASSET_KIND_OPTIONS.map((kind) => (
                    <button
                      type="button"
                      key={kind}
                      className={`filter-chip filter-chip--toggle ${assetFilters.kinds.includes(kind) ? "is-active" : ""}`}
                      onClick={() => handleToggleKindFilter(kind)}
                    >
                      {kind}
                    </button>
                  ))}
                </div>
              </div>
              {assetCatalog.length ? (
                visibleAssetCatalog.length ? (
                  <div className="asset-grid asset-grid--rich">
                    {visibleAssetCatalog.map((asset) => {
                      const tagValue = tagDrafts[asset.id] ?? asset.tags.join(", ");
                      return (
                        <div
                          key={asset.id}
                          className="asset-card asset-card--rich"
                          role="button"
                          tabIndex={0}
                          onClick={() => handlePickAsset(asset.path)}
                          onKeyDown={(event) => handleAssetCardKeyDown(event, asset.path)}
                          aria-label={`Insert ${asset.label}`}
                        >
                          <div className="asset-card__header">
                            <div>
                              <span className="asset-card__label">{asset.label}</span>
                              <span className="asset-card__path">{asset.path}</span>
                            </div>
                            <div className="asset-card__pill-group">
                              <span className="asset-card__badge">{asset.kind}</span>
                              <span className="asset-card__size">{formatBytes(asset.sizeBytes)}</span>
                            </div>
                          </div>
                          <div className="asset-card__preview-wrapper">
                            {asset.thumbnailUrl || asset.previewUrl ? (
                              <div
                                className="asset-card__preview"
                                style={{ backgroundImage: `url(${asset.thumbnailUrl ?? asset.previewUrl})` }}
                              />
                            ) : (
                              <div className="asset-card__preview asset-card__preview--empty">No preview available</div>
                            )}
                          </div>
                          <div className="asset-card__meta">
                            <span>
                              {asset.usageCount} {asset.usageCount === 1 ? "use" : "uses"}
                            </span>
                            <span>
                              {asset.targets.length
                                ? `${asset.targets.length} target${asset.targets.length === 1 ? "" : "s"}`
                                : "Not applied yet"}
                            </span>
                          </div>
                          <div className="asset-card__targets">
                            {asset.targets.length ? (
                              asset.targets.slice(0, 4).map((target) => (
                                <button
                                  type="button"
                                  key={`${asset.id}-${target}`}
                                  className="asset-target-chip"
                                  onClick={(event) => {
                                    event.stopPropagation();
                                    handleAddTargetFilter(target);
                                  }}
                                >
                                  {formatAssetTarget(target)}
                                </button>
                              ))
                            ) : (
                              <span className="asset-target-chip asset-target-chip--empty">No targets</span>
                            )}
                          </div>
                          <div className="asset-card__tags">
                            {asset.tags.length ? (
                              asset.tags.map((tag) => (
                                <button
                                  type="button"
                                  key={`${asset.id}-${tag}`}
                                  className="asset-tag"
                                  onClick={(event) => {
                                    event.stopPropagation();
                                    handleAddFilterTag(tag);
                                  }}
                                >
                                  #{tag}
                                </button>
                              ))
                            ) : (
                              <span className="asset-tag asset-tag--empty">No tags yet</span>
                            )}
                          </div>
                          <div className="asset-card__tag-editor">
                            <input
                              type="text"
                              className="input-field asset-tag-input"
                              value={tagValue}
                              placeholder="Add comma separated tags"
                              disabled={tagBusyMap[asset.id]}
                              onClick={(event) => event.stopPropagation()}
                              onChange={(event) => handleTagInputChange(asset.id, event.target.value)}
                              onBlur={(event) => void commitAssetTags(asset, event.target.value)}
                              onKeyDown={(event) => handleTagInputKeyDown(event, asset)}
                            />
                            {tagBusyMap[asset.id] && <span className="asset-tag-spinner">Saving…</span>}
                          </div>
                          <button
                            type="button"
                            className="button tertiary asset-card__jump"
                            onClick={(event) => {
                              event.stopPropagation();
                              handleOpenAssetManager(asset);
                            }}
                          >
                            Open in Asset Manager
                          </button>
                          <span className="asset-card__action">Use asset</span>
                        </div>
                      );
                    })}
                  </div>
                ) : (
                  <div className="asset-catalog-empty">
                    <span>No assets match “{assetFilters.query}”.</span>
                  </div>
                )
              ) : (
                <>
                  <p className="field-hint">We fall back to local suggestions until the backend finds asset references.</p>
                  <div className="asset-grid">
                    {assetGrid.length ? (
                      assetGrid.map((asset) => (
                        <button type="button" key={asset} className="asset-card" onClick={() => handlePickAsset(asset)}>
                          <span className="asset-card__label">{asset}</span>
                          <span className="asset-card__action">Use asset</span>
                        </button>
                      ))
                    ) : (
                      <span className="field-hint">No assets detected yet. Import media to populate this list.</span>
                    )}
                  </div>
                </>
              )}
            </div>
            <div className="modal__footer asset-modal__footer">
              <span className="field-hint">Set the YAMUI_ASSET_ROOT env var to pull file sizes and existence metadata.</span>
            </div>
          </div>
        </div>
      )}
    </section>
  );
}
