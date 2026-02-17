import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { useProject, type EditorTarget } from "../context/ProjectContext";
import {
  ProjectModel,
  StyleCategory,
  StylePreview,
  StyleTokenModel,
  ValidationIssue,
  WidgetNode,
  WidgetPath,
} from "../types/yamui";
import { lintStyles, previewStyle } from "../utils/api";
import { emitTelemetry } from "../utils/telemetry";
import StylePreviewCard, { buildStylePreviewKnobs } from "./StylePreviewCard";

const CATEGORY_OPTIONS: StyleCategory[] = ["color", "surface", "text", "spacing", "shadow"];
const STYLE_FILTER_CATEGORIES: Array<StyleCategory | "all"> = ["all", ...CATEGORY_OPTIONS];
const MAX_USAGE_PREVIEW = 6;
const PREVIEW_DEBOUNCE_MS = 250;

interface FormState {
  name: string;
  category: StyleCategory;
  description: string;
  tagsText: string;
  valueText: string;
  metadataText: string;
}

const EMPTY_JSON = JSON.stringify({}, null, 2);

const defaultFormState = (): FormState => ({
  name: "",
  category: "surface",
  description: "",
  tagsText: "",
  valueText: EMPTY_JSON,
  metadataText: EMPTY_JSON,
});

function tokenToForm(token?: StyleTokenModel): FormState {
  if (!token) {
    return defaultFormState();
  }
  return {
    name: token.name,
    category: token.category,
    description: token.description ?? "",
    tagsText: token.tags?.join(", ") ?? "",
    valueText: JSON.stringify(token.value ?? {}, null, 2),
    metadataText: JSON.stringify(token.metadata ?? {}, null, 2),
  };
}

function generateStyleName(existing: Record<string, StyleTokenModel>): string {
  let index = Object.keys(existing).length + 1;
  let candidate = `style_${index}`;
  while (existing[candidate]) {
    index += 1;
    candidate = `style_${index}`;
  }
  return candidate;
}

function generateCloneName(baseName: string, existing: Record<string, StyleTokenModel>): string {
  const sanitizedBase = baseName.replace(/\s+/g, "_");
  let index = 1;
  let candidate = `${sanitizedBase}_copy`;
  while (existing[candidate]) {
    index += 1;
    candidate = `${sanitizedBase}_copy_${index}`;
  }
  return candidate;
}

function getColorValue(token: StyleTokenModel, key: "backgroundColor" | "color", fallback: string): string {
  const raw = token.value[key];
  return typeof raw === "string" ? raw : fallback;
}

interface StyleUsage {
  target: EditorTarget;
  path: WidgetPath;
  widget: WidgetNode;
}

type UsageFocusOrigin = "list_click" | "cycle" | "lint_jump";

interface LintDrawerEntry {
  name: string;
  issues: ValidationIssue[];
  errorCount: number;
  warningCount: number;
}

function buildStyleUsageMap(project: ProjectModel): Record<string, StyleUsage[]> {
  const map: Record<string, StyleUsage[]> = {};
  const visit = (widgets: WidgetNode[] | undefined, target: EditorTarget, trail: WidgetPath) => {
    if (!widgets) {
      return;
    }
    widgets.forEach((widget, index) => {
      const nextPath = [...trail, index];
      if (widget.style) {
        if (!map[widget.style]) {
          map[widget.style] = [];
        }
        map[widget.style]!.push({ target, path: nextPath, widget });
      }
      visit(widget.widgets, target, nextPath);
    });
  };
  Object.entries(project.screens).forEach(([id, screen]) => {
    visit(screen.widgets, { type: "screen", id }, []);
  });
  Object.entries(project.components).forEach(([id, component]) => {
    visit(component.widgets, { type: "component", id }, []);
  });
  return map;
}

function describeUsageLocation(usage: StyleUsage): { owner: string; widget: string; pathLabel: string } {
  const ownerType = usage.target.type === "screen" ? "Screen" : "Component";
  const owner = `${ownerType} • ${usage.target.id}`;
  const widgetId = usage.widget.id ? `#${usage.widget.id}` : "(no id)";
  const widget = `${usage.widget.type} ${widgetId}`;
  const pathLabel = usage.path.length ? usage.path.join(" / ") : "root";
  return { owner, widget, pathLabel };
}

