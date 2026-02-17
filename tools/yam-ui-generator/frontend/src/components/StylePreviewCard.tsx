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
  knobEditingEnabled?: boolean;
  knobEditingDisabledReason?: string;
  onKnobFieldChange?: (fieldKey: string, value: string) => void;
}

export interface StylePreviewKnobField {
  key: string;
  label: string;
  value: string;
  placeholder?: string;
}

export interface StylePreviewKnob {
  id: string;
  label: string;
  summary?: string;
  fields: StylePreviewKnobField[];
}

const DEFAULT_BACKGROUND = "#f1f5f9";
const DEFAULT_FOREGROUND = "#0f172a";

type StyleValueMap = Record<string, unknown>;

const asDisplayString = (value: unknown): string | undefined => {
  if (value === null || value === undefined) {
    return undefined;
  }
  if (typeof value === "string" || typeof value === "number") {
    const text = String(value).trim();
    return text.length ? text : undefined;
  }
  return undefined;
};

const pickKey = (valueMap: StyleValueMap, candidates: string[]): string => {
  for (const candidate of candidates) {
    if (valueMap[candidate] !== undefined) {
      return candidate;
    }
  }
  return candidates[0];
};

const buildKnobField = (
  label: string,
  valueMap: StyleValueMap,
  candidates: string[],
  placeholder?: string
): StylePreviewKnobField => {
  const key = pickKey(valueMap, candidates);
  return {
    key,
    label,
    value: asDisplayString(valueMap[key]) ?? "",
    placeholder,
  };
};

const describeTypography = (value: StyleValueMap): string | null => {
  const family = asDisplayString(value.fontFamily ?? value.typeface);
  const size = asDisplayString(value.fontSize);
  const weight = asDisplayString(value.fontWeight);
  const lineHeight = asDisplayString(value.lineHeight);
  const letterSpacing = asDisplayString(value.letterSpacing);
  const textTransform = asDisplayString(value.textTransform);
  if (!family && !size && !weight && !lineHeight && !letterSpacing && !textTransform) {
    return null;
  }
  const metrics = [size, lineHeight ? `lh ${lineHeight}` : undefined, letterSpacing ? `trk ${letterSpacing}` : undefined]
    .filter(Boolean)
    .join(" • ");
  const traits = [weight ? `w${weight}` : undefined, textTransform?.toLowerCase()].filter(Boolean).join(" • ");
  return [family, metrics, traits].filter(Boolean).join(" | ");
};

const describeBorder = (value: StyleValueMap): string | null => {
  const width = asDisplayString(value.borderWidth ?? value.strokeWidth);
  const style = asDisplayString(value.borderStyle);
  const color = asDisplayString(value.borderColor ?? value.strokeColor);
  const radius = asDisplayString(value.borderRadius ?? value.cornerRadius);
  if (!width && !style && !color && !radius) {
    return null;
  }
  const base = [width, style, color].filter(Boolean).join(" ");
  return [base, radius ? `radius ${radius}` : undefined].filter(Boolean).join(" • ");
};

const describeSpacing = (value: StyleValueMap): string | null => {
  const padding =
    asDisplayString(value.padding) ??
    asDisplayString(value.paddingAll) ??
    asDisplayString(value.paddingVertical);
  const gap = asDisplayString(value.gap ?? value.spacing);
  const margin = asDisplayString(value.margin);
  if (!padding && !gap && !margin) {
    return null;
  }
  const tokens = [padding ? `Padding ${padding}` : undefined, gap ? `Gap ${gap}` : undefined, margin ? `Margin ${margin}` : undefined]
    .filter(Boolean)
    .join(" • ");
  return tokens || null;
};

const describeShadow = (value: StyleValueMap): string | null => {
  const boxShadow = asDisplayString(value.boxShadow ?? value.shadow);
  const color = asDisplayString(value.shadowColor);
  const blur = asDisplayString(value.shadowBlur ?? value.shadowRadius);
  const spread = asDisplayString(value.shadowSpread);
  const offsetX = asDisplayString(value.shadowOffsetX ?? value.shadowX);
  const offsetY = asDisplayString(value.shadowOffsetY ?? value.shadowY);
  if (!boxShadow && !color && !blur && !spread && !offsetX && !offsetY) {
    return null;
  }
  if (boxShadow) {
    return boxShadow;
  }
  const offset = `${offsetX ?? 0}, ${offsetY ?? 0}`;
  return [offset, blur ? `blur ${blur}` : undefined, spread ? `spread ${spread}` : undefined, color]
    .filter(Boolean)
    .join(" • ");
};

