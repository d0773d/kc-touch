import { ReactNode } from "react";
import { act, renderHook } from "@testing-library/react";
import { beforeEach, describe, expect, it, vi } from "vitest";

vi.mock("../utils/api", () => ({
  fetchTemplateProject: vi.fn(),
}));

import { fetchTemplateProject } from "../utils/api";
import { ProjectModel } from "../types/yamui";
import { ProjectProvider, useProject } from "./ProjectContext";

describe("ProjectContext", () => {
  const wrapper = ({ children }: { children: ReactNode }) => <ProjectProvider>{children}</ProjectProvider>;

  beforeEach(() => {
    vi.clearAllMocks();
  });

  it("provides a default project with an initial main screen", () => {
    const { result } = renderHook(() => useProject(), { wrapper });
    expect(result.current.project.screens.main).toBeDefined();
    expect(result.current.project.screens.main.initial).toBe(true);
    expect(result.current.editorTarget).toEqual({ type: "screen", id: "main" });
  });

  it("can add widgets to the active screen", () => {
    const { result } = renderHook(() => useProject(), { wrapper });

    act(() => {
      result.current.addWidget("label");
    });

    const mainWidgets = result.current.project.screens.main.widgets;
    expect(mainWidgets).toHaveLength(1);
    expect(mainWidgets[0]?.type).toBe("label");
    expect(mainWidgets[0]?.id).toMatch(/^widget_/);
  });

  it("updates, reorders, and removes widgets", () => {
    const { result } = renderHook(() => useProject(), { wrapper });

    act(() => {
      result.current.addWidget("label");
      result.current.addWidget("button");
    });

    act(() => {
      result.current.updateWidget([0], { text: "Hello" });
    });
    expect(result.current.project.screens.main.widgets[0]?.text).toBe("Hello");

    act(() => {
      result.current.moveWidget([1], [], 0);
    });
    const reordered = result.current.project.screens.main.widgets;
    expect(reordered[0]?.type).toBe("button");
    expect(reordered[1]?.type).toBe("label");

    act(() => {
      result.current.removeWidget([1]);
    });
    expect(result.current.project.screens.main.widgets).toHaveLength(1);
  });

  it("loads the template project on demand and clears cached exports", async () => {
    const templateProject: ProjectModel = {
      app: {},
      state: {},
      styles: {},
      components: {},
      screens: {
        dashboard: {
          name: "dashboard",
          title: "Sample Dashboard",
          initial: true,
          metadata: {},
          widgets: [
            {
              type: "label",
              id: "template-label",
              text: "Hello",
              props: {},
              events: {},
              bindings: {},
              accessibility: {},
              widgets: [],
            },
          ],
        },
      },
    };

    (fetchTemplateProject as vi.Mock).mockResolvedValue(templateProject);

    const { result } = renderHook(() => useProject(), { wrapper });

    act(() => {
      result.current.setLastExport({ yaml: "foo", issues: [] });
    });

    await act(async () => {
      await result.current.loadTemplateProject();
    });

    expect(fetchTemplateProject).toHaveBeenCalledTimes(1);
    expect(result.current.project.screens.dashboard.title).toBe("Sample Dashboard");
    expect(result.current.editorTarget).toEqual({ type: "screen", id: "dashboard" });
    expect(result.current.lastExport).toBeUndefined();
  });

  it("manages screens via add/remove/duplicate helpers", () => {
    const { result } = renderHook(() => useProject(), { wrapper });

    act(() => {
      result.current.addScreen("secondary");
    });

    expect(result.current.project.screens.secondary).toBeDefined();

    act(() => {
      result.current.duplicateScreen("secondary");
    });

    expect(result.current.project.screens.secondary_copy?.initial).toBe(false);

    act(() => {
      result.current.removeScreen("secondary");
    });

    expect(result.current.project.screens.secondary).toBeUndefined();
  });

  it("falls back to an existing screen when the current target is removed", () => {
    const { result } = renderHook(() => useProject(), { wrapper });

    act(() => {
      result.current.addScreen("secondary");
      result.current.setEditorTarget({ type: "screen", id: "secondary" });
    });

    expect(result.current.editorTarget.id).toBe("secondary");

    act(() => {
      result.current.removeScreen("secondary");
    });

    expect(result.current.editorTarget.id).toBe("main");
  });

  it("saves, renames, and deletes style tokens while updating widget references", () => {
    const { result } = renderHook(() => useProject(), { wrapper });

    act(() => {
      result.current.addWidget("label");
    });

    act(() => {
      result.current.saveStyleToken({
        name: "primary",
        category: "color",
        description: "Primary swatch",
        value: { backgroundColor: "#ffffff", color: "#0f172a" },
        tags: ["core"],
        metadata: {},
      });
      result.current.updateWidget([0], { style: "primary" });
    });

    act(() => {
      const existing = result.current.project.styles.primary;
      if (!existing) {
        throw new Error("Style not found");
      }
      result.current.saveStyleToken({ ...existing, name: "brandPrimary" }, "primary");
    });

    expect(result.current.project.styles.brandPrimary).toBeDefined();
    expect(result.current.project.screens.main.widgets[0]?.style).toBe("brandPrimary");
    expect(result.current.styleEditorSelection).toBe("brandPrimary");

    act(() => {
      result.current.deleteStyleToken("brandPrimary");
    });

    expect(result.current.project.styles.brandPrimary).toBeUndefined();
    expect(result.current.project.screens.main.widgets[0]?.style).toBeUndefined();
    expect(result.current.styleEditorSelection).toBeNull();
  });
});
