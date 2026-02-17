import { AssetReference, ProjectModel, StylePreview, StyleTokenModel, ValidationIssue, WidgetMetadata } from "../types/yamui";

const API_BASE_URL = import.meta.env.VITE_API_BASE_URL ?? "http://localhost:8000";

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
  previewUrl: asset.preview_url,
  thumbnailUrl: asset.thumbnail_url,
  downloadUrl: asset.download_url,
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
