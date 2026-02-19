import { ChangeEvent, FormEvent, KeyboardEvent, useCallback, useEffect, useMemo, useRef, useState } from "react";
import { useProject } from "../context/ProjectContext";
import { exportTranslations, importTranslations } from "../utils/api";
import { getPrimaryLocale } from "../utils/translation";
import type { ValidationIssue } from "../types/yamui";
import Modal from "./Modal";

interface LocaleStats {
  code: string;
  label: string;
  keyCount: number;
  missingVisible: number;
}

interface TranslationManagerProps {
  issues: ValidationIssue[];
}

interface FillPreviewState {
  key: string;
  source: string;
  targets: Array<{ code: string; before: string }>;
}

interface TranslationIssueDetail {
  issue: ValidationIssue;
  locale: string;
  key: string;
}

interface DiffViewerProps {
  title: string;
  sourceLabel: string;
  sourceValue: string;
  targets: Array<{ code: string; before: string }>;
  onConfirm: (locales: string[]) => void;
  onDismiss: () => void;
}

const MAX_TRANSLATION_ISSUE_PREVIEW = 4;
const TRANSLATION_ISSUE_REGEX = /^\/translations\/([^/]+)\/entries\/(.+)$/;

const safeDecode = (value: string): string => {
  try {
    return decodeURIComponent(value);
  } catch {
    return value;
  }
};

const parseTranslationIssue = (issue: ValidationIssue): TranslationIssueDetail | null => {
  const match = TRANSLATION_ISSUE_REGEX.exec(issue.path);
  if (!match) {
    return null;
  }
  const [, rawLocale, rawKey] = match;
  return {
    issue,
    locale: safeDecode(rawLocale),
    key: safeDecode(rawKey),
  };
};

const buildColumnsTemplate = (localeCount: number): string => {
  const safeCount = Math.max(1, localeCount);
  return `minmax(200px, 240px) repeat(${safeCount}, minmax(160px, 1fr)) 72px`;
};

const normalizeKey = (value: string): string => value.trim();

