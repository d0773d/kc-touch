export type WidgetType =
  | "row"
  | "column"
  | "panel"
  | "spacer"
  | "list"
  | "label"
  | "button"
  | "img"
  | "textarea"
  | "switch"
  | "slider"
  | "component";

export interface WidgetNode {
  type: WidgetType;
  id?: string;
  text?: string;
  src?: string;
  style?: string;
  props?: Record<string, unknown>;
  events?: Record<string, unknown>;
  bindings?: Record<string, unknown>;
  accessibility?: Record<string, unknown>;
  widgets?: WidgetNode[];
}

export interface ScreenModel {
  name: string;
  title?: string;
  initial?: boolean;
  widgets: WidgetNode[];
  metadata?: Record<string, unknown>;
}

export interface ComponentModel {
  description?: string;
  props?: Record<string, unknown>;
  prop_schema?: ComponentPropDefinition[];
  widgets: WidgetNode[];
}

export type StyleCategory = "color" | "surface" | "text" | "spacing" | "shadow";

export interface StyleTokenModel {
  name: string;
  category: StyleCategory;
  description?: string;
  value: Record<string, unknown>;
  tags?: string[];
  metadata?: Record<string, unknown>;
}

export interface StylePreview {
  category: StyleCategory;
  backgroundColor: string;
  color: string;
  description: string;
  widget?: WidgetNode | null;
}

export interface TranslationLocaleModel {
  label?: string;
  description?: string;
  entries: Record<string, string>;
  metadata?: Record<string, unknown>;
}

export type TranslationStore = Record<string, TranslationLocaleModel>;

export interface TranslationImportResult {
  translations: TranslationStore;
  issues: ValidationIssue[];
}

export interface TranslationExportResult {
  filename: string;
  mime_type: string;
  content: string;
  issues: ValidationIssue[];
}

export interface ProjectSettingsResult {
  settings: Record<string, unknown>;
}

export interface ProjectSettingsUpdateResult {
  project: ProjectModel;
  settings: Record<string, unknown>;
  issues: ValidationIssue[];
}

export interface ProjectModel {
  app: Record<string, unknown>;
  state: Record<string, unknown>;
  translations: TranslationStore;
  styles: Record<string, StyleTokenModel>;
  components: Record<string, ComponentModel>;
  screens: Record<string, ScreenModel>;
}

export interface ComponentPropDefinition {
  name: string;
  type: "string" | "number" | "boolean" | "style" | "asset" | "component" | "json";
  required?: boolean;
  default?: unknown;
}

export interface ValidationIssue {
  path: string;
  message: string;
  severity: "error" | "warning";
}

export interface WidgetMetadata {
  type: WidgetType;
  category: "layout" | "ui" | "component";
  icon: string;
  description: string;
  accepts_children: boolean;
  allowed_children: string[];
  componentName?: string;
}

export type WidgetPath = number[];

export interface AssetReference {
  id: string;
  path: string;
  label: string;
  extension: string;
  kind: "image" | "video" | "audio" | "font" | "binary" | "unknown";
  usageCount: number;
  widgetIds: string[];
  targets: string[];
  tags: string[];
  sizeBytes?: number;
  metadata?: Record<string, unknown>;
  previewUrl?: string;
  thumbnailUrl?: string;
  downloadUrl?: string;
}
