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
  spinner: () => ({ ...baseWidget(), type: "spinner", props: { duration: 1000, arc_sweep: 240 } }),
  line: () => ({ ...baseWidget(), type: "line", props: { points: [[0, 0], [100, 50], [200, 0]], y_invert: false } }),
  qrcode: () => ({ ...baseWidget(), type: "qrcode", props: { data: "https://example.com", size: 150 } }),
  spinbox: () => ({ ...baseWidget(), type: "spinbox", props: { value: 0, min: -100, max: 100, step: 1, digit_count: 5, decimal_pos: 0 } }),
  scale: () => ({ ...baseWidget(), type: "scale", props: { mode: "horizontal", range_min: 0, range_max: 100, tick_count: 21, major_tick_every: 5, label_show: true } }),
  buttonmatrix: () => ({ ...baseWidget(), type: "buttonmatrix", props: { map: ["Btn1", "Btn2", "\n", "Btn3", "Btn4"], one_checked: false } }),
  imagebutton: () => ({ ...baseWidget(), type: "imagebutton", props: { src_released: "", src_pressed: "", src_disabled: "" } }),
  msgbox: () => ({ ...baseWidget(), type: "msgbox", props: { title: "Alert", content_text: "Are you sure?", close_button: true, buttons: ["OK", "Cancel"] } }),
  tileview: () => ({ ...baseWidget(), type: "tileview", widgets: [], props: { active_col: 0, active_row: 0 } }),
  win: () => ({ ...baseWidget(), type: "win", widgets: [], props: { title: "Window", header_height: 40 } }),
  span: () => ({ ...baseWidget(), type: "span", props: { mode: "break", spans: [{ text: "Hello " }, { text: "world" }] } }),
  animimg: () => ({ ...baseWidget(), type: "animimg", props: { sources: [], duration: 500, repeat_count: -1 } }),
  component: () => ({ ...baseWidget(), type: "component", props: { component: "" } }),
};

export function createWidget(type: WidgetType): WidgetNode {
  const factory = widgetFactories[type];
  if (!factory) {
    throw new Error(`Unknown widget type: ${type}`);
  }
  return { ...factory(), id: `widget_${crypto.randomUUID().slice(0, 8)}` };
}
