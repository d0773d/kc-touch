import { render, screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { afterEach, beforeAll, describe, expect, it, vi } from "vitest";
import TranslationManager from "./TranslationManager";
import type { ProjectModel, TranslationLocaleModel, TranslationStore } from "../types/yamui";

let mockContext: MockProjectContext;
const exportTranslationsMock = vi.fn();
const importTranslationsMock = vi.fn();

vi.mock("../context/ProjectContext", () => ({
  useProject: () => mockContext as never,
}));

vi.mock("../utils/api", () => ({
  exportTranslations: (...args: unknown[]) => exportTranslationsMock(...args),
  importTranslations: (...args: unknown[]) => importTranslationsMock(...args),
}));

beforeAll(() => {
  Object.defineProperty(window.HTMLElement.prototype, "scrollIntoView", {
    value: vi.fn(),
    writable: true,
  });
});

afterEach(() => {
  vi.clearAllMocks();
});

interface MockProjectContext {
  project: ProjectModel;
  addTranslationLocale: ReturnType<typeof vi.fn>;
  removeTranslationLocale: ReturnType<typeof vi.fn>;
  setTranslationLocaleLabel: ReturnType<typeof vi.fn>;
  addTranslationKey: ReturnType<typeof vi.fn>;
  renameTranslationKey: ReturnType<typeof vi.fn>;
  updateTranslationValue: ReturnType<typeof vi.fn>;
  deleteTranslationKey: ReturnType<typeof vi.fn>;
  setTranslations: ReturnType<typeof vi.fn>;
  translationFocusRequest: { key: string; origin?: string } | null;
  clearTranslationFocusRequest: ReturnType<typeof vi.fn>;
}

const createLocale = (label: string, entries: Record<string, string>): TranslationLocaleModel => ({
  label,
  entries,
  metadata: {},
});

const createProject = (translations: TranslationStore): ProjectModel => ({
  app: { locale: "en" },
  state: {},
  translations,
  styles: {},
  components: {},
  screens: {
    main: {
      name: "main",
      title: "Main",
      initial: true,
      widgets: [],
      metadata: {},
    },
  },
});

const createMockContext = (translations: TranslationStore, overrides?: Partial<MockProjectContext>): MockProjectContext => {
  const base: MockProjectContext = {
    project: createProject(translations),
    addTranslationLocale: vi.fn(),
    removeTranslationLocale: vi.fn(),
    setTranslationLocaleLabel: vi.fn(),
    addTranslationKey: vi.fn(),
    renameTranslationKey: vi.fn().mockReturnValue(true),
    updateTranslationValue: vi.fn(),
    deleteTranslationKey: vi.fn(),
    setTranslations: vi.fn(),
    translationFocusRequest: null,
    clearTranslationFocusRequest: vi.fn(),
  };
  return { ...base, ...overrides };
};

describe("TranslationManager guardrails", () => {
  it("filters keys via missing-locale chips", async () => {
    const translations: TranslationStore = {
      en: createLocale("English", { greeting: "Hello", farewell: "Goodbye" }),
      es: createLocale("Spanish", { greeting: "", farewell: "" }),
      fr: createLocale("French", { greeting: "", farewell: "Au revoir" }),
    };
    mockContext = createMockContext(translations);

    render(<TranslationManager issues={[]} />);
    const user = userEvent.setup();

    expect(screen.getByRole("button", { name: /es\s+\u00b7\s+2/i })).toBeInTheDocument();
    const frChip = screen.getByRole("button", { name: /fr\s+\u00b7\s+1/i });
    await user.click(frChip);

    expect(screen.getByDisplayValue("greeting")).toBeInTheDocument();
    expect(screen.queryByDisplayValue("farewell")).not.toBeInTheDocument();

    const clearButton = screen.getByRole("button", { name: "Clear locale filter" });
    await user.click(clearButton);

    expect(screen.getByDisplayValue("farewell")).toBeInTheDocument();
    expect(screen.queryByRole("button", { name: "Clear locale filter" })).not.toBeInTheDocument();
  });

  it("fills missing locales using the preview modal", async () => {
    const translations: TranslationStore = {
      en: createLocale("English", { greeting: "Hello!" }),
      es: createLocale("Spanish", { greeting: "" }),
      fr: createLocale("French", { greeting: "" }),
    };
    const updateSpy = vi.fn();
    mockContext = createMockContext(translations, { updateTranslationValue: updateSpy });

    render(<TranslationManager issues={[]} />);
    const user = userEvent.setup();

    await user.click(screen.getByRole("button", { name: /Preview fill from English/i }));
    expect(screen.getByText("Fill greeting")).toBeInTheDocument();
    expect(screen.getByText("Locales missing this key")).toBeInTheDocument();

    await user.click(screen.getByRole("button", { name: "Fill missing locales" }));

    expect(updateSpy).toHaveBeenCalledTimes(2);
    expect(updateSpy).toHaveBeenNthCalledWith(1, "es", "greeting", "Hello!");
    expect(updateSpy).toHaveBeenNthCalledWith(2, "fr", "greeting", "Hello!");

    await waitFor(() => expect(screen.queryByText("Fill greeting")).not.toBeInTheDocument());
  });

  it("handles translation focus handoffs from the inspector", async () => {
    const translations: TranslationStore = {
      en: createLocale("English", { greeting: "Hello", farewell: "Later" }),
      es: createLocale("Spanish", { greeting: "Hola", farewell: "" }),
    };
    const clearFocus = vi.fn();
    mockContext = createMockContext(translations, {
      translationFocusRequest: { key: "farewell", origin: "inspector" },
      clearTranslationFocusRequest: clearFocus,
    });

    render(<TranslationManager issues={[]} />);

    await waitFor(() => expect(clearFocus).toHaveBeenCalledTimes(1));
    const toast = screen.getByText(/Jumped to farewell from Inspector/i);
    expect(toast).toBeInTheDocument();

    await waitFor(() => {
      const keyRow = document.querySelector('[data-translation-key="farewell"]');
      expect(keyRow).not.toBeNull();
      expect(keyRow).toHaveClass("is-focused");
    });
  });
});
