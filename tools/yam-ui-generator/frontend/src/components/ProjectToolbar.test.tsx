import { fireEvent, render, screen, waitFor } from "@testing-library/react";
import { beforeEach, describe, expect, it, vi } from "vitest";
import ProjectToolbar from "./ProjectToolbar";
import { useProject } from "../context/ProjectContext";
import { ProjectModel } from "../types/yamui";

vi.mock("../context/ProjectContext", () => ({
  useProject: vi.fn(),
}));

vi.mock("../utils/api", () => ({
  exportProject: vi.fn(),
  importProject: vi.fn(),
  lintStyles: vi.fn(),
  validateProject: vi.fn(),
}));

const buildProject = (screenName: string): ProjectModel => ({
  app: { initial_screen: screenName },
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
    [screenName]: {
      name: screenName,
      initial: true,
      widgets: [],
      metadata: {},
    },
  },
});

describe("ProjectToolbar restore flow", () => {
  const useProjectMock = useProject as unknown as vi.Mock;
  const restoreSnapshot = vi.fn();
  const updateSnapshotMetadata = vi.fn();
  const setSnapshotPinned = vi.fn();
  const setProject = vi.fn();
  const setLastExport = vi.fn();
  const loadTemplateProject = vi.fn();
  const onIssues = vi.fn();

  beforeEach(() => {
    vi.clearAllMocks();
    useProjectMock.mockReturnValue({
      project: buildProject("main"),
      setProject,
      lastExport: undefined,
      setLastExport,
      loadTemplateProject,
      snapshotHistory: [
        {
          id: "snap_1",
          savedAt: 1710000000000,
          label: "Before refactor",
          note: "Known-good state",
          pinned: true,
          project: buildProject("old_main"),
          editorTarget: { type: "screen", id: "old_main" },
        },
      ],
      restoreSnapshot,
      updateSnapshotMetadata,
      setSnapshotPinned,
    });
    restoreSnapshot.mockReturnValue(true);
  });

  it("shows optional diff preview in confirm restore modal", async () => {
    render(<ProjectToolbar onIssues={onIssues} />);

    fireEvent.click(screen.getByRole("button", { name: "Restore Snapshot" }));
    fireEvent.click(screen.getByRole("button", { name: "Restore" }));

    await waitFor(() => {
      expect(screen.getByRole("dialog", { name: "Confirm Snapshot Restore" })).toBeInTheDocument();
    });

    expect(screen.getByText("Show diff preview (optional)")).toBeInTheDocument();
    fireEvent.click(screen.getByText("Show diff preview (optional)"));
    expect(screen.getByText(/Widget delta after restore:/i)).toBeInTheDocument();
    expect(screen.getByText(/Initial screen:/i)).toBeInTheDocument();
  });

  it("allows keyboard escape to close restore modal", async () => {
    render(<ProjectToolbar onIssues={onIssues} />);

    fireEvent.click(screen.getByRole("button", { name: "Restore Snapshot" }));
    expect(screen.getByRole("dialog", { name: "Restore Snapshot" })).toBeInTheDocument();

    fireEvent.keyDown(window, { key: "Escape" });

    await waitFor(() => {
      expect(screen.queryByRole("dialog", { name: "Restore Snapshot" })).not.toBeInTheDocument();
    });
  });

  it("restores selected snapshot from confirmation modal", async () => {
    render(<ProjectToolbar onIssues={onIssues} />);

    fireEvent.click(screen.getByRole("button", { name: "Restore Snapshot" }));
    fireEvent.click(screen.getByRole("button", { name: "Restore" }));
    fireEvent.click(screen.getByRole("button", { name: "Restore now" }));

    await waitFor(() => {
      expect(restoreSnapshot).toHaveBeenCalledWith("snap_1");
      expect(onIssues).toHaveBeenCalledWith([]);
    });
  });
});
