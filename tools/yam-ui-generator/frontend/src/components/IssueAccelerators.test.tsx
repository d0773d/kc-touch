import { fireEvent, render, screen } from "@testing-library/react";
import { beforeEach, describe, expect, it, vi } from "vitest";
import IssueAccelerators from "./IssueAccelerators";
import { useProject } from "../context/ProjectContext";
import { ProjectModel, ValidationIssue } from "../types/yamui";

vi.mock("../context/ProjectContext", () => ({
  useProject: vi.fn(),
}));

const buildProject = (): ProjectModel => ({
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
    main: {
      name: "main",
      initial: true,
      widgets: [
        {
          type: "img",
          id: "hero",
          src: "media/hero.png",
          bindings: { value: "{{ state.dashboard.title }}" },
          events: { on_click: ["push(settings)"] },
        },
      ],
      metadata: {},
    },
  },
});

describe("IssueAccelerators", () => {
  const useProjectMock = useProject as unknown as vi.Mock;
  const setEditorTarget = vi.fn();
  const selectWidget = vi.fn();
  const setStyleEditorSelection = vi.fn();

  beforeEach(() => {
    vi.clearAllMocks();
    useProjectMock.mockReturnValue({
      project: buildProject(),
      setEditorTarget,
      selectWidget,
      setStyleEditorSelection,
    });
  });

  it("treats src/bindings/events widget findings as actionable", () => {
    const issues: ValidationIssue[] = [
      { path: "/screens/main/widgets/0/src", message: "Asset missing", severity: "warning" },
      { path: "/screens/main/widgets/0/bindings/value", message: "State missing", severity: "warning" },
      { path: "/screens/main/widgets/0/events/on_click/0", message: "Target missing", severity: "warning" },
    ];

    render(<IssueAccelerators issues={issues} />);

    fireEvent.click(screen.getByRole("button", { name: /Asset missing/i }));
    expect(setEditorTarget).toHaveBeenCalledWith({ type: "screen", id: "main" });
    expect(selectWidget).toHaveBeenCalledWith([0]);

    fireEvent.click(screen.getByRole("button", { name: /State missing/i }));
    expect(setEditorTarget).toHaveBeenCalledWith({ type: "screen", id: "main" });
    expect(selectWidget).toHaveBeenCalledWith([0]);

    fireEvent.click(screen.getByRole("button", { name: /Target missing/i }));
    expect(setEditorTarget).toHaveBeenCalledWith({ type: "screen", id: "main" });
    expect(selectWidget).toHaveBeenCalledWith([0]);
  });

  it("reports non-actionable issues separately", () => {
    const issues: ValidationIssue[] = [
      { path: "/app/locale", message: "Locale missing", severity: "warning" },
      { path: "/screens/main/widgets/0/src", message: "Asset missing", severity: "warning" },
    ];

    render(<IssueAccelerators issues={issues} />);

    expect(screen.getByText(/1 issue require YAML or style updates/i)).toBeInTheDocument();
  });
});
