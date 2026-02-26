import { ChangeEvent, useMemo, useState } from "react";
import { useProject } from "../context/ProjectContext";
import { updateProjectSettings } from "../utils/api";

function asString(value: unknown): string {
  if (typeof value === "string") {
    return value;
  }
  if (typeof value === "number" || typeof value === "boolean") {
    return String(value);
  }
  return "";
}

function parseLocaleList(raw: string): string[] {
  return raw
    .split(",")
    .map((token) => token.trim())
    .filter(Boolean);
}

export default function ProjectSettings(): JSX.Element {
  const { project, setProject } = useProject();
  const [syncBusy, setSyncBusy] = useState(false);
  const [syncError, setSyncError] = useState<string | null>(null);
  const [syncSummary, setSyncSummary] = useState<string | null>(null);

  const app = project.app ?? {};
  const screenNames = useMemo(() => Object.keys(project.screens), [project.screens]);
  const localeNames = useMemo(() => Object.keys(project.translations), [project.translations]);

  const currentInitialScreen = asString(app.initial_screen) || screenNames.find((name) => project.screens[name]?.initial) || "";
  const currentDefaultLocale = asString(app.locale) || localeNames[0] || "";
  const currentSupportedLocales = Array.isArray(app.supported_locales)
    ? app.supported_locales.map((entry) => String(entry))
    : localeNames;

  const updateAppField = (key: string, value: unknown) => {
    setProject({
      ...project,
      app: {
        ...app,
        [key]: value,
      },
    });
  };

  const handleTextInput = (key: string) => (event: ChangeEvent<HTMLInputElement>) => {
    setSyncError(null);
    setSyncSummary(null);
    updateAppField(key, event.target.value);
  };

  const handleInitialScreenChange = (event: ChangeEvent<HTMLSelectElement>) => {
    setSyncError(null);
    setSyncSummary(null);
    const selected = event.target.value;
    const nextScreens = Object.fromEntries(
      Object.entries(project.screens).map(([name, screen]) => [
        name,
        {
          ...screen,
          initial: name === selected,
        },
      ])
    );

    setProject({
      ...project,
      app: {
        ...app,
        initial_screen: selected,
      },
      screens: nextScreens,
    });
  };

  const handleDefaultLocaleChange = (event: ChangeEvent<HTMLSelectElement>) => {
    setSyncError(null);
    setSyncSummary(null);
    const selected = event.target.value.trim();
    if (!selected) {
      return;
    }
    const nextTranslations = { ...project.translations };
    if (!nextTranslations[selected]) {
      nextTranslations[selected] = {
        label: selected,
        entries: {},
        metadata: {},
      };
    }
    setProject({
      ...project,
      app: {
        ...app,
        locale: selected,
      },
      translations: nextTranslations,
    });
  };

  const handleSupportedLocalesChange = (event: ChangeEvent<HTMLInputElement>) => {
    setSyncError(null);
    setSyncSummary(null);
    const locales = parseLocaleList(event.target.value);
    const nextTranslations = { ...project.translations };
    locales.forEach((locale) => {
      if (!nextTranslations[locale]) {
        nextTranslations[locale] = {
          label: locale,
          entries: {},
          metadata: {},
        };
      }
    });
    setProject({
      ...project,
      app: {
        ...app,
        supported_locales: locales,
      },
      translations: nextTranslations,
    });
  };

  const handleApplySettings = async () => {
    setSyncBusy(true);
    setSyncError(null);
    setSyncSummary(null);
    try {
      const response = await updateProjectSettings(project, app);
      setProject(response.project);
      const errorCount = response.issues.filter((issue) => issue.severity === "error").length;
      const warningCount = response.issues.length - errorCount;
      if (response.issues.length === 0) {
        setSyncSummary("Settings applied");
      } else {
        setSyncSummary(`Applied with ${errorCount} error(s) and ${warningCount} warning(s)`);
      }
    } catch (error) {
      setSyncError((error as Error).message);
    } finally {
      setSyncBusy(false);
    }
  };

  return (
    <section className="panel">
      <h3 style={{ marginTop: 0 }}>Project Settings</h3>
      <label className="inspector-field">
        <div className="field-label">
          <span>App Name</span>
        </div>
        <input className="text-field" value={asString(app.name)} onChange={handleTextInput("name")} placeholder="YamUI App" />
      </label>
      <label className="inspector-field">
        <div className="field-label">
          <span>Version</span>
        </div>
        <input className="text-field" value={asString(app.version)} onChange={handleTextInput("version")} placeholder="0.1.0" />
      </label>
      <label className="inspector-field">
        <div className="field-label">
          <span>Author</span>
        </div>
        <input className="text-field" value={asString(app.author)} onChange={handleTextInput("author")} placeholder="Team or owner" />
      </label>
      <label className="inspector-field">
        <div className="field-label">
          <span>Initial Screen</span>
        </div>
        <select className="text-field" value={currentInitialScreen} onChange={handleInitialScreenChange}>
          {screenNames.map((name) => (
            <option key={name} value={name}>
              {name}
            </option>
          ))}
        </select>
      </label>
      <label className="inspector-field">
        <div className="field-label">
          <span>Default Locale</span>
        </div>
        <select className="text-field" value={currentDefaultLocale} onChange={handleDefaultLocaleChange}>
          {localeNames.map((locale) => (
            <option key={locale} value={locale}>
              {locale}
            </option>
          ))}
        </select>
      </label>
      <label className="inspector-field">
        <div className="field-label">
          <span>Supported Locales</span>
        </div>
        <input
          className="text-field"
          value={currentSupportedLocales.join(", ")}
          onChange={handleSupportedLocalesChange}
          placeholder="en, es, fr"
        />
      </label>
      <div style={{ display: "flex", gap: 8 }}>
        <button className="button secondary" onClick={handleApplySettings} disabled={syncBusy}>
          {syncBusy ? "Applying..." : "Apply Settings"}
        </button>
      </div>
      {syncSummary && <p className="field-hint">{syncSummary}</p>}
      {syncError && <p className="field-hint warning-text">{syncError}</p>}
    </section>
  );
}
