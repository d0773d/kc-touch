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

type FormErrors = Partial<Record<keyof FormState, string>>;

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
  const [usageCursor, setUsageCursor] = useState<Record<string, number>>({});
  const previewCacheRef = useRef<Map<string, StylePreview>>(new Map());

  const normalizedQuery = filterQuery.trim().toLowerCase();
  const styleEntries = useMemo(() => {
    const entries = Object.entries(project.styles).map(([key, token]) => ({ key, token }));
    return entries
      .filter(({ token }) => {
        const matchesCategory = filterCategory === "all" || token.category === filterCategory;
        if (!matchesCategory) {
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
  }, [project.styles, filterCategory, normalizedQuery]);
  const selectedStyle = styleEditorSelection;
  const selectedToken = selectedStyle ? project.styles[selectedStyle] : undefined;
  const hasAnyStyles = Object.keys(project.styles).length > 0;
  const styleUsageMap = useMemo(() => buildStyleUsageMap(project), [project]);
  const selectedStyleUsage = selectedStyle && styleUsageMap[selectedStyle] ? styleUsageMap[selectedStyle]! : [];
  const selectedStyleLintIssues = selectedStyle && styleLintMap[selectedStyle] ? styleLintMap[selectedStyle]! : [];
  const usagePreview = selectedStyleUsage.slice(0, MAX_USAGE_PREVIEW);
  const usageOverflow = Math.max(0, selectedStyleUsage.length - usagePreview.length);
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
  };

  const buildTokenFromForm = (): StyleTokenModel | null => {
    const trimmedName = formState.name.trim();
    if (!trimmedName) {
      setFormErrors((prev) => ({ ...prev, name: "Name is required" }));
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
    (styleName: string, usageIndex: number) => {
      const usages = styleUsageMap[styleName];
      if (!usages?.length) {
        return;
      }
      const clampedIndex = Math.max(0, Math.min(usageIndex, usages.length - 1));
      const usage = usages[clampedIndex];
      setEditorTarget(usage.target);
      selectWidget(usage.path);
    },
    [selectWidget, setEditorTarget, styleUsageMap]
  );

  const handleFocusNextUsage = useCallback(
    (styleName: string) => {
      const usages = styleUsageMap[styleName];
      if (!usages?.length) {
        return;
      }
      setUsageCursor((prev) => {
        const nextIndex = ((prev[styleName] ?? -1) + 1) % usages.length;
        focusUsageAtIndex(styleName, nextIndex);
        return { ...prev, [styleName]: nextIndex };
      });
    },
    [focusUsageAtIndex, styleUsageMap]
  );

  const handleDelete = () => {
    if (!selectedStyle) {
      return;
    }
    const usageCount = selectedStyleUsage.length;
    const message =
      usageCount > 0
        ? `Style "${selectedStyle}" is used by ${usageCount} widget${usageCount === 1 ? "" : "s"}. Delete anyway? Widgets will lose the reference.`
        : `Delete style "${selectedStyle}"?`;
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
      </div>
      <div className="style-manager__lint-status">
        {lintScanBusy ? (
          <span>Running background lint…</span>
        ) : lintScanError ? (
          <span className="field-hint error-text">Lint unavailable: {lintScanError}</span>
        ) : (
          <span className="field-hint">Lint stays fresh automatically as styles change.</span>
        )}
      </div>
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
          <div className="style-usage-list">
            {selectedStyleUsage.length === 0 && <span className="field-hint">No widgets reference this token yet.</span>}
            {selectedStyle &&
              usagePreview.map((usage, index) => {
                const { owner, widget, pathLabel } = describeUsageLocation(usage);
                return (
                  <button
                    type="button"
                    key={`${owner}-${widget}-${pathLabel}-${index}`}
                    className="style-usage-item"
                    onClick={() => {
                      setUsageCursor((prev) => ({ ...prev, [selectedStyle]: index }));
                      focusUsageAtIndex(selectedStyle, index);
                    }}
                  >
                    <div className="style-usage-item__meta">
                      <span className="style-usage-item__owner">{owner}</span>
                      <span className="style-usage-item__widget">{widget}</span>
                    </div>
                    {autoPreview && (
                      <div className="style-usage-item__preview" style={{ background: autoPreview.backgroundColor, color: autoPreview.color }}>
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
              })}
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
            </div>
            <textarea
              className="textarea-field"
              rows={4}
              value={formState.valueText}
              onChange={(event) => updateForm("valueText", event.target.value)}
            />
            {formErrors.valueText && <span className="field-hint error-text">{formErrors.valueText}</span>}
          </label>
          <label className={`inspector-field ${formErrors.metadataText ? "error" : ""}`}>
            <div className="field-label">
              <span>Metadata JSON</span>
              <span className="field-badge warning">Optional hints</span>
            </div>
            <textarea
              className="textarea-field"
              rows={3}
              value={formState.metadataText}
              onChange={(event) => updateForm("metadataText", event.target.value)}
            />
            {formErrors.metadataText && <span className="field-hint error-text">{formErrors.metadataText}</span>}
          </label>
          <div className="style-actions">
            <button type="button" className="button primary" onClick={handleSave}>
              Save Style
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
            <button type="button" className="button tertiary" onClick={handleDelete}>
              Delete
            </button>
          </div>
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