const escapeSelector = (value: string): string => {
  if (typeof window !== "undefined" && typeof window.CSS !== "undefined" && typeof window.CSS.escape === "function") {
    return window.CSS.escape(value);
  }
  return value.replace(/["\\]/g, "\\$&");
};

export default function TranslationManager({ issues }: TranslationManagerProps): JSX.Element | null {
  const {
    project,
    addTranslationLocale,
    removeTranslationLocale,
    setTranslationLocaleLabel,
    addTranslationKey,
    renameTranslationKey,
    updateTranslationValue,
    deleteTranslationKey,
    setTranslations,
    translationFocusRequest,
    clearTranslationFocusRequest,
  } = useProject();
  const translations = useMemo(() => project.translations ?? {}, [project.translations]);
  const primaryLocale = useMemo(() => getPrimaryLocale(project), [project]);
  const primaryLocaleLabel = primaryLocale ? translations[primaryLocale]?.label ?? primaryLocale : null;
  const localeCodes = useMemo(() => Object.keys(translations).sort((a, b) => a.localeCompare(b)), [translations]);
  const [newLocaleCode, setNewLocaleCode] = useState("");
  const [newLocaleLabel, setNewLocaleLabel] = useState("");
  const [newKeyName, setNewKeyName] = useState("");
  const [filterQuery, setFilterQuery] = useState("");
  const [showMissingOnly, setShowMissingOnly] = useState(false);
  const [missingLocaleFilter, setMissingLocaleFilter] = useState<string | null>(null);
  const [localeLabelDrafts, setLocaleLabelDrafts] = useState<Record<string, string>>({});
  const [keyDrafts, setKeyDrafts] = useState<Record<string, string>>({});
  const [renameError, setRenameError] = useState<string | null>(null);
  const [activeDiff, setActiveDiff] = useState<FillPreviewState | null>(null);
  const [localeError, setLocaleError] = useState<string | null>(null);
  const [keyError, setKeyError] = useState<string | null>(null);
  const [exportBusy, setExportBusy] = useState<"json" | "csv" | null>(null);
  const [importBusy, setImportBusy] = useState(false);
  const [ioStatus, setIoStatus] = useState<string | null>(null);
  const [ioError, setIoError] = useState<string | null>(null);
  const [pendingImportFormat, setPendingImportFormat] = useState<"json" | "csv">("json");
  const [handoffMessage, setHandoffMessage] = useState<string | null>(null);
  const [focusedKey, setFocusedKey] = useState<string | null>(null);
  const [pendingFocusKey, setPendingFocusKey] = useState<string | null>(null);
  const fileInputRef = useRef<HTMLInputElement | null>(null);
  const translationIssues = useMemo(
    () => (issues ?? []).filter((issue) => issue.path.startsWith("/translations")),
    [issues]
  );
  const translationIssueDetails = useMemo(
    () =>
      translationIssues
        .map(parseTranslationIssue)
        .filter((entry): entry is TranslationIssueDetail => Boolean(entry)),
    [translationIssues]
  );
  const translationIssuesByCell = useMemo(() => {
    const map = new Map<string, TranslationIssueDetail[]>();
    translationIssueDetails.forEach((detail) => {
      const key = `${detail.locale}::${detail.key}`;
      const bucket = map.get(key);
      if (bucket) {
        bucket.push(detail);
      } else {
        map.set(key, [detail]);
      }
    });
    return map;
  }, [translationIssueDetails]);
  const translationIssueLocaleCounts = useMemo(() => {
    const counts: Record<string, number> = {};
    translationIssueDetails.forEach((detail) => {
      counts[detail.locale] = (counts[detail.locale] ?? 0) + 1;
    });
    return counts;
  }, [translationIssueDetails]);
  const translationIssueSummary = useMemo(() => {
    const entries = Object.entries(translationIssueLocaleCounts);
    if (!entries.length) {
      return "";
    }
    return entries
      .sort((a, b) => a[0].localeCompare(b[0]))
      .map(([locale, count]) => `${locale} (${count})`)
      .join(", ");
  }, [translationIssueLocaleCounts]);

  const allKeys = useMemo(() => {
    const keySet = new Set<string>();
    Object.values(translations).forEach((locale) => {
      Object.keys(locale?.entries ?? {}).forEach((key) => keySet.add(key));
    });
    return Array.from(keySet).sort((a, b) => a.localeCompare(b));
  }, [translations]);

  const visibleKeys = useMemo(() => {
    const query = filterQuery.trim().toLowerCase();
    return allKeys.filter((key) => {
      const matchesQuery = !query || key.toLowerCase().includes(query);
      if (!matchesQuery) {
        return false;
      }
      if (!showMissingOnly) {
        return true;
      }
      return localeCodes.some((code) => {
        const value = translations[code]?.entries?.[key];
        return !value || !value.trim().length;
      });
    });
  }, [allKeys, filterQuery, localeCodes, showMissingOnly, translations]);

  const missingLocalesByKey = useMemo(() => {
    const map = new Map<string, string[]>();
    visibleKeys.forEach((key) => {
      const missing = localeCodes.filter((code) => {
        const value = translations[code]?.entries?.[key];
        return !value || !value.trim().length;
      });
      map.set(key, missing);
    });
    return map;
  }, [localeCodes, translations, visibleKeys]);

  const filteredKeys = useMemo(() => {
    if (!missingLocaleFilter) {
      return visibleKeys;
    }
    return visibleKeys.filter((key) => (missingLocalesByKey.get(key) ?? []).includes(missingLocaleFilter));
  }, [missingLocaleFilter, missingLocalesByKey, visibleKeys]);

  const handleFillMissingFromPrimary = useCallback(
    (key: string, previewOnly = false, selectedLocales?: string[]) => {
      if (!primaryLocale) {
        return;
      }
      const baseline = translations[primaryLocale]?.entries?.[key];
      const source = baseline?.trim();
      if (!source) {
        return;
      }
      const missingTargets = localeCodes.reduce<Array<{ code: string; before: string }>>((list, code) => {
        if (code === primaryLocale) {
          return list;
        }
        const value = translations[code]?.entries?.[key];
        if (!value || !value.trim().length) {
          list.push({ code, before: value?.trim() ?? "" });
        }
        return list;
      }, []);
      if (!missingTargets.length) {
        return;
      }
      if (previewOnly) {
        setActiveDiff({ key, source, targets: missingTargets });
        return;
      }
      const allowed = selectedLocales && selectedLocales.length ? new Set(selectedLocales) : null;
      const targetsToFill = missingTargets
        .map(({ code }) => code)
        .filter((code) => !allowed || allowed.has(code));
      if (!targetsToFill.length) {
        return;
      }
      targetsToFill.forEach((code) => updateTranslationValue(code, key, source));
      setIoError(null);
      setIoStatus(`Filled ${targetsToFill.length} locale${targetsToFill.length === 1 ? "" : "s"} for ${key}`);
      setActiveDiff(null);
    },
    [localeCodes, primaryLocale, setIoError, setIoStatus, translations, updateTranslationValue]
  );

  const missingLocaleChips = useMemo<Array<{ code: string; count: number }>>(() => {
    if (!visibleKeys.length) {
      return [];
    }
    const counts: Record<string, number> = {};
    visibleKeys.forEach((key) => {
      (missingLocalesByKey.get(key) ?? []).forEach((code) => {
        counts[code] = (counts[code] ?? 0) + 1;
      });
    });
    return Object.entries(counts)
      .filter(([, count]) => count > 0)
      .map(([code, count]) => ({ code, count }))
      .sort((a, b) => b.count - a.count || a.code.localeCompare(b.code));
  }, [missingLocalesByKey, visibleKeys]);

  const localeStats: LocaleStats[] = useMemo(() => {
    return localeCodes.map((code) => {
      const entry = translations[code];
      const entries = entry?.entries ?? {};
      const missingVisible = filteredKeys.reduce((count, key) => {
        const value = entries[key];
        return !value || !value.trim().length ? count + 1 : count;
      }, 0);
      return {
        code,
        label: entry?.label ?? "",
        keyCount: Object.keys(entries).length,
        missingVisible,
      };
    });
  }, [filteredKeys, localeCodes, translations]);

  const totalMissingVisible = useMemo(
    () => localeStats.reduce((sum, stats) => sum + stats.missingVisible, 0),
    [localeStats]
  );

  useEffect(() => {
    if (!translationFocusRequest) {
      return;
    }
    const { key, origin } = translationFocusRequest;
    if (!allKeys.includes(key)) {
      setIoError(`Translation key "${key}" not found`);
      setHandoffMessage(null);
      clearTranslationFocusRequest();
      return;
    }
    setFilterQuery("");
    setShowMissingOnly(false);
    setMissingLocaleFilter(null);
    setPendingFocusKey(key);
    setIoError(null);
    setHandoffMessage(origin === "inspector" ? `Jumped to ${key} from Inspector` : `Jumped to ${key}`);
    clearTranslationFocusRequest();
  }, [allKeys, clearTranslationFocusRequest, translationFocusRequest]);

  useEffect(() => {
    if (!handoffMessage) {
      return;
    }
    const timer = window.setTimeout(() => setHandoffMessage(null), 3500);
    return () => window.clearTimeout(timer);
  }, [handoffMessage]);

  useEffect(() => {
    if (!pendingFocusKey || !filteredKeys.includes(pendingFocusKey)) {
      return;
    }
    const scrollToRow = () => {
      if (typeof document !== "undefined") {
        const selector = `[data-translation-key="${escapeSelector(pendingFocusKey)}"]`;
        const node = document.querySelector(selector);
        if (node instanceof HTMLElement) {
          node.scrollIntoView({ behavior: "smooth", block: "center" });
        }
      }
      setFocusedKey(pendingFocusKey);
      setPendingFocusKey(null);
    };
    if (typeof window === "undefined") {
      scrollToRow();
      return;
    }
    const frame = window.requestAnimationFrame(scrollToRow);
    return () => window.cancelAnimationFrame(frame);
  }, [filteredKeys, pendingFocusKey]);

  useEffect(() => {
    if (!focusedKey) {
      return;
    }
    const timer = window.setTimeout(() => setFocusedKey(null), 4000);
    return () => window.clearTimeout(timer);
  }, [focusedKey]);

  if (localeCodes.length === 0) {
    return null;
  }

  const tableColumnsStyle = { gridTemplateColumns: buildColumnsTemplate(localeCodes.length) };
  const keyCount = allKeys.length;
  const summarizeIssues = (issues: ValidationIssue[]): string | null => {
    if (!issues.length) {
      return null;
    }
    if (issues.length === 1) {
      return issues[0]?.message ?? "Validation issue detected";
    }
    return `${issues.length} validation issues detected (${issues[0]?.message ?? "see validator"})`;
  };

  const handleExport = async (format: "json" | "csv") => {
    setExportBusy(format);
    setIoError(null);
    setIoStatus(null);
    try {
      const response = await exportTranslations(project, format);
      const blob = new Blob([response.content], { type: response.mime_type });
      const url = URL.createObjectURL(blob);
      const link = document.createElement("a");
      link.href = url;
      link.download = response.filename;
      document.body.appendChild(link);
      link.click();
      link.remove();
      URL.revokeObjectURL(url);
      const issueSummary = summarizeIssues(response.issues);
      setIoStatus(issueSummary ? `Exported with warnings: ${issueSummary}` : `Exported ${response.filename}`);
    } catch (error) {
      setIoError(error instanceof Error ? error.message : "Unable to export translations");
    } finally {
      setExportBusy(null);
    }
  };

  const handleImportClick = (format: "json" | "csv") => {
    setPendingImportFormat(format);
    setIoError(null);
    setIoStatus(null);
    if (!fileInputRef.current) {
      return;
    }
    fileInputRef.current.value = "";
    fileInputRef.current.accept = format === "csv" ? ".csv,text/csv" : "application/json,.json";
    fileInputRef.current.click();
  };

  const handleFileChange = async (event: ChangeEvent<HTMLInputElement>) => {
    const file = event.target.files?.[0];
    if (!file) {
      return;
    }
    setImportBusy(true);
    setIoError(null);
    setIoStatus(null);
    try {
      const content = await file.text();
      const response = await importTranslations(project, pendingImportFormat, content);
      setTranslations(response.translations);
      const localeCount = Object.keys(response.translations ?? {}).length;
      const issueSummary = summarizeIssues(response.issues);
      setIoStatus(
        issueSummary
          ? `Imported with warnings: ${issueSummary}`
          : `Imported ${localeCount} locale${localeCount === 1 ? "" : "s"}`
      );
    } catch (error) {
      setIoError(error instanceof Error ? error.message : "Unable to import translations");
    } finally {
      setImportBusy(false);
      event.target.value = "";
    }
  };

  const handleLocaleSubmit = (event: FormEvent) => {
    event.preventDefault();
    const code = normalizeKey(newLocaleCode);
    if (!code) {
      setLocaleError("Enter a locale code");
      return;
    }
    if (translations[code]) {
      setLocaleError(`Locale "${code}" already exists`);
      return;
    }
    addTranslationLocale(code, newLocaleLabel.trim() || undefined);
    setNewLocaleCode("");
    setNewLocaleLabel("");
    setLocaleLabelDrafts((prev) => {
      const next = { ...prev };
      delete next[code];
      return next;
    });
    setLocaleError(null);
  };

  const handleKeySubmit = (event: FormEvent) => {
    event.preventDefault();
    const key = normalizeKey(newKeyName);
    if (!key) {
      setKeyError("Enter a translation key");
      return;
    }
    if (allKeys.includes(key)) {
      setKeyError(`Key "${key}" already exists`);
      return;
    }
    addTranslationKey(key);
    setNewKeyName("");
    setKeyError(null);
  };

  const handleLocaleLabelBlur = (code: string) => {
    const draft = localeLabelDrafts[code];
    if (draft === undefined) {
      return;
    }
    setTranslationLocaleLabel(code, draft);
    setLocaleLabelDrafts((prev) => {
      const next = { ...prev };
      delete next[code];
      return next;
    });
  };

  const handleKeyDraftCommit = (originalKey: string) => {
    const draft = keyDrafts[originalKey];
    if (draft === undefined) {
      setRenameError(null);
      return;
    }
    const nextName = normalizeKey(draft);
    if (!nextName || nextName === originalKey) {
      setKeyDrafts((prev) => {
        const next = { ...prev };
        delete next[originalKey];
        return next;
      });
      setRenameError(null);
      return;
    }
    if (allKeys.some((key) => key === nextName)) {
      setRenameError(`Key "${nextName}" already exists`);
      return;
    }
    const renamed = renameTranslationKey(originalKey, nextName);
    if (!renamed) {
      setRenameError(`Unable to rename "${originalKey}"`);
      return;
    }
    setRenameError(null);
    setKeyDrafts((prev) => {
      const next = { ...prev };
      delete next[originalKey];
      return next;
    });
  };

  const handleKeyInputChange = (key: string, event: ChangeEvent<HTMLInputElement>) => {
    const value = event.target.value;
    setKeyDrafts((prev) => ({ ...prev, [key]: value }));
  };

  const handleKeyInputKeyDown = (key: string, event: KeyboardEvent<HTMLInputElement>) => {
    if (event.key === "Enter") {
      event.preventDefault();
      handleKeyDraftCommit(key);
    }
    if (event.key === "Escape") {
      setKeyDrafts((prev) => {
        const next = { ...prev };
        delete next[key];
        return next;
      });
      setRenameError(null);
    }
  };

  const handleValueChange = (locale: string, key: string, value: string) => {
    updateTranslationValue(locale, key, value);
  };

  return (
    <section className="translation-manager" id="translation-manager">
      <div className="translation-manager__header">
        <div>
          <p className="section-title" style={{ marginBottom: 4 }}>
            Translation Library
          </p>
          <p className="field-hint">Manage locales and copy-ready strings for the YAML export.</p>
        </div>
        <div className="translation-manager__stats">
          <span>
            <strong>{localeCodes.length}</strong> locales
          </span>
          <span>
            <strong>{keyCount}</strong> keys
          </span>
          <span>
            <strong>{totalMissingVisible}</strong> missing
          </span>
        </div>
      </div>

        {handoffMessage && (
          <p className="translation-manager__handoff-toast" aria-live="polite">
            {handoffMessage}
          </p>
        )}

        {translationIssueDetails.length > 0 && (
          <div className="translation-manager__issues">
            <div className="translation-manager__issues-header">
              <span>
                ⚠ {translationIssueDetails.length} translation warning{translationIssueDetails.length === 1 ? "" : "s"}
              </span>
              <button
                type="button"
                className="button tertiary"
                onClick={() => {
                  setShowMissingOnly(true);
                  setFilterQuery("");
                }}
              >
                Show missing entries
              </button>
            </div>
            {translationIssueSummary && (
              <p className="translation-manager__issue-summary">Locales impacted: {translationIssueSummary}</p>
            )}
            <ul className="translation-manager__issue-list">
              {translationIssueDetails.slice(0, MAX_TRANSLATION_ISSUE_PREVIEW).map((detail) => (
                <li key={`${detail.locale}-${detail.key}`}>
                  <strong>{detail.key}</strong> · {detail.locale} — {detail.issue.message}
                </li>
              ))}
            </ul>
            {translationIssueDetails.length > MAX_TRANSLATION_ISSUE_PREVIEW && (
              <p className="translation-manager__issue-footnote">
                Showing {MAX_TRANSLATION_ISSUE_PREVIEW} of {translationIssueDetails.length} warnings.
              </p>
            )}
          </div>
        )}

      <div className="translation-manager__io">
        <div className="translation-manager__io-group">
          <button
            type="button"
            className="button tertiary"
            onClick={() => handleExport("json")}
            disabled={Boolean(importBusy || exportBusy !== null)}
          >
            {exportBusy === "json" ? "Exporting JSON..." : "Export JSON"}
          </button>
          <button
            type="button"
            className="button tertiary"
            onClick={() => handleExport("csv")}
            disabled={Boolean(importBusy || exportBusy !== null)}
          >
            {exportBusy === "csv" ? "Exporting CSV..." : "Export CSV"}
          </button>
        </div>
        <div className="translation-manager__io-group">
          <button
            type="button"
            className="button secondary"
            onClick={() => handleImportClick("json")}
            disabled={Boolean(importBusy || exportBusy !== null)}
          >
            {importBusy && pendingImportFormat === "json" ? "Importing JSON..." : "Import JSON"}
          </button>
          <button
            type="button"
            className="button secondary"
            onClick={() => handleImportClick("csv")}
            disabled={Boolean(importBusy || exportBusy !== null)}
          >
            {importBusy && pendingImportFormat === "csv" ? "Importing CSV..." : "Import CSV"}
          </button>
        </div>
      </div>
      {(ioStatus || ioError) && (
        <p className={`field-hint ${ioError ? "error-text" : ""}`} aria-live="polite">
          {ioError ?? ioStatus}
        </p>
      )}
      <input
        ref={fileInputRef}
        type="file"
        style={{ display: "none" }}
        onChange={handleFileChange}
        accept={pendingImportFormat === "csv" ? ".csv,text/csv" : "application/json,.json"}
      />

      <form className="translation-manager__form" onSubmit={handleLocaleSubmit}>
        <input
          className="input-field"
          placeholder="Add locale code (en, es-MX)"
          value={newLocaleCode}
          onChange={(event) => setNewLocaleCode(event.target.value)}
        />
        <input
          className="input-field"
          placeholder="Display label (optional)"
          value={newLocaleLabel}
          onChange={(event) => setNewLocaleLabel(event.target.value)}
        />
        <button type="submit" className="button secondary">
          Add locale
        </button>
      </form>
      {localeError && <p className="field-hint error-text">{localeError}</p>}

      <form className="translation-manager__form" onSubmit={handleKeySubmit}>
        <input
          className="input-field"
          placeholder="Add translation key (e.g., panels.status.title)"
          value={newKeyName}
          onChange={(event) => setNewKeyName(event.target.value)}
        />
        <button type="submit" className="button secondary">
          Add key
        </button>
      </form>
      {keyError && <p className="field-hint error-text">{keyError}</p>}

      <div className="translation-manager__controls">
        <input
          className="input-field"
          placeholder="Filter keys"
          value={filterQuery}
          onChange={(event) => setFilterQuery(event.target.value)}
        />
        <label className="translation-manager__toggle">
          <input
            type="checkbox"
            checked={showMissingOnly}
            onChange={(event) => setShowMissingOnly(event.target.checked)}
          />
          Show missing only
        </label>
        {(filterQuery || showMissingOnly || missingLocaleFilter) && (
          <button
            type="button"
            className="button tertiary"
            onClick={() => {
              setFilterQuery("");
              setShowMissingOnly(false);
              setMissingLocaleFilter(null);
            }}
          >
            Clear filters
          </button>
        )}
      </div>
      {missingLocaleChips.length > 0 && (
        <div className="translation-manager__missing-chips">
          {missingLocaleChips.map(({ code, count }) => (
            <button
              key={code}
              type="button"
              className={`missing-chip ${missingLocaleFilter === code ? "is-active" : ""}`}
              onClick={() => setMissingLocaleFilter((current) => (current === code ? null : code))}
            >
              {code} · {count}
            </button>
          ))}
          {missingLocaleFilter && (
            <button type="button" className="button tertiary" onClick={() => setMissingLocaleFilter(null)}>
              Clear locale filter
            </button>
          )}
        </div>
      )}

      <div className="translation-manager__locales">
        {localeStats.map((stats) => {
          const labelDraft = localeLabelDrafts[stats.code];
          const labelValue = labelDraft ?? stats.label;
          const canRemove = localeCodes.length > 1;
          return (
            <div key={stats.code} className="translation-locale-card">
              <div className="translation-locale-card__header">
                <span className="translation-locale-card__code">{stats.code}</span>
                <button
                  type="button"
                  className="button tertiary"
                  onClick={() => removeTranslationLocale(stats.code)}
                  disabled={!canRemove}
                  title={canRemove ? "Remove locale" : "Keep at least one locale"}
                >
                  Remove
                </button>
              </div>
              <input
                className="input-field translation-locale-card__label"
                placeholder="Locale label"
                value={labelValue}
                onChange={(event) =>
                  setLocaleLabelDrafts((prev) => ({ ...prev, [stats.code]: event.target.value }))
                }
                onBlur={() => handleLocaleLabelBlur(stats.code)}
              />
              <p className="translation-locale-card__meta">
                <span>{stats.keyCount} keys</span>
                <span>{stats.missingVisible} missing</span>
              </p>
            </div>
          );
        })}
      </div>

      {renameError && <p className="field-hint error-text">{renameError}</p>}

      <div className="translation-table">
        <div className="translation-table__row translation-table__row--head" style={tableColumnsStyle}>
          <div className="translation-table__cell translation-table__cell--key">Key</div>
          {localeCodes.map((code) => (
            <div key={code} className="translation-table__cell translation-table__cell--locale">
              {code}
            </div>
          ))}
          <div className="translation-table__cell translation-table__cell--actions">Actions</div>
        </div>
        {filteredKeys.length === 0 && (
          <div className="translation-table__empty">No keys match the current filters.</div>
        )}
        {filteredKeys.map((key) => (
          <div
            key={key}
            data-translation-key={key}
            className={`translation-table__row${focusedKey === key ? " is-focused" : ""}`}
            style={tableColumnsStyle}
          >
            <div className="translation-table__cell translation-table__cell--key">
              <input
                className="input-field translation-table__key-input"
                value={keyDrafts[key] ?? key}
                onChange={(event) => handleKeyInputChange(key, event)}
                onBlur={() => handleKeyDraftCommit(key)}
                onKeyDown={(event) => handleKeyInputKeyDown(key, event)}
              />
            </div>
            {localeCodes.map((code) => {
              const value = translations[code]?.entries?.[key] ?? "";
              const isMissing = !value || !value.trim().length;
              const cellIssues = translationIssuesByCell.get(`${code}::${key}`) ?? [];
              const hasIssue = cellIssues.length > 0;
              const cellClasses = ["translation-table__cell", "translation-table__cell--locale"];
              if (isMissing) {
                cellClasses.push("is-missing");
              }
              if (hasIssue) {
                cellClasses.push("has-issue");
              }
              const issueTitle = hasIssue ? cellIssues.map((entry) => entry.issue.message).join("\n") : undefined;
              return (
                <div key={`${key}-${code}`} className={cellClasses.join(" ")} title={issueTitle}>
                  <textarea
                    className="input-field translation-table__textarea"
                    value={value}
                    placeholder="Enter translation"
                    onChange={(event) => handleValueChange(code, key, event.target.value)}
                    aria-label={`Translation for ${key} (${code})`}
                    rows={1}
                  />
                  {hasIssue && (
                    <span className="translation-table__issue-hint">
                      {cellIssues[0]?.issue.message ?? "Translation issue detected"}
                    </span>
                  )}
                </div>
              );
            })}
            <div className="translation-table__cell translation-table__cell--actions">
              {(() => {
                const missingLocales = missingLocalesByKey.get(key) ?? [];
                const missingTitle = missingLocales.length ? `Missing locales: ${missingLocales.join(", ")}` : "All locales populated";
                const displayList = missingLocales.slice(0, 3).join(", ");
                const overflowCount = Math.max(0, missingLocales.length - 3);
                const primaryValue = primaryLocale ? translations[primaryLocale]?.entries?.[key]?.trim() : "";
                const canFill = Boolean(
                  primaryLocale &&
                  primaryValue &&
                  missingLocales.some((code) => code !== primaryLocale)
                );
                return (
                  <div className="translation-table__missing-tools" title={missingTitle}>
                    <span className={`translation-table__missing-pill ${missingLocales.length ? "is-missing" : "is-complete"}`}>
                      {missingLocales.length === 0
                        ? "Complete"
                        : overflowCount > 0
                          ? `${displayList} +${overflowCount}`
                          : displayList || missingLocales[0]}
                    </span>
                    <button
                      type="button"
                      className="button tertiary"
                      onClick={() => handleFillMissingFromPrimary(key, true)}
                      disabled={!canFill}
                    >
                      {primaryLocaleLabel ? `Preview fill from ${primaryLocaleLabel}` : "Preview fill"}
                    </button>
                  </div>
                );
              })()}
              <button type="button" className="button tertiary" onClick={() => deleteTranslationKey(key)}>
                Delete
              </button>
            </div>
          </div>
        ))}
      </div>
      {activeDiff && (
        <DiffViewer
          title={`Fill ${activeDiff.key}`}
          sourceLabel={primaryLocaleLabel ?? primaryLocale ?? "primary"}
          sourceValue={activeDiff.source}
          targets={activeDiff.targets}
          onConfirm={(locales) => handleFillMissingFromPrimary(activeDiff.key, false, locales)}
          onDismiss={() => setActiveDiff(null)}
        />
      )}
    </section>
  );
}

function DiffViewer({ title, sourceLabel, sourceValue, targets, onConfirm, onDismiss }: DiffViewerProps) {
  const sortedTargets = useMemo(() => targets.slice().sort((a, b) => a.code.localeCompare(b.code)), [targets]);
  const [selectedLocales, setSelectedLocales] = useState<Set<string>>(() => new Set(sortedTargets.map((entry) => entry.code)));

  useEffect(() => {
    setSelectedLocales(new Set(sortedTargets.map((entry) => entry.code)));
  }, [sortedTargets]);

  const toggleLocale = (code: string) => {
    setSelectedLocales((prev) => {
      const next = new Set(prev);
      if (next.has(code)) {
        next.delete(code);
      } else {
        next.add(code);
      }
      return next;
    });
  };

  const handleSelectAll = (value: boolean) => {
    if (value) {
      setSelectedLocales(new Set(sortedTargets.map((entry) => entry.code)));
    } else {
      setSelectedLocales(new Set());
    }
  };

  const selectedCount = selectedLocales.size;
  const footerMessage = selectedCount === sortedTargets.length
    ? `${selectedCount} locale${selectedCount === 1 ? "" : "s"} will be updated`
    : `Updating ${selectedCount} of ${sortedTargets.length} locales`;

  const handleConfirm = () => {
    if (!selectedCount) {
      return;
    }
    onConfirm(Array.from(selectedLocales));
  };

  return (
    <Modal
      title={title}
      onClose={onDismiss}
      width={560}
      footer={
        <div className="diff-viewer__footer">
          <span className="diff-viewer__count">{footerMessage}</span>
          <div className="diff-viewer__footer-actions">
            <button type="button" className="button tertiary" onClick={() => handleSelectAll(false)} disabled={!selectedCount}>
              Clear
            </button>
            <button
              type="button"
              className="button secondary"
              onClick={handleConfirm}
              disabled={!selectedCount}
            >
              Fill missing locales
            </button>
          </div>
        </div>
      }
    >
      <div className="diff-viewer">
        <section className="diff-viewer__section">
          <p className="field-label">{sourceLabel}</p>
          <pre className="diff-viewer__source" aria-label="Primary locale value">
            {sourceValue}
          </pre>
        </section>
        <section className="diff-viewer__section">
          <div className="diff-viewer__targets-header">
            <p className="field-label">Locales missing this key</p>
            <button
              type="button"
              className="button tertiary"
              onClick={() => handleSelectAll(selectedCount !== sortedTargets.length)}
            >
              {selectedCount === sortedTargets.length ? "Unselect all" : "Select all"}
            </button>
          </div>
          <ul className="diff-viewer__targets">
            {sortedTargets.map(({ code, before }) => (
              <li key={code}>
                <label className={`diff-viewer__target ${selectedLocales.has(code) ? "is-selected" : ""}`}>
                  <input
                    type="checkbox"
                    checked={selectedLocales.has(code)}
                    onChange={() => toggleLocale(code)}
                    aria-label={`${code} locale toggle`}
                  />
                  <div className="diff-viewer__target-body">
                    <span className="diff-viewer__target-code">{code}</span>
                    <span className="diff-viewer__target-before">
                      {before?.length ? `Existing: ${before}` : "Empty (will copy primary)"}
                    </span>
                  </div>
                </label>
              </li>
            ))}
          </ul>
          <p className="field-hint">Only checked locales receive the primary copy. Existing translations remain unchanged.</p>
        </section>
      </div>
    </Modal>
  );
}
