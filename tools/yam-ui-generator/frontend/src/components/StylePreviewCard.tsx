import { StylePreview, StyleTokenModel } from "../types/yamui";

interface StylePreviewCardProps {
  label?: string;
  token?: StyleTokenModel;
  preview: StylePreview | null;
  busy?: boolean;
  error?: string | null;
  emptyHint?: string;
  footnote?: string;
  className?: string;
}

const DEFAULT_BACKGROUND = "#f1f5f9";
const DEFAULT_FOREGROUND = "#0f172a";

function getStyleColor(token: StyleTokenModel | undefined, key: "backgroundColor" | "color", fallback: string): string {
  if (!token) {
    return fallback;
  }
  const value = token.value?.[key];
  return typeof value === "string" ? value : fallback;
}

export default function StylePreviewCard({
  label = "Live Preview",
  token,
  preview,
  busy = false,
  error = null,
  emptyHint = "Preview will display once the backend responds.",
  footnote = "Preview widget updates whenever style data changes.",
  className,
}: StylePreviewCardProps): JSX.Element {
  const baseBackground = getStyleColor(token, "backgroundColor", DEFAULT_BACKGROUND);
  const baseForeground = getStyleColor(token, "color", DEFAULT_FOREGROUND);
  const backgroundColor = preview?.backgroundColor ?? baseBackground;
  const textColor = preview?.color ?? baseForeground;
  const categoryLabel = (preview?.category ?? token?.category ?? "style").toUpperCase();
  const widgetTitle = preview?.widget?.text ?? preview?.widget?.id ?? token?.name ?? "Preview";
  const widgetType = preview?.widget?.type?.toUpperCase() ?? (token?.category ? `${token.category.toUpperCase()} STYLE` : "STYLE");
  const description = preview?.description ?? token?.description ?? "This style has no description yet.";
  const blockClassName = ["style-preview-block", className].filter(Boolean).join(" ");
  const cardClassName = [
    "style-preview-card",
    busy ? "is-syncing" : "",
    !preview ? "is-empty" : "",
  ]
    .filter(Boolean)
    .join(" ");
  const statusCopy = error ? error : busy ? "Syncing…" : "In sync";
  const statusClass = [
    "style-preview-status",
    error ? "error" : "",
    busy && !error ? "busy" : "",
  ]
    .filter(Boolean)
    .join(" ");

  return (
    <div className={blockClassName}>
      <div className="style-preview-header">
        <span>{label}</span>
        <span className={statusClass}>{statusCopy}</span>
      </div>
      {error && !busy ? <span className="style-preview-status error">{error}</span> : null}
      {token ? (
        <div className={cardClassName}>
          <div className="style-preview-card__surface" style={{ background: backgroundColor, color: textColor }}>
            <span className="style-preview-card__badge">{categoryLabel}</span>
            <div className="style-preview-card__widget">
              <span className="style-preview-card__widget-type">{widgetType}</span>
              <span className="style-preview-card__widget-text">{widgetTitle}</span>
              <span className="style-preview-card__widget-desc">{description}</span>
            </div>
          </div>
          <div className="style-preview-card__meta">
            <div className="style-preview-card__meta-row">
              <span className="style-preview-card__meta-label">Background</span>
              <div className="style-preview-card__swatch-pair">
                <span className="style-preview-card__swatch" style={{ background: backgroundColor }} aria-label={`Background ${backgroundColor}`} />
                <span className="style-preview-card__meta-value">{backgroundColor}</span>
              </div>
            </div>
            <div className="style-preview-card__meta-row">
              <span className="style-preview-card__meta-label">Text</span>
              <div className="style-preview-card__swatch-pair">
                <span className="style-preview-card__swatch" style={{ background: textColor }} aria-label={`Text ${textColor}`} />
                <span className="style-preview-card__meta-value">{textColor}</span>
              </div>
            </div>
            <span className="style-preview-card__meta-hint">{preview ? footnote : emptyHint}</span>
          </div>
        </div>
      ) : (
        <span className="style-preview-status">Select or create a style to see its live preview.</span>
      )}
    </div>
  );
}
