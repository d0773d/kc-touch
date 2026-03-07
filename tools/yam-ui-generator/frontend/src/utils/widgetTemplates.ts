import { WidgetNode, WidgetType } from "../types/yamui";

const baseWidget = (): WidgetNode => ({
  type: "label",
  id: undefined,
  text: undefined,
  style: undefined,
  props: {},
  events: {},
  bindings: {},
  accessibility: {},
  widgets: [],
});

const widgetFactories: Record<WidgetType, () => WidgetNode> = {
  row: () => ({ ...baseWidget(), type: "row", widgets: [] }),
  column: () => ({ ...baseWidget(), type: "column", widgets: [] }),
  panel: () => ({ ...baseWidget(), type: "panel", widgets: [] }),
  spacer: () => ({ ...baseWidget(), type: "spacer" }),
  list: () => ({ ...baseWidget(), type: "list", widgets: [] }),
  label: () => ({ ...baseWidget(), type: "label", text: "Label" }),
  button: () => ({ ...baseWidget(), type: "button", text: "Button" }),
  img: () => ({ ...baseWidget(), type: "img", src: "" }),
  textarea: () => ({ ...baseWidget(), type: "textarea", text: "" }),
  switch: () => ({ ...baseWidget(), type: "switch" }),
  slider: () => ({ ...baseWidget(), type: "slider" }),
  component: () => ({ ...baseWidget(), type: "component", props: { component: "" } }),
};

export function createWidget(type: WidgetType): WidgetNode {
  const factory = widgetFactories[type];
  if (!factory) {
    throw new Error(`Unknown widget type: ${type}`);
  }
  return { ...factory(), id: `widget_${crypto.randomUUID().slice(0, 8)}` };
}
