import {
  AssetReference,
  ProjectModel,
  ProjectSettingsResult,
  ProjectSettingsUpdateResult,
  StylePreview,
  StyleTokenModel,
  TranslationExportResult,
  TranslationImportResult,
  ValidationIssue,
  WidgetMetadata,
} from "../types/yamui";

const ABSOLUTE_URL_PATTERN = /^https?:\/\//i;

const getWindowOrigin = (): string | undefined => {
  if (typeof window === "undefined" || !window.location?.origin) {
    return undefined;
  }
  return window.location.origin;
};

const LOCAL_DEV_BACKEND = "http://localhost:8000";

const determineFallbackOrigin = (): string => {
  const origin = getWindowOrigin();
  if (!origin) {
    return LOCAL_DEV_BACKEND;
  }
  try {
    const parsed = new URL(origin);
    const isLocalDevHost = ["localhost", "127.0.0.1"].includes(parsed.hostname) && parsed.port === "5173"; // Vite dev server
    return isLocalDevHost ? LOCAL_DEV_BACKEND : origin;
  } catch {
    return LOCAL_DEV_BACKEND;
  }
};

const determineApiBase = (): { root: string; origin: string; path: string } => {
  const raw = import.meta.env.VITE_API_BASE_URL as string | undefined;
  const fallbackOrigin = determineFallbackOrigin();
  const target = raw && raw.trim().length ? raw.trim() : fallbackOrigin;
  try {
    const baseUrl = ABSOLUTE_URL_PATTERN.test(target)
      ? new URL(target)
      : new URL(target, fallbackOrigin);
    const root = baseUrl.href.replace(/\/$/, "");
    const path = baseUrl.pathname === "/" ? "" : baseUrl.pathname.replace(/\/$/, "");
    return { root, origin: baseUrl.origin, path };
  } catch {
    return { root: fallbackOrigin, origin: fallbackOrigin, path: "" };
  }
};

const { root: API_BASE_URL, origin: API_BASE_ORIGIN, path: API_BASE_PATH } = determineApiBase();

const resolveAssetUrl = (url?: string | null): string | undefined => {
  if (!url) {
    return undefined;
  }
  try {
    const basePath = API_BASE_PATH && API_BASE_PATH !== "/" ? API_BASE_PATH : "";
    if (url.startsWith("/")) {
      const combined = `${basePath}${url}` || url;
      return new URL(combined, `${API_BASE_ORIGIN}/`).toString();
    }
    return new URL(url, `${API_BASE_ORIGIN}/`).toString();
  } catch {
    return url;
  }
};

async function request<T>(path: string, init?: RequestInit): Promise<T> {
  const response = await fetch(`${API_BASE_URL}${path}`, {
    headers: {
      "Content-Type": "application/json",
    },
    ...init,
  });
  if (!response.ok) {
    const detail = await response.text();
    throw new Error(detail || `Request failed with status ${response.status}`);
  }
  return (await response.json()) as T;
}

export async function fetchPalette(): Promise<WidgetMetadata[]> {
  return request<WidgetMetadata[]>("/widgets/palette");
}

export async function importProject(yamlText: string): Promise<{ project: ProjectModel; issues: ValidationIssue[] }> {
  return request("/projects/import", {
    method: "POST",
    body: JSON.stringify({ yaml: yamlText }),
  });
}

export async function exportProject(project: ProjectModel): Promise<{ yaml: string; issues: ValidationIssue[] }> {
  return request("/projects/export", {
    method: "POST",
    body: JSON.stringify({ project }),
  });
}

export async function fetchTemplateProject(): Promise<ProjectModel> {
  return request<ProjectModel>("/projects/template");
}

export async function fetchProjectSettings(): Promise<ProjectSettingsResult> {
  return request<ProjectSettingsResult>("/project/settings");
}

