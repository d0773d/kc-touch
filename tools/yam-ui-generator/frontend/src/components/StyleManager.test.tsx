import { act } from "react";
import { fireEvent, render, screen, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import StyleManager from "./StyleManager";
import type { ProjectModel, StyleTokenModel, ValidationIssue, WidgetNode } from "../types/yamui";

const previewStyleMock = vi.fn();
const lintStylesMock = vi.fn();
const emitTelemetryMock = vi.fn();

vi.mock("../utils/api", () => ({
  previewStyle: (...args: unknown[]) => previewStyleMock(...args),
  lintStyles: (...args: unknown[]) => lintStylesMock(...args),
}));

vi.mock("../utils/telemetry", () => ({
  emitTelemetry: (...args: unknown[]) => emitTelemetryMock(...args),
}));

let mockContext: MockProjectContext;
let setTimeoutSpy: ReturnType<typeof vi.spyOn>;
let clearTimeoutSpy: ReturnType<typeof vi.spyOn>;
const flushAsync = async (): Promise<void> => {
  await act(async () => {
    await Promise.resolve();
  });
};

vi.mock("../context/ProjectContext", () => ({
  useProject: () => mockContext as never,
}));

type MockProjectContext = {
  project: ProjectModel;
  saveStyleToken: ReturnType<typeof vi.fn>;
  deleteStyleToken: ReturnType<typeof vi.fn>;
  styleEditorSelection: string | null;
  setStyleEditorSelection: ReturnType<typeof vi.fn>;
  selectWidget: ReturnType<typeof vi.fn>;
  setEditorTarget: ReturnType<typeof vi.fn>;
};

const createStyle = (name: string, overrides?: Partial<StyleTokenModel>): StyleTokenModel => ({
  name,
  category: "surface",
  description: "",
  value: { backgroundColor: "#ffffff", color: "#0f172a" },
  tags: [],
  metadata: {},
  ...overrides,
});

const createProject = (options?: {
  styles?: Record<string, StyleTokenModel>;
  widgets?: WidgetNode[];
}): ProjectModel => ({
  app: {},
  state: {},
  styles: options?.styles ?? {},
  components: {},
  screens: {
    main: {
      name: "main",
      title: "Main",
      initial: true,
      widgets: options?.widgets ?? [],
      metadata: {},
    },
  },
});

const setupContext = (project: ProjectModel, selection: string | null = null): MockProjectContext => ({
  project,
  saveStyleToken: vi.fn(),
  deleteStyleToken: vi.fn(),
  styleEditorSelection: selection,
  setStyleEditorSelection: vi.fn(),
  selectWidget: vi.fn(),
  setEditorTarget: vi.fn(),
});

describe("StyleManager guardrails", () => {
  beforeEach(() => {
    previewStyleMock.mockResolvedValue({
      category: "surface",
      backgroundColor: "#ffffff",
      color: "#0f172a",
      description: "",
      widget: null,
    });
    lintStylesMock.mockResolvedValue([]);
    vi.spyOn(window, "alert").mockImplementation(() => undefined);
    vi.spyOn(window, "confirm").mockImplementation(() => true);
    setTimeoutSpy = vi
      .spyOn(globalThis, "setTimeout")
      .mockImplementation(((callback: Parameters<typeof setTimeout>[0]) => {
        if (typeof callback === "function") {
          callback();
        }
        return 0 as ReturnType<typeof setTimeout>;
      }) as typeof setTimeout);
    clearTimeoutSpy = vi
      .spyOn(globalThis, "clearTimeout")
      .mockImplementation((() => undefined) as typeof clearTimeout);
    emitTelemetryMock.mockReset();
  });

  afterEach(() => {
    setTimeoutSpy?.mockRestore();
    clearTimeoutSpy?.mockRestore();
    vi.restoreAllMocks();
  });

  it("disables deletion while a style is still referenced", async () => {
    const primaryStyle = createStyle("primary");
    const project = createProject({
      styles: { primary: primaryStyle },
      widgets: [
        {
          type: "box",
          id: "hero",
          style: "primary",
          widgets: [],
        },
      ],
    });
    mockContext = setupContext(project, "primary");

    render(<StyleManager />);
    await flushAsync();

    const deleteButton = screen.getByRole("button", { name: "Delete" });
    expect(deleteButton).toBeDisabled();
    expect(screen.getByText(/Remove references before deleting/i)).toBeInTheDocument();
  });

  it("surfaces an error when renaming to an existing style name", async () => {
    const project = createProject({
      styles: {
        primary: createStyle("primary"),
        secondary: createStyle("secondary"),
      },
    });
    const saveSpy = vi.fn();
    mockContext = {
      ...setupContext(project, "primary"),
      saveStyleToken: saveSpy,
    };

    render(<StyleManager />);
    await flushAsync();
    const user = userEvent.setup();
    const nameInput = screen.getByLabelText("Style Name");
    await user.clear(nameInput);
    await user.type(nameInput, "secondary");
    await user.click(screen.getByRole("button", { name: "Save Style" }));
    await flushAsync();

    expect(screen.getByText("A different style already uses this name")).toBeInTheDocument();
    expect(saveSpy).not.toHaveBeenCalled();
  });

  it("shows usage counts and filters unused styles", async () => {
    const project = createProject({
      styles: {
        used: createStyle("used"),
        spare: createStyle("spare"),
      },
      widgets: [
        {
          type: "label",
          id: "hero",
          style: "used",
          widgets: [],
        },
      ],
    });
    mockContext = setupContext(project, "used");

    render(<StyleManager />);
    await flushAsync();

    expect(screen.getByText("1 use")).toBeInTheDocument();
    expect(screen.getByText("Unused")).toBeInTheDocument();

    const unusedToggle = screen.getByLabelText("Show unused only");
    const user = userEvent.setup();
    await user.click(unusedToggle);
    await flushAsync();

    expect(screen.queryByRole("button", { name: (value) => /^used\b/i.test(value) })).not.toBeInTheDocument();
    expect(screen.getByRole("button", { name: /spare/i })).toBeInTheDocument();
  });

  it("shows owner badges for each usage entry", async () => {
    const project = createProject({
      styles: {
        primary: createStyle("primary"),
      },
      widgets: [
        {
          type: "label",
          id: "hero",
          style: "primary",
          widgets: [],
        },
      ],
    });
    project.components = {
      card: {
        name: "card",
        title: "Card",
        widgets: [
          {
            type: "box",
            id: "cardRoot",
            style: "primary",
            widgets: [],
          },
        ],
        metadata: {},
      },
    };
    mockContext = setupContext(project, "primary");

    render(<StyleManager />);
    await flushAsync();

    const screenBadge = screen.getByTestId("style-usage-owner-screen-main");
    expect(screenBadge).toHaveTextContent(/Screen/i);
    expect(screenBadge).toHaveTextContent(/main/i);

    const componentBadge = screen.getByTestId("style-usage-owner-component-card");
    expect(componentBadge).toHaveTextContent(/Component/i);
    expect(componentBadge).toHaveTextContent(/card/i);
  });

  it("highlights usage entries when focused", async () => {
    const project = createProject({
      styles: {
        primary: createStyle("primary"),
      },
      widgets: [
        {
          type: "label",
          id: "hero",
          style: "primary",
          widgets: [],
        },
        {
          type: "box",
          id: "footer",
          style: "primary",
          widgets: [],
        },
      ],
    });
    mockContext = setupContext(project, "primary");

    render(<StyleManager />);
    await flushAsync();

    const usageButtons = screen.getAllByTestId(/style-usage-item-/);
    expect(usageButtons.every((node) => !node.classList.contains("is-active"))).toBe(true);

    const user = userEvent.setup();
    await user.click(usageButtons[0]!);
    await flushAsync();
    expect(usageButtons[0]!.classList.contains("is-active")).toBe(true);
    expect(emitTelemetryMock).toHaveBeenCalledTimes(1);
    expect(emitTelemetryMock).toHaveBeenLastCalledWith(
      "styles",
      "style_usage_focus",
      expect.objectContaining({
        style: "primary",
        usageIndex: 0,
        origin: "list_click",
      })
    );

    const focusNextButton = screen.getByRole("button", { name: "Focus next match" });
    await user.click(focusNextButton);
    await flushAsync();

    const updatedButtons = screen.getAllByTestId(/style-usage-item-/);
    expect(updatedButtons[1]!.classList.contains("is-active")).toBe(true);
    expect(updatedButtons[0]!.classList.contains("is-active")).toBe(false);
    expect(emitTelemetryMock).toHaveBeenCalledTimes(2);
    expect(emitTelemetryMock).toHaveBeenLastCalledWith(
      "styles",
      "style_usage_focus",
      expect.objectContaining({
        style: "primary",
        usageIndex: 1,
        origin: "cycle",
      })
    );
  });

  it("filters usage entries by owner and widget type", async () => {
    const project = createProject({
      styles: {
        primary: createStyle("primary"),
      },
      widgets: [
        {
          type: "label",
          id: "hero",
          style: "primary",
          widgets: [],
        },
      ],
    });
    project.components = {
      card: {
        name: "card",
        title: "Card",
        widgets: [
          {
            type: "box",
            id: "cardRoot",
            style: "primary",
            widgets: [],
          },
        ],
        metadata: {},
      },
    };
    mockContext = setupContext(project, "primary");

    render(<StyleManager />);
    await flushAsync();

    expect(screen.getAllByTestId(/style-usage-item-/)).toHaveLength(2);

    const componentFilter = screen.getByTestId("usage-owner-filter-component");
    const user = userEvent.setup();
    await user.click(componentFilter);
    await flushAsync();

    const componentOnly = screen.getAllByTestId(/style-usage-item-/);
    expect(componentOnly).toHaveLength(1);
    expect(within(componentOnly[0]!).getByTestId("style-usage-owner-component-card")).toBeInTheDocument();

    const widgetSelect = screen.getByLabelText("Widget type filter") as HTMLSelectElement;
    await user.selectOptions(widgetSelect, "label");
    await flushAsync();

    expect(screen.getByText("No matches for the current usage filters.")).toBeInTheDocument();

    const resetUsageFilters = screen.getByRole("button", { name: "Reset usage filters" });
    await user.click(resetUsageFilters);
    await flushAsync();

    expect(screen.getAllByTestId(/style-usage-item-/)).toHaveLength(2);
  });

  it("resets search, category, lint, and unused filters", async () => {
    const project = createProject({
      styles: {
        used: createStyle("used"),
        spare: createStyle("spare"),
      },
    });
    mockContext = setupContext(project, null);

    render(<StyleManager />);
    await flushAsync();

    const searchInput = screen.getByPlaceholderText("Search styles by name, tag, or description");
    const lintFilterSelect = screen.getByLabelText("Lint filter");
    const unusedToggle = screen.getByLabelText("Show unused only");
    const resetFiltersButton = screen.getByRole("button", { name: "Reset filters" });

    expect(resetFiltersButton).toBeDisabled();

    const user = userEvent.setup();
    await user.type(searchInput, "used");
    await user.click(unusedToggle);
    await user.selectOptions(lintFilterSelect, "Errors only");
    await flushAsync();

    expect(resetFiltersButton).not.toBeDisabled();
    expect(screen.getByText(/No styles match/i)).toBeInTheDocument();
    const summaryPill = () => screen.getByTestId("style-results-summary");
    expect(summaryPill()).toHaveTextContent("Showing 0 of 2");

    await user.click(resetFiltersButton);
    await flushAsync();

    expect(searchInput).toHaveValue("");
    expect(unusedToggle).not.toBeChecked();
    expect((lintFilterSelect as HTMLSelectElement).value).toBe("all");
    expect(resetFiltersButton).toBeDisabled();
    expect(summaryPill()).toHaveTextContent("2 styles");
    expect(screen.getAllByText(/^used$/i)).toHaveLength(1);
    expect(screen.getAllByText(/^spare$/i)).toHaveLength(1);
  });

  it("filters styles by lint severity", async () => {
    const project = createProject({
      styles: {
        broken: createStyle("broken"),
        caution: createStyle("caution"),
        clean: createStyle("clean"),
      },
    });
    const lintIssues: ValidationIssue[] = [
      {
        path: "styles/broken/backgroundColor",
        message: "Missing background",
        severity: "error",
      },
      {
        path: "styles/caution/value",
        message: "Incomplete contrast",
        severity: "warning",
      },
    ];
    lintStylesMock.mockResolvedValue(lintIssues);
    mockContext = setupContext(project, "broken");

    render(<StyleManager />);
    await flushAsync();

    const lintFilterSelect = screen.getByLabelText("Lint filter");
    const user = userEvent.setup();

    expect(screen.getByRole("button", { name: /clean/i })).toBeInTheDocument();
    const lintSummaryPill = () => screen.getByTestId("style-lint-summary");
    expect(lintSummaryPill()).toHaveTextContent("1 error • 1 warning");

    await user.selectOptions(lintFilterSelect, "Only lint issues");
    await flushAsync();

    expect(screen.queryByRole("button", { name: /clean/i })).not.toBeInTheDocument();
    expect(screen.getByRole("button", { name: /broken/i })).toBeInTheDocument();
    expect(screen.getByRole("button", { name: /caution/i })).toBeInTheDocument();
    expect(lintSummaryPill()).toHaveTextContent("1 error • 1 warning");

    await user.selectOptions(lintFilterSelect, "Errors only");
    await flushAsync();

    expect(screen.getByRole("button", { name: /broken/i })).toBeInTheDocument();
    expect(screen.queryByRole("button", { name: /caution/i })).not.toBeInTheDocument();
    expect(lintSummaryPill()).toHaveTextContent("1 error");
  });

  it("opens the lint drawer and navigates to an offending style", async () => {
    const project = createProject({
      styles: {
        broken: createStyle("broken"),
        caution: createStyle("caution"),
      },
    });
    const lintIssues: ValidationIssue[] = [
      {
        path: "styles/broken/backgroundColor",
        message: "Missing background",
        severity: "error",
      },
      {
        path: "styles/caution/value",
        message: "Low contrast",
        severity: "warning",
      },
    ];
    lintStylesMock.mockResolvedValue(lintIssues);
    mockContext = setupContext(project, "broken");
    const selectSpy = mockContext.setStyleEditorSelection;

    render(<StyleManager />);
    await flushAsync();

    const lintSummaryButton = screen.getByTestId("style-lint-summary");
    const user = userEvent.setup();
    await user.click(lintSummaryButton);
    await flushAsync();

    const drawer = screen.getByRole("dialog", { name: "Lint breakdown" });
    expect(drawer).toBeInTheDocument();
    expect(within(drawer).getByText(/Errors/i)).toBeInTheDocument();
    expect(within(drawer).getByText(/Warnings/i)).toBeInTheDocument();

    const brokenButton = within(drawer).getByRole("button", { name: /broken/i });
    await user.click(brokenButton);
    await flushAsync();

    expect(selectSpy).toHaveBeenCalledWith("broken");
    expect(screen.queryByRole("dialog", { name: "Lint breakdown" })).not.toBeInTheDocument();
  });

  it("copies the value JSON to the clipboard", async () => {
    const style = createStyle("primary");
    const project = createProject({
      styles: {
        primary: style,
      },
    });
    mockContext = setupContext(project, "primary");
    const execCommandSpy = vi.fn().mockReturnValue(true);
    (document as Document & { execCommand?: typeof document.execCommand }).execCommand = execCommandSpy as typeof document.execCommand;
    const clipboard = (navigator as Navigator & { clipboard?: Clipboard }).clipboard;
    const clipboardSpy = clipboard && "writeText" in clipboard ? vi.spyOn(clipboard, "writeText").mockRejectedValue(new Error("denied")) : null;

    render(<StyleManager />);
    await flushAsync();

    const copyButton = screen.getByRole("button", { name: "Copy Value JSON" });
    const valueTextarea = screen.getByLabelText("Value JSON", { selector: "textarea" });
    const user = userEvent.setup();
    await user.click(copyButton);
    await flushAsync();

    expect(execCommandSpy).toHaveBeenCalledTimes(1);
    expect(screen.getByText("Copied!")).toBeInTheDocument();

    delete (document as Document & { execCommand?: typeof document.execCommand }).execCommand;
    clipboardSpy?.mockRestore();
  });

  it("copies the metadata JSON to the clipboard", async () => {
    const style = createStyle("primary", { metadata: { source: "hero" } });
    const project = createProject({
      styles: {
        primary: style,
      },
    });
    mockContext = setupContext(project, "primary");
    const execCommandSpy = vi.fn().mockReturnValue(true);
    (document as Document & { execCommand?: typeof document.execCommand }).execCommand = execCommandSpy as typeof document.execCommand;
    const clipboard = (navigator as Navigator & { clipboard?: Clipboard }).clipboard;
    const clipboardSpy = clipboard && "writeText" in clipboard ? vi.spyOn(clipboard, "writeText").mockRejectedValue(new Error("denied")) : null;

    render(<StyleManager />);
    await flushAsync();

    const metadataTextarea = screen.getByLabelText("Metadata JSON", { selector: "textarea" });
    const copyButton = screen.getByRole("button", { name: "Copy Metadata JSON" });
    const user = userEvent.setup();
    await user.click(copyButton);
    await flushAsync();

    expect(execCommandSpy).toHaveBeenCalledTimes(1);
    const metadataField = metadataTextarea.closest("label");
    expect(metadataField).not.toBeNull();
    expect(within(metadataField as HTMLElement).getByText("Copied!")).toBeInTheDocument();

    delete (document as Document & { execCommand?: typeof document.execCommand }).execCommand;
    clipboardSpy?.mockRestore();
  });

  it("shows a copy error when execCommand fallback fails", async () => {
    const style = createStyle("primary");
    const project = createProject({
      styles: {
        primary: style,
      },
    });
    mockContext = setupContext(project, "primary");
    const execCommandSpy = vi.fn().mockReturnValue(false);
    (document as Document & { execCommand?: typeof document.execCommand }).execCommand = execCommandSpy as typeof document.execCommand;
    const clipboard = (navigator as Navigator & { clipboard?: Clipboard }).clipboard;
    const clipboardSpy = clipboard && "writeText" in clipboard ? vi.spyOn(clipboard, "writeText").mockRejectedValue(new Error("denied")) : null;

    render(<StyleManager />);
    await flushAsync();

    const copyButton = screen.getByRole("button", { name: "Copy Value JSON" });
    const user = userEvent.setup();
    await user.click(copyButton);
    await flushAsync();

    expect(execCommandSpy).toHaveBeenCalled();
    expect(screen.getByText("Copy failed")).toBeInTheDocument();

    delete (document as Document & { execCommand?: typeof document.execCommand }).execCommand;
    clipboardSpy?.mockRestore();
  });

  it("formats the value JSON and shows a success hint", async () => {
    const project = createProject({
      styles: {
        primary: createStyle("primary"),
      },
    });
    mockContext = setupContext(project, "primary");

    render(<StyleManager />);
    await flushAsync();

    const valueTextarea = screen.getByLabelText("Value JSON", { selector: "textarea" });
    const formatButton = screen.getByRole("button", { name: "Format Value JSON" });
    fireEvent.change(valueTextarea, { target: { value: '{"backgroundColor":"#ffffff"}' } });
    const user = userEvent.setup();
    await user.click(formatButton);
    await flushAsync();

    expect(valueTextarea).toHaveValue(`{
  "backgroundColor": "#ffffff"
}`);
    expect(screen.getByText("Formatted")).toBeInTheDocument();
  });

  it("surfaces an error when formatting invalid JSON", async () => {
    const project = createProject({
      styles: {
        primary: createStyle("primary"),
      },
    });
    mockContext = setupContext(project, "primary");

    render(<StyleManager />);
    await flushAsync();

    const valueTextarea = screen.getByLabelText("Value JSON", { selector: "textarea" });
    const formatButton = screen.getByRole("button", { name: "Format Value JSON" });
    fireEvent.change(valueTextarea, { target: { value: "{" } });
    const user = userEvent.setup();
    await user.click(formatButton);
    await flushAsync();

    expect(screen.getByText("Format failed")).toBeInTheDocument();
    expect(screen.getByText("Value JSON is invalid")).toBeInTheDocument();
  });

  it("formats the metadata JSON and shows a success hint", async () => {
    const project = createProject({
      styles: {
        primary: createStyle("primary"),
      },
    });
    mockContext = setupContext(project, "primary");

    render(<StyleManager />);
    await flushAsync();

    const metadataTextarea = screen.getByLabelText("Metadata JSON", { selector: "textarea" });
    const formatButton = screen.getByRole("button", { name: "Format Metadata JSON" });
    fireEvent.change(metadataTextarea, { target: { value: '{"source":"hero"}' } });
    const user = userEvent.setup();
    await user.click(formatButton);
    await flushAsync();

    expect(metadataTextarea).toHaveValue(`{
  "source": "hero"
}`);
    const metadataField = metadataTextarea.closest("label");
    expect(metadataField).not.toBeNull();
    expect(within(metadataField as HTMLElement).getByText("Formatted")).toBeInTheDocument();
  });

  it("surfaces an error when formatting invalid metadata JSON", async () => {
    const project = createProject({
      styles: {
        primary: createStyle("primary"),
      },
    });
    mockContext = setupContext(project, "primary");

    render(<StyleManager />);
    await flushAsync();

    const metadataTextarea = screen.getByLabelText("Metadata JSON", { selector: "textarea" });
    const formatButton = screen.getByRole("button", { name: "Format Metadata JSON" });
    fireEvent.change(metadataTextarea, { target: { value: "{" } });
    const user = userEvent.setup();
    await user.click(formatButton);
    await flushAsync();

    const metadataField = metadataTextarea.closest("label");
    expect(metadataField).not.toBeNull();
    expect(within(metadataField as HTMLElement).getByText("Format failed")).toBeInTheDocument();
    expect(screen.getByText("Metadata JSON is invalid")).toBeInTheDocument();
  });

  it("resets the form back to the saved style", async () => {
    const style = createStyle("primary", { description: "Base" });
    const project = createProject({
      styles: {
        primary: style,
      },
    });
    mockContext = setupContext(project, "primary");

    render(<StyleManager />);
    await flushAsync();

    const resetButton = screen.getByRole("button", { name: "Reset Changes" });
    const saveButton = screen.getByRole("button", { name: "Save Style" });
    expect(resetButton).toBeDisabled();
    expect(saveButton).toBeDisabled();
    expect(screen.getByText("All changes saved")).toBeInTheDocument();

    const descriptionInput = screen.getByLabelText("Description");
    const user = userEvent.setup();
    await user.clear(descriptionInput);
    await user.type(descriptionInput, "Base updated");
    await flushAsync();

    expect(resetButton).not.toBeDisabled();
    expect(saveButton).not.toBeDisabled();
    expect(screen.getByText("Unsaved edits")).toBeInTheDocument();

    await user.click(resetButton);
    await flushAsync();

    expect(descriptionInput).toHaveValue("Base");
    expect(resetButton).toBeDisabled();
    expect(saveButton).toBeDisabled();
    expect(screen.getByText("All changes saved")).toBeInTheDocument();
  });
});
