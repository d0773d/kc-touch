import { ChangeEvent, useCallback, useEffect, useMemo, useRef, useState, type DragEvent } from "react";
import { useProject } from "../context/ProjectContext";
import type { AssetReference } from "../types/yamui";
import AssetUploadQueue from "./AssetUploadQueue";
import { useAssetUploads } from "../hooks/useAssetUploads";

const KIND_LABELS: Record<AssetReference["kind"], string> = {
  image: "Image",
  video: "Video",
  audio: "Audio",
  font: "Font",
  binary: "Binary",
  unknown: "Unknown",
};

const KIND_OPTIONS: Array<{ value: AssetReference["kind"] | "all"; label: string }> = [
  { value: "all", label: "All kinds" },
  { value: "image", label: KIND_LABELS.image },
  { value: "video", label: KIND_LABELS.video },
  { value: "audio", label: KIND_LABELS.audio },
  { value: "font", label: KIND_LABELS.font },
  { value: "binary", label: KIND_LABELS.binary },
  { value: "unknown", label: KIND_LABELS.unknown },
];

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

export default function AssetManager(): JSX.Element {
  const {
    assetCatalog,
    assetCatalogBusy,
    assetCatalogError,
    assetCatalogLoadedAt,
    loadAssetCatalog,
    assetFilters,
    setAssetFilters,
    resetAssetFilters,
    pendingAssetUploads,
    selectedPath,
    updateWidget,
  } = useProject();
  const assetFileInputRef = useRef<HTMLInputElement | null>(null);
  const [isDroppingAsset, setIsDroppingAsset] = useState(false);
  const [copiedAssetId, setCopiedAssetId] = useState<string | null>(null);
  const { uploadFiles, dismissPendingUpload } = useAssetUploads();

  const initialRequestSent = useRef(false);

  useEffect(() => {
    if (initialRequestSent.current) {
      return;
    }
    if (assetCatalogLoadedAt === null && !assetCatalogBusy) {
      initialRequestSent.current = true;
      void loadAssetCatalog();
    }
  }, [assetCatalogBusy, assetCatalogLoadedAt, loadAssetCatalog]);

  const visibleAssets = useMemo(() => {
    const query = assetFilters.query.trim().toLowerCase();
    const tagFilters = assetFilters.tags.map((tag) => tag.toLowerCase());
    const targetFilters = assetFilters.targets.map((target) => target.toLowerCase());
    const kindFilters = assetFilters.kinds;
    return assetCatalog.filter((asset) => {
      if (kindFilters.length && !kindFilters.includes(asset.kind)) {
        return false;
      }
      if (tagFilters.length && !tagFilters.every((tag) => asset.tags.some((existing) => existing.toLowerCase() === tag))) {
        return false;
      }
      if (targetFilters.length && !asset.targets.some((target) => targetFilters.includes(target.toLowerCase()))) {
        return false;
      }
      if (!query) {
        return true;
      }
      const haystack = `${asset.label} ${asset.path} ${asset.tags.join(" ")} ${asset.targets.join(" ")}`.toLowerCase();
      return haystack.includes(query);
    });
  }, [assetCatalog, assetFilters]);

  const tagStats = useMemo(() => {
    const counts = new Map<string, number>();
    assetCatalog.forEach((asset) => {
      asset.tags.forEach((tag) => counts.set(tag, (counts.get(tag) ?? 0) + 1));
    });
    return Array.from(counts.entries())
      .sort((a, b) => b[1] - a[1])
      .slice(0, 8);
  }, [assetCatalog]);

  const targetStats = useMemo(() => {
    const counts = new Map<string, number>();
    assetCatalog.forEach((asset) => {
      asset.targets.forEach((target) => counts.set(target, (counts.get(target) ?? 0) + 1));
    });
    return Array.from(counts.entries())
      .sort((a, b) => b[1] - a[1])
      .slice(0, 6);
  }, [assetCatalog]);

  const handleQueryChange = (event: ChangeEvent<HTMLInputElement>) => {
    const value = event.target.value;
    setAssetFilters((prev) => ({ ...prev, query: value }));
  };

  const handleKindChange = (event: ChangeEvent<HTMLSelectElement>) => {
    const value = event.target.value as AssetReference["kind"] | "all";
    setAssetFilters((prev) => ({
      ...prev,
      kinds: value === "all" ? [] : [value],
    }));
  };

  const toggleTagFilter = (tag: string) => {
    setAssetFilters((prev) => {
      const hasTag = prev.tags.includes(tag);
      const tags = hasTag ? prev.tags.filter((value) => value !== tag) : [...prev.tags, tag];
      return { ...prev, tags };
    });
  };

  const toggleTargetFilter = (target: string) => {
    setAssetFilters((prev) => {
      const hasTarget = prev.targets.includes(target);
      const targets = hasTarget ? prev.targets.filter((value) => value !== target) : [...prev.targets, target];
      return { ...prev, targets };
    });
  };

  const filtersActive = Boolean(
    assetFilters.query || assetFilters.tags.length || assetFilters.targets.length || assetFilters.kinds.length
  );

  const selectedBytes = useMemo(
    () => visibleAssets.reduce((sum, asset) => sum + (asset.sizeBytes ?? 0), 0),
    [visibleAssets]
  );

  const handleFilePickerClick = useCallback(() => {
    assetFileInputRef.current?.click();
  }, []);

  const handleFileInputChange = useCallback(
    (event: ChangeEvent<HTMLInputElement>) => {
      const files = event.target.files ? Array.from(event.target.files) : [];
      if (files.length) {
        void uploadFiles(files);
      }
      event.target.value = "";
    },
    [uploadFiles]
  );

  const handleAssetDragOver = useCallback((event: DragEvent<HTMLDivElement>) => {
    event.preventDefault();
    setIsDroppingAsset(true);
  }, []);

  const handleAssetDragLeave = useCallback((event: DragEvent<HTMLDivElement>) => {
    event.preventDefault();
    setIsDroppingAsset(false);
  }, []);

  const handleAssetDrop = useCallback(
    (event: DragEvent<HTMLDivElement>) => {
      event.preventDefault();
      setIsDroppingAsset(false);
      const files = Array.from(event.dataTransfer?.files ?? []);
      if (files.length) {
        void uploadFiles(files);
      }
    },
    [uploadFiles]
  );

  const handleApplyToSelection = useCallback(
    (asset: AssetReference) => {
      if (!selectedPath) {
        return;
      }
      updateWidget(selectedPath, { src: asset.path });
    },
    [selectedPath, updateWidget]
  );

  const handleCopyAssetPath = useCallback(async (assetId: string, path: string) => {
    try {
      await navigator.clipboard.writeText(path);
      setCopiedAssetId(assetId);
      window.setTimeout(() => {
        setCopiedAssetId((current) => (current === assetId ? null : current));
      }, 1200);
    } catch (error) {
      console.warn("Unable to copy asset path", error);
    }
  }, []);

  return (
    <section className="asset-manager" id="asset-manager">
      <div className="asset-manager__header">
        <p className="section-title" style={{ marginBottom: 0 }}>
          Asset Library
        </p>
        <div className="asset-manager__header-actions">
          <button
            type="button"
            className="button tertiary"
            onClick={() => loadAssetCatalog({ force: true })}
            disabled={assetCatalogBusy}
          >
            {assetCatalogBusy ? "Refreshing…" : "Refresh"}
          </button>
          {filtersActive && (
            <button type="button" className="button tertiary" onClick={resetAssetFilters}>
              Clear filters
            </button>
          )}
        </div>
      </div>
      <div className="asset-manager__filters">
        <input
          className="input-field"
          placeholder="Search assets by name, path, tag, or target"
          value={assetFilters.query}
          onChange={handleQueryChange}
        />
        <select className="select-field" value={assetFilters.kinds[0] ?? "all"} onChange={handleKindChange}>
          {KIND_OPTIONS.map((option) => (
            <option key={option.value} value={option.value}>
              {option.label}
            </option>
          ))}
        </select>
        <span className="field-hint">
          {assetCatalogLoadedAt
            ? `Last synced ${new Date(assetCatalogLoadedAt).toLocaleTimeString()}`
            : "Assets load on demand"}
        </span>
      </div>
      {assetCatalogError && <p className="field-hint error-text">{assetCatalogError}</p>}
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
        <span className="field-hint">Assets live alongside your project and sync into the catalog automatically.</span>
        <input
          ref={assetFileInputRef}
          type="file"
          multiple
          className="asset-upload-input"
          onChange={handleFileInputChange}
        />
      </div>
      <AssetUploadQueue uploads={pendingAssetUploads} onDismiss={dismissPendingUpload} />
      <div className="asset-manager__chips" aria-label="Tag filters">
        {tagStats.length === 0 ? (
          <span className="field-hint">No tags yet.</span>
        ) : (
          tagStats.map(([tag, count]) => (
            <button
              type="button"
              key={tag}
              className={`asset-chip ${assetFilters.tags.includes(tag) ? "asset-chip--active" : ""}`}
              onClick={() => toggleTagFilter(tag)}
            >
              {tag} <span className="asset-chip__count">{count}</span>
            </button>
          ))
        )}
      </div>
      <div className="asset-manager__chips" aria-label="Target filters">
        {targetStats.length === 0 ? (
          <span className="field-hint">No widget references recorded yet.</span>
        ) : (
          targetStats.map(([target, count]) => (
            <button
              type="button"
              key={target}
              className={`asset-chip ${assetFilters.targets.includes(target) ? "asset-chip--active" : ""}`}
              onClick={() => toggleTargetFilter(target)}
            >
              {target} <span className="asset-chip__count">{count}</span>
            </button>
          ))
        )}
      </div>
      <div className="asset-manager__summary">
        <span>
          {visibleAssets.length} / {assetCatalog.length} assets shown
        </span>
        <span>{selectedBytes ? `≈ ${formatBytes(selectedBytes)} selected` : "Size pending"}</span>
      </div>
      <div className="asset-manager__grid">
        {assetCatalogBusy && assetCatalog.length === 0 ? (
          <p className="field-hint">Scanning project assets…</p>
        ) : visibleAssets.length === 0 ? (
          <p className="field-hint">No assets match these filters.</p>
        ) : (
          visibleAssets.map((asset) => (
            <article key={asset.id} className="asset-card">
              <div className="asset-card__preview">
                {asset.thumbnailUrl || asset.previewUrl ? (
                  <img
                    src={asset.thumbnailUrl ?? asset.previewUrl ?? ""}
                    alt={`${asset.label} preview`}
                    loading="lazy"
                  />
                ) : (
                  <span className="asset-card__placeholder">{(asset.extension || asset.kind).toUpperCase()}</span>
                )}
              </div>
              <div className="asset-card__meta">
                <strong>{asset.label}</strong>
                <span className="asset-card__path">{asset.path}</span>
                <span className="asset-card__details">
                  {KIND_LABELS[asset.kind]} • {formatBytes(asset.sizeBytes)} • {asset.usageCount} use{asset.usageCount === 1 ? "" : "s"}
                </span>
                {asset.tags.length ? (
                  <span className="asset-card__tags">{asset.tags.join(", ")}</span>
                ) : (
                  <span className="asset-card__tags">No tags</span>
                )}
              </div>
              <div style={{ display: "flex", gap: 8, flexWrap: "wrap", marginTop: 8 }}>
                <button
                  type="button"
                  className="button tertiary"
                  onClick={() => handleCopyAssetPath(asset.id, asset.path)}
                >
                  {copiedAssetId === asset.id ? "Copied" : "Copy path"}
                </button>
                <button
                  type="button"
                  className="button tertiary"
                  onClick={() => handleApplyToSelection(asset)}
                  disabled={!selectedPath}
                >
                  {selectedPath ? "Apply to widget" : "Select a widget"}
                </button>
              </div>
            </article>
          ))
        )}
      </div>
    </section>
  );
}