export default function StyleManager(): JSX.Element {
  const {
    project,
    saveStyleToken,
    deleteStyleToken,
    styleEditorSelection,
    setStyleEditorSelection,
    selectWidget,
    setEditorTarget,
  } = useProject();
  const [formState, setFormState] = useState<FormState>(() => tokenToForm());
  const [formErrors, setFormErrors] = useState<FormErrors>({});
  const [draftPreview, setDraftPreview] = useState<StylePreview | null>(null);
  const [draftPreviewError, setDraftPreviewError] = useState<string | null>(null);
  const [draftLintIssues, setDraftLintIssues] = useState<ValidationIssue[]>([]);
  const [draftLintError, setDraftLintError] = useState<string | null>(null);
  const [isPreviewingDraft, setIsPreviewingDraft] = useState(false);
  const [isLintingDraft, setIsLintingDraft] = useState(false);
  const [autoPreview, setAutoPreview] = useState<StylePreview | null>(null);
  const [autoPreviewError, setAutoPreviewError] = useState<string | null>(null);
  const [autoPreviewBusy, setAutoPreviewBusy] = useState(false);
  const [styleLintMap, setStyleLintMap] = useState<Record<string, ValidationIssue[]>>({});
  const [lintScanBusy, setLintScanBusy] = useState(false);
  const [lintScanError, setLintScanError] = useState<string | null>(null);
  const [filterQuery, setFilterQuery] = useState("");
  const [filterCategory, setFilterCategory] = useState<StyleCategory | "all">("all");
  const [lintFilter, setLintFilter] = useState<"all" | "issues" | "errors">("all");
  const [usageCursor, setUsageCursor] = useState<Record<string, number>>({});
  const [usageOwnerFilter, setUsageOwnerFilter] = useState<"all" | "screen" | "component">("all");
  const [usageWidgetFilter, setUsageWidgetFilter] = useState<string>("all");
  const [showUnusedOnly, setShowUnusedOnly] = useState(false);
  const [valueCopyState, setValueCopyState] = useState<"idle" | "copied" | "error">("idle");
  const [valueFormatState, setValueFormatState] = useState<"idle" | "formatted" | "error">("idle");
  const [metadataCopyState, setMetadataCopyState] = useState<"idle" | "copied" | "error">("idle");
  const [metadataFormatState, setMetadataFormatState] = useState<"idle" | "formatted" | "error">("idle");
  const [isLintDrawerOpen, setIsLintDrawerOpen] = useState(false);
  const previewCacheRef = useRef<Map<string, StylePreview>>(new Map());

  const normalizedQuery = filterQuery.trim().toLowerCase();
  const styleUsageMap = useMemo(() => buildStyleUsageMap(project), [project]);
  const styleEntries = useMemo(() => {
    const entries = Object.entries(project.styles).map(([key, token]) => ({ key, token }));
    return entries
      .filter(({ token }) => {
        const matchesCategory = filterCategory === "all" || token.category === filterCategory;
        if (!matchesCategory) {
          return false;
        }
        const lintIssues = styleLintMap[token.name] ?? [];
        const matchesLintFilter =
          lintFilter === "all"
            ? true
            : lintFilter === "issues"
              ? lintIssues.length > 0
              : lintIssues.some((issue) => issue.severity === "error");
        if (!matchesLintFilter) {
          return false;
        }
        if (showUnusedOnly && (styleUsageMap[token.name]?.length ?? 0) > 0) {
          return false;
        }
        if (!normalizedQuery) {
          return true;
        }
        const inName = token.name.toLowerCase().includes(normalizedQuery);
        const inDescription = token.description?.toLowerCase().includes(normalizedQuery);
        const inTags = token.tags?.some((tag) => tag.toLowerCase().includes(normalizedQuery));
        return Boolean(inName || inDescription || inTags);
      })
        .sort((a, b) => a.token.name.localeCompare(b.token.name));
      }, [project.styles, filterCategory, lintFilter, normalizedQuery, showUnusedOnly, styleLintMap, styleUsageMap]);
  const visibleLintStats = useMemo(() => {
    return styleEntries.reduce(
      (acc, { token }) => {
        const issues = styleLintMap[token.name] ?? [];
        issues.forEach((issue) => {
          if (issue.severity === "error") {
            acc.errors += 1;
          } else {
            acc.warnings += 1;
          }
        });
        return acc;
      },
      { errors: 0, warnings: 0 }
    );
  }, [styleEntries, styleLintMap]);
  const selectedStyle = styleEditorSelection;
  const selectedToken = selectedStyle ? project.styles[selectedStyle] : undefined;
  const hasAnyStyles = Object.keys(project.styles).length > 0;
  const selectedStyleUsage = selectedStyle && styleUsageMap[selectedStyle] ? styleUsageMap[selectedStyle]! : [];
  const selectedStyleLintIssues = selectedStyle && styleLintMap[selectedStyle] ? styleLintMap[selectedStyle]! : [];
  const availableUsageWidgetTypes = useMemo(() => {
    const types = new Set<string>();
    selectedStyleUsage.forEach((usage) => types.add(usage.widget.type));
    return Array.from(types).sort((a, b) => a.localeCompare(b));
  }, [selectedStyleUsage]);
  const filteredStyleUsageEntries = useMemo(() => {
    return selectedStyleUsage
      .map((usage, index) => ({ usage, index }))
      .filter(({ usage }) => {
        const ownerMatches = usageOwnerFilter === "all" || usage.target.type === usageOwnerFilter;
        const widgetMatches = usageWidgetFilter === "all" || usage.widget.type === usageWidgetFilter;
        return ownerMatches && widgetMatches;
      });
  }, [selectedStyleUsage, usageOwnerFilter, usageWidgetFilter]);
  const usagePreview = filteredStyleUsageEntries.slice(0, MAX_USAGE_PREVIEW);
  const usageOverflow = Math.max(0, filteredStyleUsageEntries.length - usagePreview.length);
  const deleteGuardReason = selectedStyleUsage.length
    ? `Used by ${selectedStyleUsage.length} widget${selectedStyleUsage.length === 1 ? "" : "s"}. Remove references before deleting.`
    : null;
  const highlightedUsageIndex = selectedStyle ? usageCursor[selectedStyle] ?? null : null;
  const noUsageMatches = filteredStyleUsageEntries.length === 0;
  const usageFiltersDirty = usageOwnerFilter !== "all" || usageWidgetFilter !== "all";
  const lintErrorCount = selectedStyleLintIssues.filter((issue) => issue.severity === "error").length;
  const lintWarningCount = selectedStyleLintIssues.filter((issue) => issue.severity !== "error").length;
  const lintSummaryLabel = lintErrorCount || lintWarningCount
    ? [
        lintErrorCount ? `${lintErrorCount} error${lintErrorCount === 1 ? "" : "s"}` : "",
        lintWarningCount ? `${lintWarningCount} warning${lintWarningCount === 1 ? "" : "s"}` : "",
      ]
        .filter(Boolean)
        .join(" • ")
    : "";
  const lintSeverityClass = lintErrorCount ? "danger" : "warning";
  const lintHelpText = lintErrorCount
    ? "Resolve errors before shipping this style."
    : lintWarningCount
      ? "Warnings highlight optional improvements."
      : lintScanBusy
        ? "Running background lint…"
        : "Lint stays fresh automatically as styles change.";
  const filtersAreDefault = !normalizedQuery && filterCategory === "all" && lintFilter === "all" && !showUnusedOnly;
  const totalStyleCount = Object.keys(project.styles).length;
  const visibleStyleCount = styleEntries.length;
  const visibleLintSummaryParts = [
    visibleLintStats.errors ? `${visibleLintStats.errors} error${visibleLintStats.errors === 1 ? "" : "s"}` : "",
    visibleLintStats.warnings ? `${visibleLintStats.warnings} warning${visibleLintStats.warnings === 1 ? "" : "s"}` : "",
  ].filter(Boolean);
  const visibleLintSummaryLabel = lintScanBusy
    ? "Lint updating…"
    : visibleLintSummaryParts.length > 0
      ? visibleLintSummaryParts.join(" • ")
      : "No lint issues in view";
  const visibleLintSummaryTone = lintScanBusy
    ? "is-busy"
    : visibleLintStats.errors
      ? "is-danger"
      : visibleLintStats.warnings
        ? "is-warning"
        : "is-clean";
  const lintDrawerGroups = useMemo(() => {
    const entries: LintDrawerEntry[] = Object.entries(project.styles)
      .map(([name]) => {
        const issues = styleLintMap[name] ?? [];
        if (!issues.length) {
          return null;
        }
        const errorCount = issues.filter((issue) => issue.severity === "error").length;
        const warningCount = issues.length - errorCount;
        return { name, issues, errorCount, warningCount };
      })
      .filter((entry): entry is LintDrawerEntry => Boolean(entry));
    const errors = entries
      .filter((entry) => entry.errorCount > 0)
      .sort((a, b) => b.errorCount - a.errorCount || b.warningCount - a.warningCount || a.name.localeCompare(b.name));
    const warnings = entries
      .filter((entry) => entry.errorCount === 0 && entry.warningCount > 0)
      .sort((a, b) => b.warningCount - a.warningCount || a.name.localeCompare(b.name));
    const totalIssues = entries.reduce((sum, entry) => sum + entry.issues.length, 0);
    return { errors, warnings, totalIssues };
  }, [project.styles, styleLintMap]);
  const lintDrawerHasIssues = lintDrawerGroups.totalIssues > 0;
  const hasUnsavedChanges = useMemo(() => {
    const baseline = selectedToken ? tokenToForm(selectedToken) : defaultFormState();
    return (
      baseline.name !== formState.name ||
      baseline.category !== formState.category ||
      baseline.description !== formState.description ||
      baseline.tagsText !== formState.tagsText ||
      baseline.valueText !== formState.valueText ||
      baseline.metadataText !== formState.metadataText
    );
  }, [formState, selectedToken]);
  const saveStatusLabel = hasUnsavedChanges ? "Unsaved edits" : "All changes saved";
  const liveFormToken = useMemo(() => {
    let parsedValue: Record<string, unknown>;
    let parsedMetadata: Record<string, unknown>;
    try {
      parsedValue = formState.valueText.trim() ? JSON.parse(formState.valueText) : {};
    } catch {
      return null;
    }
    try {
      parsedMetadata = formState.metadataText.trim() ? JSON.parse(formState.metadataText) : {};
    } catch {
      parsedMetadata = {};
    }
    const tags = formState.tagsText
      .split(",")
      .map((tag) => tag.trim())
      .filter(Boolean);
    const fallbackName = selectedToken?.name ?? "preview_style";
    const token: StyleTokenModel = {
      name: formState.name.trim() || fallbackName,
      category: formState.category,
      description: formState.description.trim() || undefined,
      value: parsedValue,
      tags,
      metadata: parsedMetadata,
    };
    return token;
  }, [formState, selectedToken]);
  const previewToken = liveFormToken ?? selectedToken;
  const knobEditingDisabledReason = liveFormToken ? undefined : "Fix Value JSON to use quick knobs.";
  const usageKnobSummaries = useMemo(() => buildStylePreviewKnobs(previewToken ?? selectedToken), [previewToken, selectedToken]);

  useEffect(() => {
    setUsageCursor((prev) => {
      const next: Record<string, number> = {};
      Object.entries(prev).forEach(([styleName, index]) => {
        const usages = styleUsageMap[styleName];
        if (usages?.length) {
          next[styleName] = Math.min(index, usages.length - 1);
        }
      });
      return next;
    });
  }, [styleUsageMap]);

  useEffect(() => {
    setUsageOwnerFilter("all");
    setUsageWidgetFilter("all");
  }, [selectedStyle]);

  useEffect(() => {
    if (usageWidgetFilter === "all") {
      return;
    }
    const hasType = selectedStyleUsage.some((usage) => usage.widget.type === usageWidgetFilter);
    if (!hasType) {
      setUsageWidgetFilter("all");
    }
  }, [selectedStyleUsage, usageWidgetFilter]);

  useEffect(() => {
    let cancelled = false;
    if (!hasAnyStyles) {
      setStyleLintMap({});
      setLintScanError(null);
      setLintScanBusy(false);
      return () => {
        cancelled = true;
      };
    }
    setLintScanBusy(true);
    lintStyles(project.styles)
      .then((issues) => {
        if (cancelled) {
          return;
        }
        const grouped: Record<string, ValidationIssue[]> = {};
        issues.forEach((issue) => {
          const match = issue.path.match(/^styles\/(.+?)(?:\/|$)/);
          const styleName = match ? match[1] : "__global";
          if (!grouped[styleName]) {
            grouped[styleName] = [];
          }
          grouped[styleName]!.push(issue);
        });
        setStyleLintMap(grouped);
        setLintScanError(null);
      })
      .catch((error) => {
        if (!cancelled) {
          setLintScanError(error instanceof Error ? error.message : "Unable to lint styles");
          setStyleLintMap({});
        }
      })
      .finally(() => {
        if (!cancelled) {
          setLintScanBusy(false);
        }
      });
    return () => {
      cancelled = true;
    };
  }, [hasAnyStyles, project.styles]);

  useEffect(() => {
    if (selectedToken) {
      setFormState(tokenToForm(selectedToken));
      setFormErrors({});
    } else {
      setFormState(defaultFormState());
    }
  }, [selectedToken]);

  useEffect(() => {
    let cancelled = false;
    if (!previewToken) {
      setAutoPreview(null);
      setAutoPreviewError(null);
      setAutoPreviewBusy(false);
      return () => {
        cancelled = true;
      };
    }
    const cacheKey = JSON.stringify(previewToken);
    const cachedPreview = previewCacheRef.current.get(cacheKey);
    if (cachedPreview) {
      setAutoPreview(cachedPreview);
      setAutoPreviewError(null);
      setAutoPreviewBusy(false);
      return () => {
        cancelled = true;
      };
    }
    setAutoPreviewBusy(true);
    const timeoutId = window.setTimeout(() => {
      previewStyle(previewToken)
        .then((result) => {
          if (cancelled) {
            return;
          }
          previewCacheRef.current.set(cacheKey, result);
          if (previewCacheRef.current.size > 20) {
            const oldest = previewCacheRef.current.keys().next().value;
            if (oldest) {
              previewCacheRef.current.delete(oldest);
            }
          }
          setAutoPreview(result);
          setAutoPreviewError(null);
        })
        .catch((error) => {
          if (!cancelled) {
            setAutoPreview(null);
            setAutoPreviewError(error instanceof Error ? error.message : "Unable to preview style");
          }
        })
        .finally(() => {
          if (!cancelled) {
            setAutoPreviewBusy(false);
          }
        });
    }, PREVIEW_DEBOUNCE_MS);
    return () => {
      cancelled = true;
      clearTimeout(timeoutId);
    };
  }, [previewToken]);

  const updateForm = (field: keyof FormState, value: string) => {
    setFormState((prev) => ({ ...prev, [field]: value }));
    setFormErrors((prev) => {
      const next = { ...prev };
      delete next[field];
      return next;
    });
    if (field === "valueText") {
      if (valueCopyState !== "idle") {
        setValueCopyState("idle");
      }
      if (valueFormatState !== "idle") {
        setValueFormatState("idle");
      }
    }
    if (field === "metadataText") {
      if (metadataCopyState !== "idle") {
        setMetadataCopyState("idle");
      }
      if (metadataFormatState !== "idle") {
        setMetadataFormatState("idle");
      }
    }
  };

  const buildTokenFromForm = (): StyleTokenModel | null => {
    const trimmedName = formState.name.trim();
    if (!trimmedName) {
      setFormErrors((prev) => ({ ...prev, name: "Name is required" }));
      return null;
    }
    const existingToken = project.styles[trimmedName];
    if (existingToken && trimmedName !== selectedStyle) {
      setFormErrors((prev) => ({ ...prev, name: "A different style already uses this name" }));
      return null;
    }
    let parsedValue: Record<string, unknown>;
    try {
      parsedValue = formState.valueText.trim() ? JSON.parse(formState.valueText) : {};
    } catch (error) {
      setFormErrors((prev) => ({ ...prev, valueText: "Value JSON is invalid" }));
      return null;
    }
    let parsedMetadata: Record<string, unknown>;
    try {
      parsedMetadata = formState.metadataText.trim() ? JSON.parse(formState.metadataText) : {};
    } catch (error) {
      setFormErrors((prev) => ({ ...prev, metadataText: "Metadata JSON is invalid" }));
      return null;
    }
    const tags = formState.tagsText
      .split(",")
      .map((tag) => tag.trim())
      .filter(Boolean);
    return {
      name: trimmedName,
      category: formState.category,
      description: formState.description.trim() || undefined,
      value: parsedValue,
      tags,
      metadata: parsedMetadata,
    };
  };

  const handleSave = () => {
    const token = buildTokenFromForm();
    if (!token) {
      return;
    }
    saveStyleToken(token, selectedStyle ?? undefined);
    setDraftLintIssues([]);
    setDraftPreview(null);
    setFormErrors({});
  };

  const handleCreateStyle = () => {
    const name = generateStyleName(project.styles);
    const template: StyleTokenModel = {
      name,
      category: "surface",
      description: "New surface token",
      value: {
        backgroundColor: "#ffffff",
        color: "#0f172a",
      },
      tags: ["draft"],
      metadata: {},
    };
    saveStyleToken(template);
    setFormState(tokenToForm(template));
    setFormErrors({});
    setDraftPreview(null);
    setDraftLintIssues([]);
  };

  const focusUsageAtIndex = useCallback(
    (styleName: string, usageIndex: number, origin: UsageFocusOrigin = "list_click") => {
      const usages = styleUsageMap[styleName];
      if (!usages?.length) {
        return;
      }
      const clampedIndex = Math.max(0, Math.min(usageIndex, usages.length - 1));
      const usage = usages[clampedIndex];
      setEditorTarget(usage.target);
      selectWidget(usage.path);
      emitTelemetry("styles", "style_usage_focus", {
        style: styleName,
        targetType: usage.target.type,
        targetId: usage.target.id,
        widgetType: usage.widget.type,
        widgetId: usage.widget.id ?? null,
        usageIndex: clampedIndex,
        usageCount: usages.length,
        origin,
        ownerFilter: usageOwnerFilter,
        widgetFilter: usageWidgetFilter,
      });
    },
    [selectWidget, setEditorTarget, styleUsageMap, usageOwnerFilter, usageWidgetFilter]
  );

  const handleFocusNextUsage = useCallback(
    (styleName: string) => {
      const usages = styleUsageMap[styleName];
      if (!usages?.length) {
        return;
      }
      setUsageCursor((prev) => {
        const nextIndex = ((prev[styleName] ?? -1) + 1) % usages.length;
        focusUsageAtIndex(styleName, nextIndex, "cycle");
        return { ...prev, [styleName]: nextIndex };
      });
    },
    [focusUsageAtIndex, styleUsageMap]
  );

  const handleDelete = () => {
    if (!selectedStyle) {
      return;
    }
    if (selectedStyleUsage.length > 0) {
      window.alert("Remove this style from all widgets before deleting it.");
      return;
    }
    const message = `Delete style "${selectedStyle}"? This cannot be undone.`;
    if (!window.confirm(message)) {
      return;
    }
    deleteStyleToken(selectedStyle);
    setFormErrors({});
    setDraftPreview(null);
    setDraftLintIssues([]);
  };

  const handleCloneStyle = () => {
    if (!selectedToken) {
      return;
    }
    const cloneName = generateCloneName(selectedToken.name, project.styles);
    const clonedValue = JSON.parse(JSON.stringify(selectedToken.value ?? {}));
    const clonedMetadata = JSON.parse(JSON.stringify(selectedToken.metadata ?? {}));
    const cloneToken: StyleTokenModel = {
      ...selectedToken,
      name: cloneName,
      description: selectedToken.description ? `${selectedToken.description} (clone)` : "Cloned style",
      tags: Array.from(new Set([...(selectedToken.tags ?? []), "clone"])),
      metadata: { ...clonedMetadata, clonedFrom: selectedToken.name },
      value: clonedValue,
    };
    saveStyleToken(cloneToken);
    setFormState(tokenToForm(cloneToken));
    setFormErrors({});
    setDraftPreview(null);
    setDraftLintIssues([]);
  };

  const handleResetFilters = () => {
    setFilterQuery("");
    setFilterCategory("all");
    setLintFilter("all");
    setShowUnusedOnly(false);
  };

  const handleResetUsageFilters = () => {
    setUsageOwnerFilter("all");
    setUsageWidgetFilter("all");
  };

  const handleToggleLintDrawer = () => {
    setIsLintDrawerOpen((prev) => !prev);
  };

  const handleNavigateFromLintDrawer = (styleName: string) => {
    setStyleEditorSelection(styleName);
    setIsLintDrawerOpen(false);
  };

  const copyJsonPayload = async (payload: string): Promise<void> => {
    if (navigator?.clipboard?.writeText) {
      try {
        await navigator.clipboard.writeText(payload);
        return;
      } catch {
        // Clipboard API can fail in insecure contexts; fall back to execCommand below.
      }
    }
    if (!document?.body) {
      throw new Error("Clipboard unavailable");
    }
    const temp = document.createElement("textarea");
    temp.value = payload;
    temp.style.position = "fixed";
    temp.style.opacity = "0";
    document.body.appendChild(temp);
    temp.focus();
    temp.select();
    const succeeded = document.execCommand("copy");
    document.body.removeChild(temp);
    if (!succeeded) {
      throw new Error("execCommand failed");
    }
  };

  const handleCopyValueJson = async () => {
    const payload = formState.valueText || EMPTY_JSON;
    try {
      await copyJsonPayload(payload);
      setValueCopyState("copied");
    } catch {
      setValueCopyState("error");
    }
  };

  const handleCopyMetadataJson = async () => {
    const payload = formState.metadataText || EMPTY_JSON;
    try {
      await copyJsonPayload(payload);
      setMetadataCopyState("copied");
    } catch {
      setMetadataCopyState("error");
    }
  };

  const handleFormatValueJson = () => {
    try {
      const parsed = formState.valueText.trim() ? JSON.parse(formState.valueText) : {};
      const formatted = JSON.stringify(parsed, null, 2);
      setFormState((prev) => ({ ...prev, valueText: formatted }));
      setFormErrors((prev) => {
        const next = { ...prev };
        delete next.valueText;
        return next;
      });
      setValueFormatState("formatted");
      setValueCopyState("idle");
    } catch {
      setValueFormatState("error");
      setFormErrors((prev) => ({ ...prev, valueText: "Value JSON is invalid" }));
    }
  };

  const handleFormatMetadataJson = () => {
    try {
      const parsed = formState.metadataText.trim() ? JSON.parse(formState.metadataText) : {};
      const formatted = JSON.stringify(parsed, null, 2);
      setFormState((prev) => ({ ...prev, metadataText: formatted }));
      setFormErrors((prev) => {
        const next = { ...prev };
        delete next.metadataText;
        return next;
      });
      setMetadataFormatState("formatted");
      setMetadataCopyState("idle");
    } catch {
      setMetadataFormatState("error");
      setFormErrors((prev) => ({ ...prev, metadataText: "Metadata JSON is invalid" }));
    }
  };

  const handleResetForm = () => {
    if (selectedToken) {
      setFormState(tokenToForm(selectedToken));
    } else {
      setFormState(defaultFormState());
    }
    setFormErrors({});
    setDraftPreview(null);
    setDraftPreviewError(null);
    setDraftLintIssues([]);
    setDraftLintError(null);
    setValueCopyState("idle");
    setValueFormatState("idle");
    setMetadataCopyState("idle");
    setMetadataFormatState("idle");
  };

  const handleKnobFieldChange = useCallback(
    (fieldKey: string, rawValue: string) => {
      let parsedValue: Record<string, unknown>;
      try {
        parsedValue = formState.valueText.trim() ? JSON.parse(formState.valueText) : {};
      } catch {
        setFormErrors((prev) => ({ ...prev, valueText: "Value JSON is invalid" }));
        return;
      }
      const nextValue = rawValue.trim().length === 0 ? undefined : rawValue;
      if (nextValue === undefined) {
        delete parsedValue[fieldKey];
      } else {
        parsedValue[fieldKey] = nextValue;
      }
      updateForm("valueText", JSON.stringify(parsedValue, null, 2));
    },
    [formState.valueText, setFormErrors, updateForm]
  );

  const handlePreviewDraft = async () => {
    const token = buildTokenFromForm();
    if (!token) {
      return;
    }
    setIsPreviewingDraft(true);
    setDraftPreviewError(null);
    try {
      const result = await previewStyle(token);
      setDraftPreview(result);
    } catch (error) {
      setDraftPreview(null);
      setDraftPreviewError(error instanceof Error ? error.message : "Unable to preview style");
    } finally {
      setIsPreviewingDraft(false);
    }
  };

  const handleLintDraft = async () => {
    const token = buildTokenFromForm();
    if (!token) {
      return;
    }
    setIsLintingDraft(true);
    setDraftLintError(null);
    try {
      const payload: Record<string, StyleTokenModel> = { ...project.styles, [token.name]: token };
      if (selectedStyle && selectedStyle !== token.name) {
        delete payload[selectedStyle];
      }
      const issues = await lintStyles(payload);
      setDraftLintIssues(issues);
    } catch (error) {
      setDraftLintError(error instanceof Error ? error.message : "Unable to lint styles");
    } finally {
      setIsLintingDraft(false);
    }
  };

  return (
    <section className="style-manager" id="style-manager">
      <div className="style-manager__header">
        <p className="section-title" style={{ marginBottom: 0 }}>
          Style Tokens
        </p>
        <button type="button" className="button primary" onClick={handleCreateStyle}>
          New Style
        </button>
      </div>
      <div className="style-manager__filters">
        <input
          className="input-field"
          placeholder="Search styles by name, tag, or description"
          value={filterQuery}
          onChange={(event) => setFilterQuery(event.target.value)}
        />
        <select
          className="select-field"
          value={filterCategory}
          onChange={(event) => setFilterCategory(event.target.value as StyleCategory | "all")}
        >
          {STYLE_FILTER_CATEGORIES.map((category) => (
            <option value={category} key={category}>
              {category === "all" ? "All categories" : category}
            </option>
          ))}
        </select>
        <label className="style-manager__lint-filter">
          <span>Lint filter</span>
          <select
            className="select-field"
            value={lintFilter}
            onChange={(event) => setLintFilter(event.target.value as "all" | "issues" | "errors")}
          >
            <option value="all">All styles</option>
            <option value="issues">Only lint issues</option>
            <option value="errors">Errors only</option>
          </select>
        </label>
        <label className="style-manager__unused-toggle">
          <input
            type="checkbox"
            checked={showUnusedOnly}
            onChange={(event) => setShowUnusedOnly(event.target.checked)}
          />
          <span>Show unused only</span>
        </label>
        <button
          type="button"
          className="button tertiary style-manager__filters-reset"
          onClick={handleResetFilters}
          disabled={filtersAreDefault}
        >
          Reset filters
        </button>
        <div className="style-manager__results">
          <span
            className={`style-manager__results-summary ${filtersAreDefault ? "is-baseline" : "is-filtered"}`}
            data-testid="style-results-summary"
          >
            {totalStyleCount === 0
              ? "No styles yet"
              : filtersAreDefault
                ? `${totalStyleCount} style${totalStyleCount === 1 ? "" : "s"}`
                : `Showing ${visibleStyleCount} of ${totalStyleCount}`}
          </span>
          <button
            type="button"
            className={`style-manager__lint-summary-pill ${visibleLintSummaryTone}`}
            data-testid="style-lint-summary"
            onClick={handleToggleLintDrawer}
            aria-expanded={isLintDrawerOpen}
          >
            {visibleLintSummaryLabel}
          </button>
        </div>
      </div>
      <div className="style-manager__lint-status">
        {lintScanBusy ? (
          <span>Running background lint…</span>
        ) : lintScanError ? (
          <span className="field-hint error-text">Lint unavailable: {lintScanError}</span>
        ) : lintSummaryLabel ? (
          <span className={`field-hint lint-hint lint-hint--${lintSeverityClass}`}>
            <strong>{lintSummaryLabel}</strong>
            <span>{lintHelpText}</span>
          </span>
        ) : (
          <span className="field-hint">{lintHelpText}</span>
        )}
      </div>
      {isLintDrawerOpen && (
        <div className="style-manager__lint-drawer" role="dialog" aria-label="Lint breakdown">
          <div className="lint-drawer__header">
            <div>
              <strong>Lint breakdown</strong>
              <span>
                {lintScanBusy
                  ? "Lint status updating…"
                  : lintDrawerHasIssues
                    ? `${lintDrawerGroups.totalIssues} open ${lintDrawerGroups.totalIssues === 1 ? "issue" : "issues"}`
                    : "No lint issues detected"}
              </span>
            </div>
            <button type="button" className="button tertiary lint-drawer__close" onClick={handleToggleLintDrawer}>
              Close
            </button>
          </div>
          <div className="lint-drawer__body">
            {lintScanBusy ? (
              <span className="field-hint">Lint results will appear when the scan finishes.</span>
            ) : !lintDrawerHasIssues ? (
              <span className="field-hint">All visible styles are lint-clean.</span>
            ) : (
              <>
                {lintDrawerGroups.errors.length > 0 && (
                  <section className="lint-drawer__section">
                    <p className="lint-drawer__section-title">Errors</p>
                    <ul>
                      {lintDrawerGroups.errors.map((entry) => (
                        <li key={entry.name}>
                          <button
                            type="button"
                            className="lint-drawer__style-button is-danger"
                            onClick={() => handleNavigateFromLintDrawer(entry.name)}
                          >
                            <span className="lint-drawer__style-name">{entry.name}</span>
                            <span className="lint-drawer__style-meta">
                              {entry.errorCount} error{entry.errorCount === 1 ? "" : "s"}
                              {entry.warningCount > 0 ? ` • ${entry.warningCount} warning${entry.warningCount === 1 ? "" : "s"}` : ""}
                            </span>
                          </button>
                        </li>
                      ))}
                    </ul>
                  </section>
                )}
                {lintDrawerGroups.warnings.length > 0 && (
                  <section className="lint-drawer__section">
                    <p className="lint-drawer__section-title">Warnings</p>
                    <ul>
                      {lintDrawerGroups.warnings.map((entry) => (
                        <li key={entry.name}>
                          <button
                            type="button"
                            className="lint-drawer__style-button is-warning"
                            onClick={() => handleNavigateFromLintDrawer(entry.name)}
                          >
                            <span className="lint-drawer__style-name">{entry.name}</span>
                            <span className="lint-drawer__style-meta">
                              {entry.warningCount} warning{entry.warningCount === 1 ? "" : "s"}
                            </span>
                          </button>
                        </li>
                      ))}
                    </ul>
                  </section>
                )}
              </>
            )}
          </div>
        </div>
      )}
      {!hasAnyStyles ? (
        <p className="style-manager__empty">No tokens yet. Generate a style to drive consistent theming.</p>
      ) : styleEntries.length === 0 ? (
        <p className="style-manager__empty">No styles match the current search or category filter.</p>
      ) : (
        <div className="style-list">
          {styleEntries.map(({ key, token }) => {
            const background = getColorValue(token, "backgroundColor", "#e2e8f0");
            const foreground = getColorValue(token, "color", "#0f172a");
            const lintForToken = styleLintMap[key] ?? [];
            const lintSeverity = lintForToken.some((issue) => issue.severity === "error")
              ? "error"
              : lintForToken.length > 0
                ? "warning"
                : null;
            const usageCount = styleUsageMap[key]?.length ?? 0;
            return (
              <button
                type="button"
                key={key}
                className={`style-card ${selectedStyle === key ? "active" : ""}`}
                onClick={() => setStyleEditorSelection(key)}
              >
                <div className="style-card__meta">
                  <strong>{token.name}</strong>
                  <span>{token.category}</span>
                  <span className={`style-card__usage ${usageCount === 0 ? "is-unused" : ""}`}>
                    {usageCount === 0 ? "Unused" : `${usageCount} use${usageCount === 1 ? "" : "s"}`}
                  </span>
                  {token.tags?.length ? (
                    <span className="style-card__tags">{token.tags.join(", ")}</span>
                  ) : (
                    <span className="style-card__tags">No tags</span>
                  )}
                  {lintSeverity && (
                    <span
                      className={`style-card__lint style-card__lint--${lintSeverity}`}
                      title={lintForToken.map((issue) => issue.message).join(" • ")}
                    >
                      {lintSeverity === "error" ? "!" : "⚠"} {lintForToken.length}
                    </span>
                  )}
                </div>
                <span className="style-card__swatch" style={{ background: background, color: foreground }}>
                  Aa
                </span>
              </button>
            );
          })}
        </div>
      )}

      {selectedToken ? (
        <div className="style-editor" id="style-editor-panel">
          <StylePreviewCard
            label="Style Preview"
            token={previewToken ?? undefined}
            preview={autoPreview}
            busy={autoPreviewBusy}
            error={autoPreviewError}
            emptyHint="Preview loads from the backend as soon as the token is saved."
            footnote="Auto-refreshes whenever this token changes."
            knobEditingEnabled={Boolean(liveFormToken)}
            knobEditingDisabledReason={knobEditingDisabledReason}
            onKnobFieldChange={handleKnobFieldChange}
          />
          <div className="style-usage-banner">
            <div>
              <strong>{selectedStyleUsage.length || "No"}</strong> matches using this style
            </div>
            {selectedStyleUsage.length > 0 && selectedStyle && (
              <button type="button" className="button tertiary" onClick={() => handleFocusNextUsage(selectedStyle)}>
                Focus next match
              </button>
            )}
          </div>
          {selectedStyleUsage.length > 0 && (
            <div className="style-usage-controls">
              <div className="style-usage-filter-group">
                <span className="style-usage-filter-label">Owner scope</span>
                <div className="style-usage-filter-chips" role="group" aria-label="Owner filter">
                  {(["all", "screen", "component"] as const).map((option) => {
                    const label = option === "all" ? "All" : option === "screen" ? "Screens" : "Components";
                    const isActive = usageOwnerFilter === option;
                    return (
                      <button
                        type="button"
                        key={option}
                        className={`style-usage-filter-button${isActive ? " is-active" : ""}`}
                        data-testid={`usage-owner-filter-${option}`}
                        aria-pressed={isActive}
                        onClick={() => setUsageOwnerFilter(option)}
                      >
                        {label}
                      </button>
                    );
                  })}
                </div>
              </div>
              <label className="style-usage-filter-group style-usage-widget-filter">
                <span className="style-usage-filter-label">Widget type</span>
                <select
                  className="select-field"
                  value={usageWidgetFilter}
                  onChange={(event) => setUsageWidgetFilter(event.target.value)}
                  aria-label="Widget type filter"
                  disabled={availableUsageWidgetTypes.length === 0}
                >
                  <option value="all">All widgets</option>
                  {availableUsageWidgetTypes.map((type) => (
                    <option key={type} value={type}>
                      {type}
                    </option>
                  ))}
                </select>
              </label>
              {usageFiltersDirty && (
                <button type="button" className="button tertiary style-usage-filter-reset" onClick={handleResetUsageFilters}>
                  Reset usage filters
                </button>
              )}
            </div>
          )}
          <div className="style-usage-list">
            {selectedStyleUsage.length === 0 && <span className="field-hint">No widgets reference this token yet.</span>}
            {selectedStyleUsage.length > 0 && noUsageMatches && (
              <span className="field-hint">No matches for the current usage filters.</span>
            )}
            {selectedStyle
              ? usagePreview.map(({ usage, index: usageIndex }) => {
                const { owner, widget, pathLabel } = describeUsageLocation(usage);
                const ownerTypeLabel = usage.target.type === "screen" ? "Screen" : "Component";
                const ownerTestId = `style-usage-owner-${usage.target.type}-${usage.target.id}`;
                const isActive = highlightedUsageIndex === usageIndex;
                const usageTestId = `style-usage-item-${usage.target.type}-${usage.target.id}-${usageIndex}`;
                return (
                  <button
                    type="button"
                    key={`${owner}-${widget}-${pathLabel}-${usageIndex}`}
                    className={`style-usage-item${isActive ? " is-active" : ""}`}
                    data-testid={usageTestId}
                    onClick={() => {
                      setUsageCursor((prev) => ({ ...prev, [selectedStyle]: usageIndex }));
                      focusUsageAtIndex(selectedStyle, usageIndex, "list_click");
                    }}
                    title={`Jump to ${owner}`}
                  >
                    <div className="style-usage-item__meta">
                      <div
                        className={`style-usage-item__owner-pill owner-pill--${usage.target.type}`}
                        data-testid={ownerTestId}
                      >
                        <span className="style-usage-item__owner-type">{ownerTypeLabel}</span>
                        <span className="style-usage-item__owner-id">{usage.target.id}</span>
                      </div>
                      <span className="style-usage-item__owner">{owner}</span>
                      <span className="style-usage-item__widget">{widget}</span>
                    </div>
                    {autoPreview && (
                      <div
                        className="style-usage-item__preview style-usage-item__hover-preview"
                        style={{ background: autoPreview.backgroundColor, color: autoPreview.color }}
                        aria-hidden="true"
                      >
                        <div className="style-usage-item__preview-top">
                          <span className="style-usage-item__preview-category">{autoPreview.category}</span>
                          {autoPreview.description && <span className="style-usage-item__preview-description">{autoPreview.description}</span>}
                        </div>
                        <span className="style-usage-item__preview-meta">BG {autoPreview.backgroundColor} • FG {autoPreview.color}</span>
                      </div>
                    )}
                    {usageKnobSummaries.length > 0 && (
                      <div className="style-usage-item__knobs">
                        {usageKnobSummaries.map((knob) => (
                          <div key={knob.id} className="style-usage-item__knob">
                            <span className="style-usage-item__knob-label">{knob.label}</span>
                            <span className="style-usage-item__knob-value">{knob.summary ?? "No values set"}</span>
                          </div>
                        ))}
                      </div>
                    )}
                    {lintSummaryLabel && (
                      <div className="style-usage-item__lint">
                        <span className={`style-usage-item__lint-pill ${lintSeverityClass}`}>{lintSummaryLabel}</span>
                      </div>
                    )}
                    <div className="style-usage-item__path">Path: {pathLabel}</div>
                    <span className="style-usage-item__cta">Jump</span>
                  </button>
                );
              })
              : null}
            {usageOverflow > 0 && (
              <span className="field-hint">{usageOverflow} more matches not shown. Use “Focus next match” to cycle all.</span>
            )}
          </div>
          <div className="style-lint style-lint--auto">
            <p className="style-lint__title">Auto lint</p>
            {lintScanBusy ? (
              <span className="field-hint">Checking tokens…</span>
            ) : selectedStyleLintIssues.length === 0 ? (
              <span className="field-hint">No findings for this style.</span>
            ) : (
              selectedStyleLintIssues.map((issue) => (
                <span key={`${issue.path}-${issue.message}`} className={`style-lint__issue ${issue.severity}`}>
                  {issue.severity === "error" ? "!" : "⚠"} {issue.message}
                </span>
              ))
            )}
          </div>
          <div className="style-editor__grid">
            <label className={`inspector-field ${formErrors.name ? "error" : ""}`}>
              <div className="field-label">
                <span>Style Name</span>
              </div>
              <input
                className="input-field"
                value={formState.name}
                onChange={(event) => updateForm("name", event.target.value)}
                placeholder="primary_surface"
              />
              {formErrors.name && <span className="field-hint error-text">{formErrors.name}</span>}
            </label>
            <label className="inspector-field">
              <div className="field-label">
                <span>Category</span>
              </div>
              <select
                className="select-field"
                value={formState.category}
                onChange={(event) => updateForm("category", event.target.value as StyleCategory)}
              >
                {CATEGORY_OPTIONS.map((category) => (
                  <option value={category} key={category}>
                    {category}
                  </option>
                ))}
              </select>
            </label>
            <label className="inspector-field">
              <div className="field-label">
                <span>Description</span>
              </div>
              <input
                className="input-field"
                value={formState.description}
                onChange={(event) => updateForm("description", event.target.value)}
                placeholder="Used for cards and modals"
              />
            </label>
            <label className="inspector-field">
              <div className="field-label">
                <span>Tags</span>
                <span className="field-badge warning">Comma separated</span>
              </div>
              <input
                className="input-field"
                value={formState.tagsText}
                onChange={(event) => updateForm("tagsText", event.target.value)}
                placeholder="primary, elevated"
              />
            </label>
          </div>
          <label className={`inspector-field ${formErrors.valueText ? "error" : ""}`}>
            <div className="field-label">
              <span>Value JSON</span>
              <div className="field-label__actions">
                <button
                  type="button"
                  className="button tertiary field-action-button"
                  onClick={handleCopyValueJson}
                  aria-label="Copy Value JSON"
                  title="Copy Value JSON"
                >
                  Copy JSON
                </button>
                <button
                  type="button"
                  className="button tertiary field-action-button"
                  onClick={handleFormatValueJson}
                  aria-label="Format Value JSON"
                  title="Format Value JSON"
                >
                  Format JSON
                </button>
                <div className="field-label__status">
                {valueCopyState === "copied" && <span className="field-hint success-text">Copied!</span>}
                {valueCopyState === "error" && <span className="field-hint error-text">Copy failed</span>}
                {valueFormatState === "formatted" && <span className="field-hint success-text">Formatted</span>}
                {valueFormatState === "error" && <span className="field-hint error-text">Format failed</span>}
                </div>
              </div>
            </div>
            <textarea
              className="textarea-field"
              rows={4}
              aria-label="Value JSON"
              value={formState.valueText}
              onChange={(event) => updateForm("valueText", event.target.value)}
            />
            {formErrors.valueText && <span className="field-hint error-text">{formErrors.valueText}</span>}
          </label>
          <label className={`inspector-field ${formErrors.metadataText ? "error" : ""}`}>
            <div className="field-label">
              <span>Metadata JSON</span>
              <div className="field-label__actions">
                <span className="field-badge warning">Optional hints</span>
                <button
                  type="button"
                  className="button tertiary field-action-button"
                  onClick={handleCopyMetadataJson}
                  aria-label="Copy Metadata JSON"
                  title="Copy Metadata JSON"
                >
                  Copy JSON
                </button>
                <button
                  type="button"
                  className="button tertiary field-action-button"
                  onClick={handleFormatMetadataJson}
                  aria-label="Format Metadata JSON"
                  title="Format Metadata JSON"
                >
                  Format JSON
                </button>
                <div className="field-label__status">
                  {metadataCopyState === "copied" && <span className="field-hint success-text">Copied!</span>}
                  {metadataCopyState === "error" && <span className="field-hint error-text">Copy failed</span>}
                  {metadataFormatState === "formatted" && <span className="field-hint success-text">Formatted</span>}
                  {metadataFormatState === "error" && <span className="field-hint error-text">Format failed</span>}
                </div>
              </div>
            </div>
            <textarea
              className="textarea-field"
              rows={3}
              aria-label="Metadata JSON"
              value={formState.metadataText}
              onChange={(event) => updateForm("metadataText", event.target.value)}
            />
            {formErrors.metadataText && <span className="field-hint error-text">{formErrors.metadataText}</span>}
          </label>
          <div className="style-actions">
            <button type="button" className="button primary" onClick={handleSave} disabled={!hasUnsavedChanges}>
              Save Style
            </button>
            <button type="button" className="button secondary" onClick={handleResetForm} disabled={!hasUnsavedChanges}>
              Reset Changes
            </button>
            <button type="button" className="button secondary" onClick={handleCloneStyle} disabled={!selectedToken}>
              Clone Style
            </button>
            <button type="button" className="button secondary" onClick={handlePreviewDraft} disabled={isPreviewingDraft}>
              {isPreviewingDraft ? "Previewing…" : "Preview Draft"}
            </button>
            <button type="button" className="button secondary" onClick={handleLintDraft} disabled={isLintingDraft}>
              {isLintingDraft ? "Linting…" : "Lint Draft"}
            </button>
            <button
              type="button"
              className="button tertiary"
              onClick={handleDelete}
              disabled={Boolean(deleteGuardReason)}
              title={deleteGuardReason ?? undefined}
            >
              Delete
            </button>
            <div className={`style-actions__status ${hasUnsavedChanges ? "is-dirty" : "is-clean"}`}>{saveStatusLabel}</div>
          </div>
          {deleteGuardReason && <p className="field-hint warning-text">{deleteGuardReason}</p>}
          {draftPreview && (
            <div className="style-preview style-preview--draft" style={{ background: draftPreview.backgroundColor, color: draftPreview.color }}>
              <div className="style-preview__meta">
                <span>{draftPreview.category}</span>
                <span>{draftPreview.description}</span>
              </div>
              <p className="style-preview__sample">Draft sample</p>
            </div>
          )}
          {draftPreviewError && <p className="style-manager__error">{draftPreviewError}</p>}
          <div className="style-lint style-lint--draft">
            <p className="style-lint__title">Draft lint</p>
            {draftLintError && <span className="style-manager__error">{draftLintError}</span>}
            {draftLintIssues.length === 0 && !draftLintError ? (
              <p className="style-lint__empty">Run “Lint Draft” to validate unsaved edits.</p>
            ) : (
              draftLintIssues.map((issue) => (
                <span key={`${issue.path}-${issue.message}`} className={`style-lint__issue ${issue.severity}`}>
                  {issue.severity === "error" ? "!" : "⚠"} {issue.message}
                </span>
              ))
            )}
          </div>
        </div>
      ) : (
        styleEntries.length > 0 && <p className="style-manager__empty">Select a style to edit its token definition.</p>
      )}
    </section>
  );
}
