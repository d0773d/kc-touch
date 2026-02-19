import { ChangeEvent, FormEvent, KeyboardEvent, useMemo, useRef, useState } from "react";
import { useProject } from "../context/ProjectContext";
import { exportTranslations, importTranslations } from "../utils/api";
import type { ValidationIssue } from "../types/yamui";

interface LocaleStats {
  code: string;
  label: string;
  keyCount: number;
  missingVisible: number;
}

interface TranslationManagerProps {
  issues: ValidationIssue[];
}

interface TranslationIssueDetail {
  issue: ValidationIssue;
  locale: string;
  key: string;
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
  } = useProject();
  const translations = project.translations ?? {};
  const localeCodes = useMemo(() => Object.keys(translations).sort((a, b) => a.localeCompare(b)), [translations]);
  const [newLocaleCode, setNewLocaleCode] = useState("");
  const [newLocaleLabel, setNewLocaleLabel] = useState("");
  const [newKeyName, setNewKeyName] = useState("");
  const [filterQuery, setFilterQuery] = useState("");
  const [showMissingOnly, setShowMissingOnly] = useState(false);
  const [localeLabelDrafts, setLocaleLabelDrafts] = useState<Record<string, string>>({});
  const [keyDrafts, setKeyDrafts] = useState<Record<string, string>>({});
  const [renameError, setRenameError] = useState<string | null>(null);
  const [localeError, setLocaleError] = useState<string | null>(null);
  const [keyError, setKeyError] = useState<string | null>(null);
  const [exportBusy, setExportBusy] = useState<"json" | "csv" | null>(null);
  const [importBusy, setImportBusy] = useState(false);
  const [ioStatus, setIoStatus] = useState<string | null>(null);
  const [ioError, setIoError] = useState<string | null>(null);
  const [pendingImportFormat, setPendingImportFormat] = useState<"json" | "csv">("json");
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

  const localeStats: LocaleStats[] = useMemo(() => {
    return localeCodes.map((code) => {
      const entry = translations[code];
      const entries = entry?.entries ?? {};
      const missingVisible = visibleKeys.reduce((count, key) => {
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
  }, [localeCodes, translations, visibleKeys]);

  const totalMissingVisible = useMemo(
    () => localeStats.reduce((sum, stats) => sum + stats.missingVisible, 0),
    [localeStats]
  );

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
        {(filterQuery || showMissingOnly) && (
          <button
            type="button"
            className="button tertiary"
            onClick={() => {
              setFilterQuery("");
              setShowMissingOnly(false);
            }}
          >
            Clear filters
          </button>
        )}
      </div>

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
        {visibleKeys.length === 0 && (
          <div className="translation-table__empty">No keys match the current filters.</div>
        )}
        {visibleKeys.map((key) => (
          <div key={key} className="translation-table__row" style={tableColumnsStyle}>
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
              <button type="button" className="button tertiary" onClick={() => deleteTranslationKey(key)}>
                Delete
              </button>
            </div>
          </div>
        ))}
      </div>
    </section>
  );
}
