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
  slider: () => ({ ...baseWidget(), type: "slider", props: { min: 0, max: 100 } }),
  checkbox: () => ({ ...baseWidget(), type: "checkbox", text: "Checkbox" }),
  dropdown: () => ({ ...baseWidget(), type: "dropdown", props: { options: ["Option 1", "Option 2", "Option 3"] } }),
  roller: () => ({ ...baseWidget(), type: "roller", props: { options: ["Item 1", "Item 2", "Item 3"], visible_row_count: 3 } }),
  bar: () => ({ ...baseWidget(), type: "bar", props: { min: 0, max: 100, value: 50 } }),
  arc: () => ({ ...baseWidget(), type: "arc", props: { min: 0, max: 100, value: 50 } }),
  chart: () => ({ ...baseWidget(), type: "chart", props: { chart_type: "line", point_count: 7, min: 0, max: 100, series: [] } }),
  calendar: () => ({ ...baseWidget(), type: "calendar", props: { today: "", shown_month: "" } }),
  table: () => ({ ...baseWidget(), type: "table", props: { column_widths: [150, 150], rows: [] } }),
  tabview: () => ({ ...baseWidget(), type: "tabview", props: { tab_bar_position: "top", tab_bar_size: 44, active_tab: 0, tabs: [] } }),
  menu: () => ({ ...baseWidget(), type: "menu", props: { root_title: "Menu", header_mode: "top_fixed", items: [] } }),
  keyboard: () => ({ ...baseWidget(), type: "keyboard", props: { overlay: true, target: "" } }),
  led: () => ({ ...baseWidget(), type: "led", props: { color: "#22d3ee", width: 26, height: 26 } }),
  component: () => ({ ...baseWidget(), type: "component", props: { component: "" } }),
};

export function createWidget(type: WidgetType): WidgetNode {
  const factory = widgetFactories[type];
  if (!factory) {
    throw new Error(`Unknown widget type: ${type}`);
  }
  return { ...factory(), id: `widget_${crypto.randomUUID().slice(0, 8)}` };
}