export async function updateProjectSettings(
  project: ProjectModel,
  settings: Record<string, unknown>
): Promise<ProjectSettingsUpdateResult> {
  return request<ProjectSettingsUpdateResult>("/project/settings", {
    method: "PUT",
    body: JSON.stringify({ project, settings }),
  });
}

export async function validateProject(payload: { project?: ProjectModel; yaml?: string }): Promise<{
  valid: boolean;
  issues: ValidationIssue[];
}> {
  return request("/projects/validate", {
    method: "POST",
    body: JSON.stringify(payload),
  });
}

export async function previewStyle(token: StyleTokenModel): Promise<StylePreview> {
  const response = await request<{ preview: StylePreview }>("/styles/preview", {
    method: "POST",
    body: JSON.stringify({ token }),
  });
  return response.preview;
}

export async function lintStyles(tokens: Record<string, StyleTokenModel>): Promise<ValidationIssue[]> {
  const response = await request<{ issues: ValidationIssue[] }>("/styles/lint", {
    method: "POST",
    body: JSON.stringify({ tokens }),
  });
  return response.issues;
}

type AssetReferenceWire = {
  id: string;
  path: string;
  label: string;
  extension: string;
  kind: AssetReference["kind"];
  usage_count: number;
  widget_ids: string[];
  targets: string[];
  tags: string[];
  size_bytes?: number;
  metadata?: Record<string, unknown>;
  preview_url?: string;
  thumbnail_url?: string;
  download_url?: string;
};

const mapAssetReference = (asset: AssetReferenceWire): AssetReference => ({
  id: asset.id,
  path: asset.path,
  label: asset.label,
  extension: asset.extension,
  kind: asset.kind,
  usageCount: asset.usage_count,
  widgetIds: asset.widget_ids,
  targets: asset.targets,
  tags: asset.tags ?? [],
  sizeBytes: asset.size_bytes,
  metadata: asset.metadata,
  previewUrl: resolveAssetUrl(asset.preview_url),
  thumbnailUrl: resolveAssetUrl(asset.thumbnail_url),
  downloadUrl: resolveAssetUrl(asset.download_url),
});

export async function fetchAssetCatalog(project: ProjectModel): Promise<AssetReference[]> {
  const response = await request<{ assets: AssetReferenceWire[] }>("/assets/catalog", {
    method: "POST",
    body: JSON.stringify({ project }),
  });
  return response.assets.map(mapAssetReference);
}

export async function uploadAsset(file: File, options?: { path?: string; tags?: string[] }): Promise<AssetReference> {
  const formData = new FormData();
  formData.append("file", file);
  if (options?.path) {
    formData.append("path", options.path);
  }
  if (options?.tags?.length) {
    formData.append("tags", JSON.stringify(options.tags));
  }
  const response = await fetch(`${API_BASE_URL}/assets/upload`, {
    method: "POST",
    body: formData,
  });
  if (!response.ok) {
    const detail = await response.text();
    throw new Error(detail || `Upload failed with status ${response.status}`);
  }
  const payload = (await response.json()) as { asset: AssetReferenceWire };
  return mapAssetReference(payload.asset);
}

export async function updateAssetTags(path: string, tags: string[], project: ProjectModel): Promise<AssetReference> {
  const response = await request<{ asset: AssetReferenceWire }>("/assets/catalog/tags", {
    method: "PATCH",
    body: JSON.stringify({ path, tags, project }),
  });
  return mapAssetReference(response.asset);
}

export async function exportTranslations(
  project: ProjectModel,
  format: "json" | "csv"
): Promise<TranslationExportResult> {
  return request("/translations/export", {
    method: "POST",
    body: JSON.stringify({ project, format }),
  });
}

export async function importTranslations(
  project: ProjectModel,
  format: "json" | "csv",
  content: string
): Promise<TranslationImportResult> {
  return request("/translations/import", {
    method: "POST",
    body: JSON.stringify({ project, format, content }),
  });
}
