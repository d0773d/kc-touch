import { ChangeEvent, useCallback, useMemo, useState } from "react";
import { useProject } from "../context/ProjectContext";
import {
  exportProject as exportProjectApi,
  importProject as importProjectApi,
  lintStyles,
  validateProject,
} from "../utils/api";
import { ValidationIssue } from "../types/yamui";
import Modal from "./Modal";

interface Props {
  onIssues: (issues: ValidationIssue[]) => void;
}

export default function ProjectToolbar({ onIssues }: Props): JSX.Element {
  const { project, setProject, lastExport, setLastExport, loadTemplateProject } = useProject();
  const [busy, setBusy] = useState(false);
  const [showImport, setShowImport] = useState(false);
  const [showExport, setShowExport] = useState(false);
  const [importYaml, setImportYaml] = useState("{}");
  const [importError, setImportError] = useState<string | null>(null);
  const [importBusy, setImportBusy] = useState(false);
  const [importFileName, setImportFileName] = useState<string | null>(null);

  const [exportYaml, setExportYaml] = useState("Loading...");
  const [exportBusy, setExportBusy] = useState(false);
  const [exportIssues, setExportIssues] = useState<ValidationIssue[]>([]);
  const [exportError, setExportError] = useState<string | null>(null);
  const [sampleBusy, setSampleBusy] = useState(false);
  const [styleLintIssues, setStyleLintIssues] = useState<ValidationIssue[]>([]);
  const [styleLintBusy, setStyleLintBusy] = useState(false);
  const [styleLintError, setStyleLintError] = useState<string | null>(null);

  const handleValidate = async () => {
    setBusy(true);
    try {
      const response = await validateProject({ project });
      onIssues(response.issues);
      const errorCount = response.issues.filter((issue) => issue.severity === "error").length;
      const warningCount = response.issues.length - errorCount;
      if (errorCount === 0) {
        const warningNote = warningCount ? ` but ${warningCount} warning${warningCount === 1 ? "" : "s"}` : "";
        window.alert(`Project is valid${warningNote}.`);
      } else {
        window.alert(`Project has ${errorCount} error${errorCount === 1 ? "" : "s"}${warningCount ? ` and ${warningCount} warning${warningCount === 1 ? "" : "s"}` : ""}.`);
      }
    } catch (error) {
      window.alert((error as Error).message);
    } finally {
      setBusy(false);
    }
  };

  const openImportModal = () => {
    setImportYaml("{}");
    setImportFileName(null);
    setImportError(null);
    setShowImport(true);
  };

  const handleImportFile = (event: ChangeEvent<HTMLInputElement>) => {
    const file = event.target.files?.[0];
    if (!file) {
      return;
    }
    setImportFileName(file.name);
    const reader = new FileReader();
    reader.onload = () => {
      setImportYaml(String(reader.result ?? ""));
    };
    reader.readAsText(file);
  };

  const submitImport = async () => {
    if (!importYaml.trim()) {
      setImportError("YAML text is required");
      return;
    }
    setImportBusy(true);
    try {
      const response = await importProjectApi(importYaml);
      setProject(response.project);
      onIssues(response.issues);
      setShowImport(false);
    } catch (error) {
      setImportError((error as Error).message);
    } finally {
      setImportBusy(false);
    }
  };

  const openExportModal = async () => {
    setShowExport(true);
    setExportError(null);
    setStyleLintIssues([]);
    setStyleLintError(null);
    if (lastExport) {
      setExportYaml(lastExport.yaml);
      setExportIssues(lastExport.issues);
    }
    setExportBusy(true);
    setStyleLintBusy(true);
    try {
      const response = await exportProjectApi(project);
      setExportYaml(response.yaml);
      setExportIssues(response.issues);
      setLastExport(response);
      onIssues(response.issues);
    } catch (error) {
      setExportError((error as Error).message);
    } finally {
      setExportBusy(false);
    }
    try {
      const lintIssues = await lintStyles(project.styles);
      setStyleLintIssues(lintIssues);
    } catch (error) {
      setStyleLintError((error as Error).message);
    } finally {
      setStyleLintBusy(false);
    }
  };

  const copyExport = async () => {
    await navigator.clipboard.writeText(exportYaml);
  };

  const downloadExport = () => {
    const blob = new Blob([exportYaml], { type: "text/yaml" });
    const url = URL.createObjectURL(blob);
    const link = document.createElement("a");
    link.href = url;
    link.download = "yam-ui-project.yaml";
    link.click();
    URL.revokeObjectURL(url);
  };

  const issueSummary = useMemo(() => {
    const errors = exportIssues.filter((issue) => issue.severity === "error").length;
    const warnings = exportIssues.length - errors;
    if (!exportIssues.length) {
      return "No validation issues";
    }
    return `${errors} error${errors === 1 ? "" : "s"}, ${warnings} warning${warnings === 1 ? "" : "s"}`;
  }, [exportIssues]);

  const styleLintSummary = useMemo(() => {
    if (styleLintBusy) {
      return "Checking styles...";
    }
    if (styleLintError) {
      return styleLintError;
    }
    if (!styleLintIssues.length) {
      return "No style lint findings";
    }
    const errors = styleLintIssues.filter((issue) => issue.severity === "error").length;
    const warnings = styleLintIssues.length - errors;
    return `${errors} error${errors === 1 ? "" : "s"}, ${warnings} warning${warnings === 1 ? "" : "s"}`;
  }, [styleLintBusy, styleLintError, styleLintIssues]);

  const renderIssues = useCallback(
    (issues: ValidationIssue[]) => (
      <div className="issue-list" style={{ marginTop: 12 }}>
        {issues.map((issue) => (
          <div key={`${issue.path}-${issue.message}`} className={`issue-item ${issue.severity}`}>
            <strong>{issue.severity.toUpperCase()}</strong> {issue.message} <em>{issue.path}</em>
          </div>
        ))}
        {!issues.length && <p style={{ color: "#22c55e" }}>No validation issues</p>}
      </div>
    ),
    []
  );

  const handleLoadSample = useCallback(async () => {
    setSampleBusy(true);
    try {
      await loadTemplateProject();
      onIssues([]);
    } catch (error) {
      window.alert((error as Error).message);
    } finally {
      setSampleBusy(false);
    }
  }, [loadTemplateProject, onIssues]);

  return (
    <section className="toolbar panel">
      <div>
        <h1 style={{ margin: 0 }}>Yam UI Companion</h1>
        <p style={{ margin: 0, color: "#64748b" }}>Phase 1 drag-and-drop builder</p>
      </div>
      <div style={{ display: "flex", gap: 12 }}>
        <button className="button secondary" onClick={handleLoadSample} disabled={busy || sampleBusy}>
          {sampleBusy ? "Loading sample..." : "Load Sample"}
        </button>
        <button className="button secondary" onClick={openImportModal} disabled={busy}>
          Import YAML
        </button>
        <button className="button secondary" onClick={openExportModal} disabled={busy}>
          Export YAML
        </button>
        <button className="button primary" onClick={handleValidate} disabled={busy}>
          Validate
        </button>
      </div>
      <span style={{ fontSize: "0.75rem", color: "#94a3b8" }}>Version {__APP_VERSION__}</span>
      {showImport && (
        <Modal
          title="Import YamUI YAML"
          onClose={() => {
            if (!importBusy) {
              setShowImport(false);
            }
          }}
          width={640}
        >
          <div className="modal-grid">
            <div>
              <label className="inspector-field">
                <div className="field-label">
                  <span>Paste YAML</span>
                </div>
                <textarea
                  className="textarea-field"
                  rows={10}
                  value={importYaml}
                  onChange={(event) => {
                    setImportYaml(event.target.value);
                    setImportError(null);
                  }}
                />
              </label>
            </div>
            <div>
              <label className="inspector-field">
                <div className="field-label">
                  <span>Or upload file</span>
                </div>
                <input type="file" accept=".yaml,.yml" onChange={handleImportFile} />
                {importFileName && <span className="field-hint">Loaded: {importFileName}</span>}
              </label>
              {importError && <p style={{ color: "#f43f5e" }}>{importError}</p>}
              <button className="button primary" onClick={submitImport} disabled={importBusy}>
                {importBusy ? "Importing..." : "Import"}
              </button>
            </div>
          </div>
          {importBusy && <p style={{ color: "#0ea5e9" }}>Validating import...</p>}
        </Modal>
      )}
      {showExport && (
        <Modal title="Export YamUI YAML" onClose={() => setShowExport(false)} width={680}>
          {exportError && <p style={{ color: "#f43f5e" }}>{exportError}</p>}
          <textarea className="textarea-field" rows={12} value={exportYaml} readOnly />
          <div style={{ display: "flex", gap: 8, marginTop: 12 }}>
            <button className="button secondary" onClick={copyExport} disabled={exportBusy}>
              Copy to clipboard
            </button>
            <button className="button secondary" onClick={downloadExport} disabled={exportBusy}>
              Download .yaml
            </button>
          </div>
          <p className="field-hint" style={{ marginTop: 8 }}>{issueSummary}</p>
          {renderIssues(exportIssues)}
          <div className="style-lint-panel">
            <div className="field-label" style={{ marginTop: 16 }}>
              <span>Style Lint</span>
              {styleLintBusy && <span className="field-badge warning">Checking</span>}
            </div>
            <p className="field-hint" style={{ marginTop: 4 }}>{styleLintSummary}</p>
            {styleLintError && <p className="field-hint warning-text">{styleLintError}</p>}
            {!styleLintBusy && !styleLintError && styleLintIssues.length > 0 && renderIssues(styleLintIssues)}
          </div>
        </Modal>
      )}
    </section>
  );
}