export function buildStylePreviewKnobs(token?: StyleTokenModel): StylePreviewKnob[] {
  if (!token) {
    return [];
  }
  const value = (token.value ?? {}) as StyleValueMap;
  const knobs: StylePreviewKnob[] = [];

  const typographyFields = [
    buildKnobField("Family", value, ["fontFamily", "typeface"], "Space Grotesk"),
    buildKnobField("Size", value, ["fontSize"], "16px"),
    buildKnobField("Weight", value, ["fontWeight"], "600"),
    buildKnobField("Line Height", value, ["lineHeight"], "1.4"),
    buildKnobField("Letter Spacing", value, ["letterSpacing"], "0.08em"),
    buildKnobField("Transform", value, ["textTransform"], "uppercase"),
  ];
  const hasTypographyValue = typographyFields.some((field) => field.value);
  if (token.category === "text" || hasTypographyValue) {
    knobs.push({
      id: "typography",
      label: "Typography",
      summary: describeTypography(value) ?? undefined,
      fields: typographyFields,
    });
  }

  const borderFields = [
    buildKnobField("Width", value, ["borderWidth", "strokeWidth"], "1px"),
    buildKnobField("Style", value, ["borderStyle"], "solid"),
    buildKnobField("Color", value, ["borderColor", "strokeColor"], "#e2e8f0"),
    buildKnobField("Radius", value, ["borderRadius", "cornerRadius"], "8px"),
  ];
  const hasBorderValue = borderFields.some((field) => field.value);
  if (token.category === "surface" || hasBorderValue) {
    knobs.push({
      id: "border",
      label: "Border",
      summary: describeBorder(value) ?? undefined,
      fields: borderFields,
    });
  }

  const spacingFields = [
    buildKnobField("Padding", value, ["padding", "paddingAll", "paddingVertical"], "16px"),
    buildKnobField("Gap", value, ["gap", "spacing"], "8px"),
    buildKnobField("Margin", value, ["margin"], "0"),
  ];
  const hasSpacingValue = spacingFields.some((field) => field.value);
  if (token.category === "spacing" || hasSpacingValue) {
    knobs.push({
      id: "spacing",
      label: "Spacing",
      summary: describeSpacing(value) ?? undefined,
      fields: spacingFields,
    });
  }

  const shadowFields = [
    buildKnobField("Color", value, ["shadowColor"], "rgba(15, 23, 42, 0.2)"),
    buildKnobField("Offset X", value, ["shadowOffsetX", "shadowX"], "0px"),
    buildKnobField("Offset Y", value, ["shadowOffsetY", "shadowY"], "6px"),
    buildKnobField("Blur", value, ["shadowBlur", "shadowRadius"], "12px"),
    buildKnobField("Spread", value, ["shadowSpread"], "0px"),
  ];
  const hasShadowValue = shadowFields.some((field) => field.value);
  if (token.category === "shadow" || hasShadowValue) {
    knobs.push({
      id: "shadow",
      label: "Shadow",
      summary: describeShadow(value) ?? undefined,
      fields: shadowFields,
    });
  }

  return knobs;
}

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
  knobEditingEnabled = false,
  knobEditingDisabledReason,
  onKnobFieldChange,
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
  const previewKnobs = buildStylePreviewKnobs(token);
  const knobEditingActive = Boolean(knobEditingEnabled && onKnobFieldChange);
  const knobHint = !knobEditingActive && knobEditingDisabledReason ? knobEditingDisabledReason : null;

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
            {previewKnobs.length > 0 && (
              <div className="style-preview-card__knobs">
                {knobHint && <span className="style-preview-card__knob-hint warning-text">{knobHint}</span>}
                {previewKnobs.map((knob) => (
                  <div key={knob.id} className={`style-preview-card__knob ${knobEditingActive ? "is-editable" : ""}`}>
                    <div className="style-preview-card__knob-head">
                      <span className="style-preview-card__knob-label">{knob.label}</span>
                      {!knobEditingActive && (
                        <span className="style-preview-card__knob-summary">
                          {knob.summary ?? "No values"}
                        </span>
                      )}
                    </div>
                    {knobEditingActive && onKnobFieldChange ? (
                      <div className="style-preview-card__knob-fields">
                        {knob.fields.map((field) => (
                          <label key={`${knob.id}-${field.key}`} className="style-preview-card__knob-field">
                            <span>{field.label}</span>
                            <div className="style-preview-card__knob-input">
                              <input
                                className="input-field"
                                value={field.value}
                                placeholder={field.placeholder}
                                onChange={(event) => onKnobFieldChange(field.key, event.target.value)}
                              />
                              {field.value && (
                                <button
                                  type="button"
                                  className="button tertiary style-preview-card__knob-reset"
                                  onClick={() => onKnobFieldChange(field.key, "")}
                                >
                                  Reset
                                </button>
                              )}
                            </div>
                          </label>
                        ))}
                      </div>
                    ) : (
                      <span className="style-preview-card__knob-value">{knob.summary ?? "No values set"}</span>
                    )}
                  </div>
                ))}
              </div>
            )}
            <span className="style-preview-card__meta-hint">{preview ? footnote : emptyHint}</span>
          </div>
        </div>
      ) : (
        <span className="style-preview-status">Select or create a style to see its live preview.</span>
      )}
    </div>
  );
}
