import { ReactNode } from "react";
import { act, renderHook, waitFor } from "@testing-library/react";
import { beforeEach, describe, expect, it, vi } from "vitest";

vi.mock("../utils/api", () => ({
  fetchTemplateProject: vi.fn(),
}));

import { fetchTemplateProject } from "../utils/api";
import { ProjectModel } from "../types/yamui";
import { ProjectProvider, useProject } from "./ProjectContext";

describe("ProjectContext", () => {
  const PROJECT_SNAPSHOT_STORAGE_KEY = "yamui_project_snapshot_v1";
  const PROJECT_SNAPSHOT_HISTORY_STORAGE_KEY = "yamui_project_snapshot_history_v1";
  const wrapper = ({ children }: { children: ReactNode }) => <ProjectProvider>{children}</ProjectProvider>;

  beforeEach(() => {
    vi.clearAllMocks();
    window.localStorage.clear();
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
  it("restores project state from local storage snapshot", () => {
    window.localStorage.setItem(
      PROJECT_SNAPSHOT_STORAGE_KEY,
      JSON.stringify({
        project: {
          app: {},
          state: {},
          translations: {
            en: {
              label: "English",
              entries: {},
              metadata: {},
            },
          },
          styles: {},
          components: {},
          screens: {
            saved: {
              name: "saved",
              title: "Saved Screen",
              initial: true,
              metadata: {},
              widgets: [],
            },
          },
        },
        editorTarget: { type: "screen", id: "saved" },
      })
    );

    const { result } = renderHook(() => useProject(), { wrapper });

    expect(result.current.project.screens.saved).toBeDefined();
    expect(result.current.editorTarget).toEqual({ type: "screen", id: "saved" });
  });

  it("persists project snapshot when project state changes", async () => {
    const { result } = renderHook(() => useProject(), { wrapper });

    act(() => {
      result.current.addScreen("autosave");
      result.current.setEditorTarget({ type: "screen", id: "autosave" });
    });

    await waitFor(() => {
      const raw = window.localStorage.getItem(PROJECT_SNAPSHOT_STORAGE_KEY);
      expect(raw).toBeTruthy();
      const parsed = JSON.parse(raw ?? "{}") as {
        project?: ProjectModel;
        editorTarget?: { type: string; id: string };
      };
      expect(parsed.project?.screens?.autosave).toBeDefined();
      expect(parsed.editorTarget).toEqual({ type: "screen", id: "autosave" });
      const historyRaw = window.localStorage.getItem(PROJECT_SNAPSHOT_HISTORY_STORAGE_KEY);
      expect(historyRaw).toBeTruthy();
      const history = JSON.parse(historyRaw ?? "[]") as Array<{ id: string }>;
      expect(history.length).toBeGreaterThan(0);
    });
  });
  it("restores a selected snapshot from history", async () => {
    const { result } = renderHook(() => useProject(), { wrapper });

    act(() => {
      result.current.addScreen("alpha");
      result.current.setEditorTarget({ type: "screen", id: "alpha" });
    });

    await waitFor(() => {
      expect(result.current.snapshotHistory.some((entry) => entry.editorTarget.id === "alpha")).toBe(true);
    });

    act(() => {
      result.current.addScreen("beta");
      result.current.setEditorTarget({ type: "screen", id: "beta" });
    });

    await waitFor(() => {
      expect(result.current.editorTarget.id).toBe("beta");
    });

    const alphaSnapshot = result.current.snapshotHistory
      .slice()
      .reverse()
      .find((entry) => entry.editorTarget.id === "alpha");

    expect(alphaSnapshot).toBeDefined();

    act(() => {
      const restored = result.current.restoreSnapshot(alphaSnapshot!.id);
      expect(restored).toBe(true);
    });

    expect(result.current.editorTarget.id).toBe("alpha");
    expect(result.current.project.screens.beta).toBeUndefined();
  });

  it("updates snapshot metadata and persists it", async () => {
    const { result } = renderHook(() => useProject(), { wrapper });
    const target = result.current.snapshotHistory[result.current.snapshotHistory.length - 1];
    expect(target).toBeDefined();

    act(() => {
      const updated = result.current.updateSnapshotMetadata(target!.id, {
        label: "Checkpoint A",
        note: "Before import",
      });
      expect(updated).toBe(true);
    });

    await waitFor(() => {
      const snapshot = result.current.snapshotHistory.find((entry) => entry.id === target!.id);
      expect(snapshot?.label).toBe("Checkpoint A");
      expect(snapshot?.note).toBe("Before import");
      const historyRaw = window.localStorage.getItem(PROJECT_SNAPSHOT_HISTORY_STORAGE_KEY);
      expect(historyRaw).toContain("Checkpoint A");
    });
  });

  it("retains pinned snapshots when non-pinned history rolls over", async () => {
    const { result } = renderHook(() => useProject(), { wrapper });
    const pinnedTarget = result.current.snapshotHistory[0];
    expect(pinnedTarget).toBeDefined();

    act(() => {
      const pinned = result.current.setSnapshotPinned(pinnedTarget!.id, true);
      expect(pinned).toBe(true);
    });

    act(() => {
      for (let i = 0; i < 40; i += 1) {
        const screenName = `overflow_${i}`;
        result.current.addScreen(screenName);
        result.current.setEditorTarget({ type: "screen", id: screenName });
      }
    });

    await waitFor(() => {
      const pinnedEntry = result.current.snapshotHistory.find((entry) => entry.id === pinnedTarget!.id);
      expect(pinnedEntry?.pinned).toBe(true);
      const unpinnedCount = result.current.snapshotHistory.filter((entry) => !entry.pinned).length;
      expect(unpinnedCount).toBeLessThanOrEqual(30);
    });
  });
});

