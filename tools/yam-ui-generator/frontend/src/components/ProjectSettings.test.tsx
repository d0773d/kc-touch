import { useEffect } from "react";
import { fireEvent, render, screen, waitFor } from "@testing-library/react";
import { describe, expect, it, vi } from "vitest";
import { ProjectProvider, useProject } from "../context/ProjectContext";
import ProjectSettings from "./ProjectSettings";

vi.mock("../utils/api", () => ({
  updateProjectSettings: vi.fn(async (project, settings) => ({
    project: {
      ...project,
      app: { ...project.app, ...settings },
    },
    settings,
    issues: [],
  })),
}));

function Bootstrap(): JSX.Element {
  const { addScreen } = useProject();
  useEffect(() => {
    addScreen("secondary");
  }, [addScreen]);
  return null;
}

function Snapshot(): JSX.Element {
  const { project } = useProject();
  return (
    <pre data-testid="snapshot">
      {JSON.stringify(
        {
          app: project.app,
          mainInitial: project.screens.main?.initial,
          secondaryInitial: project.screens.secondary?.initial,
          locales: Object.keys(project.translations),
        },
        null,
        2
      )}
    </pre>
  );
}

describe("ProjectSettings", () => {
  it("updates app metadata, initial screen, and locale settings", async () => {
    render(
      <ProjectProvider>
        <Bootstrap />
        <ProjectSettings />
        <Snapshot />
      </ProjectProvider>
    );

    const appName = screen.getByLabelText("App Name");
    fireEvent.change(appName, { target: { value: "Hydro Console" } });

    const supportedLocales = screen.getByLabelText("Supported Locales");
    fireEvent.change(supportedLocales, { target: { value: "en, es" } });

    await waitFor(() => {
      const snapshot = screen.getByTestId("snapshot").textContent ?? "";
      expect(snapshot).toContain("\"supported_locales\": [");
      expect(snapshot).toContain("\"es\"");
      expect(snapshot).toContain("\"Hydro Console\"");
      expect(snapshot).toContain("\"locales\": [");
      expect(snapshot).toContain("\"en\"");
      expect(snapshot).toContain("\"es\"");
    });

    const initialScreen = screen.getByLabelText("Initial Screen");
    fireEvent.change(initialScreen, { target: { value: "secondary" } });

    const defaultLocale = screen.getByLabelText("Default Locale");
    fireEvent.change(defaultLocale, { target: { value: "es" } });

    await waitFor(() => {
      const snapshot = screen.getByTestId("snapshot").textContent ?? "";
      expect(snapshot).toContain("\"initial_screen\": \"secondary\"");
      expect(snapshot).toContain("\"locale\": \"es\"");
      expect(snapshot).toContain("\"mainInitial\": false");
      expect(snapshot).toContain("\"secondaryInitial\": true");
    });
  });

  it("applies settings through the backend contract action", async () => {
    render(
      <ProjectProvider>
        <Bootstrap />
        <ProjectSettings />
        <Snapshot />
      </ProjectProvider>
    );

    fireEvent.change(screen.getByLabelText("App Name"), { target: { value: "Hydro Console" } });
    fireEvent.click(screen.getByRole("button", { name: "Apply Settings" }));

    await waitFor(() => {
      expect(screen.getByText("Settings applied")).toBeInTheDocument();
      const snapshot = screen.getByTestId("snapshot").textContent ?? "";
      expect(snapshot).toContain("\"name\": \"Hydro Console\"");
    });
  });
});
