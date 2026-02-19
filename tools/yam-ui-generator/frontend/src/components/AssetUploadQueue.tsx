import { PendingAssetUpload } from "../context/ProjectContext";

interface Props {
  uploads: PendingAssetUpload[];
  onDismiss: (id: string) => void;
}

export default function AssetUploadQueue({ uploads, onDismiss }: Props): JSX.Element | null {
  if (!uploads.length) {
    return null;
  }

  return (
    <div className="asset-upload-pending">
      {uploads.map((upload) => (
        <div key={upload.id} className={`asset-upload-pill asset-upload-pill--${upload.status}`}>
          <div className="asset-upload-pill__meta">
            <strong>{upload.fileName}</strong>
            <span>{upload.status === "uploading" ? "Uploading…" : upload.error}</span>
          </div>
          {upload.status === "error" ? (
            <button type="button" className="button tertiary" onClick={() => onDismiss(upload.id)}>
              Dismiss
            </button>
          ) : (
            <span className="asset-upload-pill__spinner" aria-hidden="true" />
          )}
        </div>
      ))}
    </div>
  );
}
