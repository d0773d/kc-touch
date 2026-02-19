import { render, screen, waitFor } from "@testing-library/react";
import { useState } from "react";
import { describe, expect, it, vi, beforeEach } from "vitest";
import YamlPanel from "./YamlPanel";
import { ProjectProvider } from "../context/ProjectContext";
import { ProjectModel, ValidationIssue } from "../types/yamui";
import { exportProject } from "../utils/api";

vi.mock("../utils/api", () => ({
  exportProject: vi.fn(),
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
          props: propsValue,
        },
      ],
      metadata: {},
    },
  },
});

describe("YamlPanel", () => {
  const exportProjectMock = exportProject as vi.MockedFunction<typeof exportProject>;

  beforeEach(() => {
    exportProjectMock.mockReset();
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
});
