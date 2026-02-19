import { useCallback } from "react";
import { useProject } from "../context/ProjectContext";
import { uploadAsset } from "../utils/api";
import { emitTelemetry } from "../utils/telemetry";

const createClientId = (): string => {
  if (typeof crypto !== "undefined" && "randomUUID" in crypto) {
    return crypto.randomUUID();
  }
  return Math.random().toString(36).slice(2, 10);
};

export function useAssetUploads() {
  const {
    setAssetCatalog,
    setAssetTagDrafts,
    setPendingAssetUploads,
    loadAssetCatalog,
  } = useProject();

  const uploadSingleAsset = useCallback(
    async (file: File) => {
      const pendingId = createClientId();
      setPendingAssetUploads((prev) => [...prev, { id: pendingId, fileName: file.name, status: "uploading" }]);
      try {
        emitTelemetry("assets", "asset_upload_start", { fileName: file.name, size: file.size });
        const uploaded = await uploadAsset(file);
        setAssetCatalog((prev) => [uploaded, ...prev.filter((item) => item.id !== uploaded.id)]);
        setAssetTagDrafts((prev) => ({ ...prev, [uploaded.id]: uploaded.tags.join(", ") }));
        setPendingAssetUploads((prev) => prev.filter((item) => item.id !== pendingId));
        emitTelemetry("assets", "asset_upload_success", { assetId: uploaded.id, kind: uploaded.kind });
      } catch (error) {
        const message = error instanceof Error ? error.message : "Failed to upload asset";
        setPendingAssetUploads((prev) =>
          prev.map((item) => (item.id === pendingId ? { ...item, status: "error", error: message } : item))
        );
        emitTelemetry("assets", "asset_upload_failure", { fileName: file.name, message });
      }
    },
    [setAssetCatalog, setAssetTagDrafts, setPendingAssetUploads]
  );

  const uploadFiles = useCallback(
    async (files: File[]) => {
      if (!files.length) {
        return;
      }
      emitTelemetry("assets", "asset_upload_batch_start", { count: files.length });
      await Promise.all(files.map((file) => uploadSingleAsset(file)));
      await loadAssetCatalog({ force: true });
      emitTelemetry("assets", "asset_upload_batch_complete", { count: files.length });
    },
    [loadAssetCatalog, uploadSingleAsset]
  );

  const dismissPendingUpload = useCallback(
    (id: string) => {
      setPendingAssetUploads((prev) => prev.filter((item) => item.id !== id));
    },
    [setPendingAssetUploads]
  );

  return {
    uploadFiles,
    dismissPendingUpload,
  };
}
