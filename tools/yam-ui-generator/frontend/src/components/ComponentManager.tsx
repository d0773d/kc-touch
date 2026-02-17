import { FormEvent, useMemo, useState } from "react";
import { useProject } from "../context/ProjectContext";
import { ComponentModel, ComponentPropDefinition } from "../types/yamui";

const PROP_TYPES: ComponentPropDefinition["type"][] = [
  "string",
  "number",
  "boolean",
  "style",
  "asset",
  "component",
  "json",
];

export default function ComponentManager(): JSX.Element {
  const { project, editorTarget, setEditorTarget, setProject } = useProject();
  const [name, setName] = useState("");
  const [description, setDescription] = useState("");

  const selectedComponent = useMemo<ComponentModel | undefined>(() => {
    if (editorTarget.type !== "component") {
      return undefined;
    }
    return project.components[editorTarget.id];
  }, [editorTarget, project.components]);

  const handleCreate = (event: FormEvent) => {
    event.preventDefault();
    if (!name.trim()) {
      return;
    }
    if (project.components[name]) {
      window.alert("A component with that name already exists.");
      return;
    }
    setProject({
      ...project,
      components: {
        ...project.components,
        [name]: {
          description,
          props: {},
          prop_schema: [],
          widgets: [],
        },
      },
    });
    setEditorTarget({ type: "component", id: name });
    setName("");
    setDescription("");
  };

  const handleDelete = (componentName: string) => {
    const { [componentName]: _removed, ...rest } = project.components;
    setProject({ ...project, components: rest });
    if (editorTarget.type === "component" && editorTarget.id === componentName) {
      const firstScreen = Object.keys(project.screens)[0];
      if (firstScreen) {
        setEditorTarget({ type: "screen", id: firstScreen });
      }
    }
  };

  const commitComponent = (next: ComponentModel) => {
    if (!selectedComponent || editorTarget.type !== "component") {
      return;
    }
    setProject({
      ...project,
      components: {
        ...project.components,
        [editorTarget.id]: next,
      },
    });
  };

  const updatePropSchema = (index: number, partial: Partial<ComponentPropDefinition>) => {
    if (!selectedComponent) {
      return;
    }
    const nextSchema = [...(selectedComponent.prop_schema ?? [])];
    nextSchema[index] = { ...nextSchema[index], ...partial } as ComponentPropDefinition;
    commitComponent({ ...selectedComponent, prop_schema: nextSchema });
  };

  const addProp = () => {
    if (!selectedComponent) {
      return;
    }
    const nextSchema = [...(selectedComponent.prop_schema ?? []), { name: "prop", type: "string", required: false }];
    commitComponent({ ...selectedComponent, prop_schema: nextSchema });
  };

  const removeProp = (index: number) => {
    if (!selectedComponent) {
      return;
    }
    const nextSchema = (selectedComponent.prop_schema ?? []).filter((_, idx) => idx !== index);
    commitComponent({ ...selectedComponent, prop_schema: nextSchema });
  };

  return (
    <section style={{ marginTop: "16px" }}>
      <p className="section-title">Components</p>
      <div style={{ maxHeight: "20vh", overflowY: "auto" }}>
        {Object.entries(project.components).map(([key, component]) => (
          <div
            key={key}
            className={`component-card ${editorTarget.type === "component" && editorTarget.id === key ? "active" : ""}`}
            onClick={() => setEditorTarget({ type: "component", id: key })}
          >
            <strong>{key}</strong>
            {component.description && <p style={{ margin: "4px 0", color: "#475569" }}>{component.description}</p>}
            <button
              className="button secondary"
              onClick={(event) => {
                event.stopPropagation();
                handleDelete(key);
              }}
            >
              Delete
            </button>
          </div>
        ))}
        {!Object.keys(project.components).length && (
          <p style={{ color: "#94a3b8" }}>No components yet.</p>
        )}
      </div>
      <form onSubmit={handleCreate} style={{ marginTop: 12, display: "flex", flexDirection: "column", gap: 8 }}>
        <input
          className="input-field"
          placeholder="Component name"
          value={name}
          onChange={(event) => setName(event.target.value)}
        />
        <input
          className="input-field"
          placeholder="Description (optional)"
          value={description}
          onChange={(event) => setDescription(event.target.value)}
        />
        <button className="button primary" type="submit">
          Add Component
        </button>
      </form>

      {selectedComponent && (
        <div className="component-props">
          <div className="component-props__header">
            <p className="section-title" style={{ marginBottom: 0 }}>
              Prop Schema for {editorTarget.id}
            </p>
            <button type="button" className="button secondary" onClick={addProp}>
              Add Prop
            </button>
          </div>
          {(selectedComponent.prop_schema ?? []).length === 0 ? (
            <p style={{ color: "#94a3b8" }}>No props defined yet.</p>
          ) : (
            <div className="prop-schema-grid">
              {(selectedComponent.prop_schema ?? []).map((prop, index) => (
                <div className="prop-row" key={`${prop.name}-${index}`}>
                  <div className="prop-field">
                    <label>Name</label>
                    <input
                      className="input-field"
                      value={prop.name}
                      onChange={(event) => updatePropSchema(index, { name: event.target.value })}
                      placeholder="title"
                    />
                  </div>
                  <div className="prop-field">
                    <label>Type</label>
                    <select
                      className="select-field"
                      value={prop.type}
                      onChange={(event) => updatePropSchema(index, { type: event.target.value as ComponentPropDefinition["type"] })}
                    >
                      {PROP_TYPES.map((type) => (
                        <option value={type} key={type}>
                          {type}
                        </option>
                      ))}
                    </select>
                  </div>
                  <div className="prop-field">
                    <label>Default</label>
                    <input
                      className="input-field"
                      value={prop.default ?? ""}
                      onChange={(event) => updatePropSchema(index, { default: event.target.value })}
                      placeholder="Optional default"
                    />
                  </div>
                  <div className="prop-field prop-field--inline">
                    <label>Required</label>
                    <input
                      type="checkbox"
                      checked={Boolean(prop.required)}
                      onChange={(event) => updatePropSchema(index, { required: event.target.checked })}
                    />
                  </div>
                  <button type="button" className="button tertiary" onClick={() => removeProp(index)}>
                    Remove
                  </button>
                </div>
              ))}
            </div>
          )}
        </div>
      )}
    </section>
  );
}
