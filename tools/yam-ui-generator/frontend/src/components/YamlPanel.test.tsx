import { fireEvent, render, screen, waitFor } from "@testing-library/react";
import { useState } from "react";
import { describe, expect, it, vi, beforeEach } from "vitest";
import YamlPanel from "./YamlPanel";
import { ProjectProvider } from "../context/ProjectContext";
import { ProjectModel, ValidationIssue } from "../types/yamui";
import { exportProject, renderPreviewContract } from "../utils/api";

vi.mock("../utils/api", () => ({
  exportProject: vi.fn(),
  renderPreviewContract: vi.fn(),
}));

function PanelHarness({ project }: { project: ProjectModel }) {
  const [issues, setIssues] = useState<ValidationIssue[]>([]);
  return (
    <ProjectProvider>
      <YamlPanel project={project} issues={issues} onIssues={setIssues} />
    </ProjectProvider>
  );
}

const buildProject = (propsValue: Record<string, unknown>): ProjectModel => ({
  app: {},
  state: {},
  translations: {
    en: {
      label: "English",
      entries: {},
    },
  },
  styles: {},
  components: {},
  screens: {
    main: {
      name: "main",
      widgets: [
        {
          type: "label",
          id: "widget_1",
          text: typeof propsValue.label === "string" ? propsValue.label : "",
          props: propsValue,
        },
      ],
      metadata: {},
    },
  },
});

describe("YamlPanel", () => {
  const exportProjectMock = exportProject as vi.MockedFunction<typeof exportProject>;
  const renderPreviewContractMock = renderPreviewContract as vi.MockedFunction<typeof renderPreviewContract>;

  beforeEach(() => {
    exportProjectMock.mockReset();
    renderPreviewContractMock.mockReset();
    renderPreviewContractMock.mockResolvedValue({ status: "ok", summary: {}, findings: [] });
  });

  it("resyncs YAML whenever the project snapshot changes", async () => {
    exportProjectMock.mockResolvedValueOnce({ yaml: "screens: {}\n", issues: [] });

    const firstProject = buildProject({ label: "Alpha" });
    const secondProject = buildProject({ label: "Beta" });

    const { rerender } = render(<PanelHarness project={firstProject} />);

    await waitFor(() => expect(exportProjectMock).toHaveBeenCalledTimes(1));
    expect(exportProjectMock).toHaveBeenCalledWith(firstProject);
    await waitFor(() => expect(screen.getByRole("textbox")).toHaveValue("screens: {}\n"));

    exportProjectMock.mockResolvedValueOnce({ yaml: "screens:\n  main: {}\n", issues: [] });

    rerender(<PanelHarness project={secondProject} />);

    await waitFor(() => expect(exportProjectMock).toHaveBeenCalledTimes(2));
    expect(exportProjectMock).toHaveBeenLastCalledWith(secondProject);
    await waitFor(() => expect(screen.getByRole("textbox")).toHaveValue("screens:\n  main: {}\n"));
  });

  it("renders a live preview from project state", async () => {
    exportProjectMock.mockResolvedValueOnce({ yaml: "screens: {}\n", issues: [] });
    const project = buildProject({ label: "Live title" });

    render(<PanelHarness project={project} />);

    await waitFor(() => expect(exportProjectMock).toHaveBeenCalledTimes(1));

    fireEvent.click(screen.getByRole("button", { name: "Live" }));

    await waitFor(() => {
      expect(screen.getByText("Live title")).toBeInTheDocument();
      expect(screen.getByText("Preview rendered without findings")).toBeInTheDocument();
    });
  });
  it("shows live preview findings for unresolved components", async () => {
    exportProjectMock.mockResolvedValueOnce({ yaml: "screens: {}\n", issues: [] });
    const project: ProjectModel = {
      app: {},
      state: {},
      translations: {
        en: {
          label: "English",
          entries: {},
        },
      },
      styles: {},
      components: {},
      screens: {
        main: {
          name: "main",
          widgets: [
            {
              type: "component",
              id: "widget_2",
              props: { component: "missing_card" },
            },
          ],
          metadata: {},
        },
      },
    };

    render(<PanelHarness project={project} />);

    await waitFor(() => expect(exportProjectMock).toHaveBeenCalledTimes(1));

    fireEvent.click(screen.getByRole("button", { name: "Live" }));

    await waitFor(() => {
      expect(screen.getByText(/Component "missing_card" is not defined/i)).toBeInTheDocument();
      expect(screen.getByText("error")).toBeInTheDocument();
    });
  });

  it("shows backend preview findings when contract returns issues", async () => {
    exportProjectMock.mockResolvedValueOnce({ yaml: "screens: {}\n", issues: [] });
    renderPreviewContractMock.mockResolvedValueOnce({
      status: "issues",
      summary: { finding_count: 1 },
      findings: [
        {
          path: "/app",
          message: "Missing initial screen",
          severity: "warning",
        },
      ],
    });
    const project = buildProject({ label: "Live title" });

    render(<PanelHarness project={project} />);

    await waitFor(() => expect(exportProjectMock).toHaveBeenCalledTimes(1));
    fireEvent.click(screen.getByRole("button", { name: "Live" }));

    await waitFor(() => {
      expect(screen.getByText(/Missing initial screen/i)).toBeInTheDocument();
      expect(screen.getByText("PREVIEW API")).toBeInTheDocument();
    });
  });
});

