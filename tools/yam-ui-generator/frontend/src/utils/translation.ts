import type { ProjectModel } from "../types/yamui";

const TRANSLATION_EXPRESSION_PATTERN = /^(?:\{\{\s*)?t\(\s*["']([^"'()]+)["']\s*(?:,[^)]*)?\)\s*(?:\}\})?$/i;

const DEFAULT_TRANSLATION_KEY = "text.key";

export function extractTranslationKey(value?: string | null): string | null {
  if (!value || typeof value !== "string") {
    return null;
  }
  const trimmed = value.trim();
  const match = TRANSLATION_EXPRESSION_PATTERN.exec(trimmed);
  if (!match) {
    return null;
  }
  return match[1]?.trim() || null;
}

export function buildTranslationExpression(key: string): string {
  return `{{ t('${key}') }}`;
}

export function suggestTranslationKey(source: string, existing?: Iterable<string>): string {
  const pool = existing ? Array.from(existing) : [];
  const set = new Set(pool);
  const normalized = source
    .trim()
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, ".")
    .replace(/^(?:\.)+|(?:\.)+$/g, "")
    .replace(/\.\.+/g, ".");
  const base = normalized || DEFAULT_TRANSLATION_KEY;
  if (!set.has(base)) {
    return base;
  }
  let suffix = 2;
  while (set.has(`${base}_${suffix}`)) {
    suffix += 1;
  }
  return `${base}_${suffix}`;
}

export function getPrimaryLocale(project: ProjectModel): string | null {
  const locales = Object.keys(project.translations ?? {});
  const configured = typeof project.app?.locale === "string" ? project.app.locale : null;
  if (configured && locales.includes(configured)) {
    return configured;
  }
  return locales[0] ?? null;
}
