import { useCallback, useEffect, useMemo, useRef, useState, type CSSProperties, type ChangeEvent, type DragEvent, type KeyboardEvent } from "react";
import { useProject } from "../context/ProjectContext";
import { AssetReference, ProjectModel, StyleCategory, StylePreview, StyleTokenModel, ValidationIssue, WidgetNode, WidgetPath } from "../types/yamui";
import { fetchAssetCatalog, previewStyle, updateAssetTags, uploadAsset } from "../utils/api";
import { emitTelemetry } from "../utils/telemetry";
import StylePreviewCard from "./StylePreviewCard";

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

type AssetFilterState = {
  tags: string[];
  targets: string[];
  kinds: AssetReference["kind"][];
};

type PendingUpload = {
  id: string;
  fileName: string;
  status: "uploading" | "error";
  error?: string;
};

const DEFAULT_ASSET_FILTERS: AssetFilterState = {
  tags: [],
  targets: [],
  kinds: [],
};

const ASSET_KIND_OPTIONS: AssetReference["kind"][] = ["image", "video", "audio", "font", "binary", "unknown"];

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

const createClientId = (): string => {
  if (typeof crypto !== "undefined" && "randomUUID" in crypto) {
    return crypto.randomUUID();
  }
  return Math.random().toString(36).slice(2, 10);
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

interface PropertyInspectorProps {
  issues: ValidationIssue[];
  style?: CSSProperties;
}

export default function PropertyInspector({ issues, style }: PropertyInspectorProps): JSX.Element {
  const { project, editorTarget, selectedPath, updateWidget, setStyleEditorSelection } = useProject();
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
  const [assetCatalog, setAssetCatalog] = useState<AssetReference[]>([]);
  const [assetCatalogBusy, setAssetCatalogBusy] = useState(false);
  const [assetCatalogError, setAssetCatalogError] = useState<string | null>(null);
  const [assetCatalogLoadedAt, setAssetCatalogLoadedAt] = useState<number | null>(null);
  const [assetSearch, setAssetSearch] = useState("");
  const [assetFilters, setAssetFilters] = useState<AssetFilterState>(() => ({ ...DEFAULT_ASSET_FILTERS }));
  const [pendingUploads, setPendingUploads] = useState<PendingUpload[]>([]);
  const [isDroppingAsset, setIsDroppingAsset] = useState(false);
  const [tagDrafts, setTagDrafts] = useState<Record<string, string>>({});
  const [tagBusyMap, setTagBusyMap] = useState<Record<string, boolean>>({});

  const assetFileInputRef = useRef<HTMLInputElement | null>(null);

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
    if (!selectedStyleToken) {
      return;
    }
    setStyleEditorSelection(selectedStyleToken.name);
    const target = document.getElementById("style-manager");
    if (target) {
      target.scrollIntoView({ behavior: "smooth", block: "start" });
    }
  };

  const styleOptions = useMemo(() => Object.keys(project.styles ?? {}), [project.styles]);
  const assetOptions = useMemo(() => collectAssetSuggestions(project), [project]);
  const assetGrid = useMemo(() => assetOptions.slice(0, 12), [assetOptions]);
  const filteredAssetCatalog = useMemo(() => {
    if (!assetCatalog.length) {
      return [] as AssetReference[];
    }
    const query = assetSearch.trim().toLowerCase();
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
  }, [assetCatalog, assetFilters, assetSearch]);
  const visibleAssetCatalog = useMemo(() => filteredAssetCatalog.slice(0, 60), [filteredAssetCatalog]);
  const filtersActive = assetFilters.tags.length + assetFilters.targets.length + assetFilters.kinds.length > 0;
  const pendingUploadsActive = pendingUploads.length > 0;
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

  const loadAssetCatalog = useCallback(async () => {
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
  }, [project]);

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
  }, [trackAssetEvent]);

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
  }, [trackAssetEvent]);

  const handleRemoveFilter = useCallback((group: keyof AssetFilterState, value: string) => {
    setAssetFilters((prev) => {
      const nextGroup = prev[group].filter((entry) => entry !== value);
      return { ...prev, [group]: nextGroup } as AssetFilterState;
    });
    trackAssetEvent("asset_filter_remove", { group, value });
  }, [trackAssetEvent]);

  const handleToggleKindFilter = useCallback((kind: AssetReference["kind"]) => {
    setAssetFilters((prev) => {
      const exists = prev.kinds.includes(kind);
      const nextKinds = exists ? prev.kinds.filter((value) => value !== kind) : [...prev.kinds, kind];
      trackAssetEvent("asset_filter_toggle", { type: "kind", value: kind, active: !exists });
      return { ...prev, kinds: nextKinds };
    });
  }, [trackAssetEvent]);

  const handleClearFilters = useCallback(() => {
    setAssetFilters({ ...DEFAULT_ASSET_FILTERS });
    trackAssetEvent("asset_filters_cleared");
  }, [trackAssetEvent]);

  const uploadSingleAsset = useCallback(async (file: File) => {
    const pendingId = createClientId();
    setPendingUploads((prev) => [...prev, { id: pendingId, fileName: file.name, status: "uploading" }]);
    try {
      trackAssetEvent("asset_upload_start", { fileName: file.name, size: file.size });
      const uploaded = await uploadAsset(file);
      setAssetCatalog((prev) => [uploaded, ...prev.filter((item) => item.id !== uploaded.id)]);
      setTagDrafts((prev) => ({ ...prev, [uploaded.id]: uploaded.tags.join(", ") }));
      trackAssetEvent("asset_upload_success", { assetId: uploaded.id, kind: uploaded.kind });
      setPendingUploads((prev) => prev.filter((item) => item.id !== pendingId));
    } catch (error) {
      const message = error instanceof Error ? error.message : "Failed to upload asset";
      setPendingUploads((prev) => prev.map((item) => (item.id === pendingId ? { ...item, status: "error", error: message } : item)));
      trackAssetEvent("asset_upload_failure", { fileName: file.name, message });
    }
  }, [trackAssetEvent]);

  const handleFilesUpload = useCallback(async (files: File[]) => {
    if (!files.length) {
      return;
    }
    trackAssetEvent("asset_upload_batch_start", { count: files.length });
    await Promise.all(files.map((file) => uploadSingleAsset(file)));
    await loadAssetCatalog();
    trackAssetEvent("asset_upload_batch_complete", { count: files.length });
  }, [loadAssetCatalog, trackAssetEvent, uploadSingleAsset]);

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
  }, []);

  const handleAssetDragLeave = useCallback((event: DragEvent<HTMLDivElement>) => {
    event.preventDefault();
    setIsDroppingAsset(false);
  }, []);

  const handleAssetDrop = useCallback((event: DragEvent<HTMLDivElement>) => {
    event.preventDefault();
    setIsDroppingAsset(false);
    const files = Array.from(event.dataTransfer?.files ?? []);
    if (files.length) {
      void handleFilesUpload(files);
    }
  }, [handleFilesUpload]);

  const handleDismissUpload = useCallback((id: string) => {
    setPendingUploads((prev) => prev.filter((item) => item.id !== id));
  }, []);

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
    [project, trackAssetEvent]
  );

  const handleTagInputChange = useCallback((assetId: string, value: string) => {
    setTagDrafts((prev) => ({ ...prev, [assetId]: value }));
  }, []);

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
    const entries = Object.values(project.styles ?? {});
    const pool = styleCategoryFilter === "all"
      ? entries
      : entries.filter((token) => token.category === styleCategoryFilter);
    return pool.slice(0, 6);
  }, [project.styles, styleCategoryFilter]);

  const handleStyleQuickPick = useCallback(
    (name: string) => {
      applyStyleValue(name);
    },
    [applyStyleValue]
  );

  const handleShuffleStyle = () => {
    const pool = filteredStyles.length ? filteredStyles : Object.values(project.styles ?? {});
    if (!pool.length) {
      return;
    }
    const random = pool[Math.floor(Math.random() * pool.length)];
    applyStyleValue(random.name);
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
  }, [selectedWidget]);

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
    } catch (error) {
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
    setAssetSearch("");
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
                  recentStyles.map((name) => (
                    <button type="button" key={name} className="style-chip" onClick={() => handleStyleQuickPick(name)}>
                      {name}
                    </button>
                  ))
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
                  filteredStyles.map((token) => (
                    <button type="button" key={token.name} className="style-chip" onClick={() => handleStyleQuickPick(token.name)}>
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
            {field === "events" && (
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
                    value={assetSearch}
                    disabled={!assetCatalog.length && !assetCatalogBusy}
                    onChange={(event) => setAssetSearch(event.target.value)}
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
              {pendingUploadsActive && (
                <div className="asset-upload-pending">
                  {pendingUploads.map((upload) => (
                    <div key={upload.id} className={`asset-upload-pill asset-upload-pill--${upload.status}`}>
                      <div className="asset-upload-pill__meta">
                        <strong>{upload.fileName}</strong>
                        <span>{upload.status === "uploading" ? "Uploading…" : upload.error}</span>
                      </div>
                      {upload.status === "error" ? (
                        <button type="button" className="button tertiary" onClick={() => handleDismissUpload(upload.id)}>
                          Dismiss
                        </button>
                      ) : (
                        <span className="asset-upload-pill__spinner" aria-hidden="true" />
                      )}
                    </div>
                  ))}
                </div>
              )}
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
                          <span className="asset-card__action">Use asset</span>
                        </div>
                      );
                    })}
                  </div>
                ) : (
                  <div className="asset-catalog-empty">
                    <span>No assets match “{assetSearch}”.</span>
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
