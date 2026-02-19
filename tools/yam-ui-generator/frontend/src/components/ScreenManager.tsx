import { useEffect, useState } from "react";
import { useProject } from "../context/ProjectContext";

export default function ScreenManager(): JSX.Element {
  const {
    project,
    editorTarget,
    setEditorTarget,
    addScreen,
    removeScreen,
    duplicateScreen,
    setProject,
  } = useProject();
  const [metadataText, setMetadataText] = useState("{}");
  const [metadataError, setMetadataError] = useState<string | null>(null);
  const [titleDraft, setTitleDraft] = useState("");

  const currentScreen = editorTarget.type === "screen" ? project.screens[editorTarget.id] : null;

  useEffect(() => {
    if (!currentScreen) {
      setMetadataText("{}");
      setTitleDraft("");
      setMetadataError(null);
      return;
    }
    setMetadataText(JSON.stringify(currentScreen.metadata ?? {}, null, 2));
    setTitleDraft(currentScreen.title ?? "");
    setMetadataError(null);
  }, [currentScreen]);

  const updateScreen = (name: string, partial: object) => {
    const nextScreen = project.screens[name];
    if (!nextScreen) {
      return;
    }
    setProject({
      ...project,
      screens: {
        ...project.screens,
        [name]: { ...nextScreen, ...partial },
      },
    });
  };

  const handleRename = (screenName: string) => {
    const nextName = window.prompt("Rename screen", screenName);
    if (!nextName || nextName === screenName) {
      return;
    }
    if (project.screens[nextName]) {
      window.alert("A screen with that name already exists.");
      return;
    }
    const nextScreens = Object.fromEntries(
      Object.entries(project.screens).map(([key, value]) => {
        if (key === screenName) {
          return [nextName, { ...value, name: nextName }];
        }
        return [key, value];
      })
    );
    setProject({ ...project, screens: nextScreens });
    if (editorTarget.type === "screen" && editorTarget.id === screenName) {
      setEditorTarget({ type: "screen", id: nextName });
    }
  };

  const handleSetInitial = (screenName: string) => {
    const nextScreens = Object.fromEntries(
      Object.entries(project.screens).map(([key, value]) => [key, { ...value, initial: key === screenName }])
    );
    setProject({ ...project, screens: nextScreens });
  };

  const handleMetadataBlur = () => {
    if (!currentScreen) {
      return;
    }
    try {
      const parsed = metadataText.trim() ? JSON.parse(metadataText) : {};
      updateScreen(currentScreen.name, { metadata: parsed });
      setMetadataError(null);
    } catch {
      setMetadataError("Metadata must be valid JSON");
    }
  };

  const handleTitleChange = (value: string) => {
    setTitleDraft(value);
    if (currentScreen) {
      updateScreen(currentScreen.name, { title: value });
    }
  };

  return (
    <section style={{ marginTop: "16px" }}>
      <p className="section-title">Screens</p>
      <div className="screen-list">
        {Object.values(project.screens).map((screen) => (
          <div
            key={screen.name}
            className={`screen-item ${editorTarget.type === "screen" && editorTarget.id === screen.name ? "active" : ""}`}
            onClick={() => setEditorTarget({ type: "screen", id: screen.name })}
          >
            <div>
              <strong>{screen.title ?? screen.name}</strong>
              {screen.initial && <span style={{ marginLeft: 8, fontSize: "0.75rem", color: "#0ea5e9" }}>Initial</span>}
            </div>
            <div style={{ display: "flex", gap: 6 }}>
              <button
                className="button secondary"
                onClick={(event) => {
                  event.stopPropagation();
                  handleRename(screen.name);
                }}
              >
                Rename
              </button>
              <button
                className="button secondary"
                onClick={(event) => {
                  event.stopPropagation();
                  duplicateScreen(screen.name);
                }}
              >
                Duplicate
              </button>
              {!screen.initial && (
                <button
                  className="button secondary"
                  onClick={(event) => {
                    event.stopPropagation();
                    handleSetInitial(screen.name);
                  }}
                >
                  Make Initial
                </button>
              )}
              {Object.keys(project.screens).length > 1 && (
                <button
                  className="button secondary"
                  onClick={(event) => {
                    event.stopPropagation();
                    removeScreen(screen.name);
                  }}
                >
                  Delete
                </button>
              )}
            </div>
          </div>
        ))}
      </div>
      <button className="button primary" onClick={() => addScreen()}>Add Screen</button>
      {currentScreen && (
        <div className="component-props" style={{ marginTop: 16 }}>
          <div className="component-props__header" style={{ marginBottom: 8 }}>
            <p className="section-title" style={{ marginBottom: 0 }}>Screen Details</p>
            {currentScreen.initial && <span style={{ color: "#0ea5e9", fontSize: "0.85rem" }}>Initial Screen</span>}
          </div>
          <div className="properties-grid">
            <label className="inspector-field">
              <div className="field-label">
                <span>Screen ID</span>
              </div>
              <input className="input-field" value={currentScreen.name} disabled />
            </label>
            <label className="inspector-field">
              <div className="field-label">
                <span>Title</span>
              </div>
              <input
                className="input-field"
                placeholder="Display title"
                value={titleDraft}
                onChange={(event) => handleTitleChange(event.target.value)}
              />
            </label>
            <label className={`inspector-field ${metadataError ? "error" : ""}`}>
              <div className="field-label">
                <span>Metadata JSON</span>
                <span className="field-badge warning">Viewport, analytics, etc.</span>
              </div>
              <textarea
                className="textarea-field"
                rows={4}
                value={metadataText}
                onChange={(event) => setMetadataText(event.target.value)}
                onBlur={handleMetadataBlur}
              />
              {metadataError && <span style={{ color: "#f43f5e", fontSize: "0.8rem" }}>{metadataError}</span>}
            </label>
          </div>
        </div>
      )}
    </section>
  );
}
