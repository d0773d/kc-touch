export type TelemetryChannel = "assets" | "preview" | "issues" | "styles";

export interface TelemetryEventDetail {
  channel: TelemetryChannel;
  event: string;
  data: Record<string, unknown>;
  timestamp: string;
}

export function emitTelemetry(channel: TelemetryChannel, event: string, data?: Record<string, unknown>): void {
  if (typeof window === "undefined" || typeof window.dispatchEvent !== "function") {
    return;
  }
  const detail: TelemetryEventDetail = {
    channel,
    event,
    data: data ?? {},
    timestamp: new Date().toISOString(),
  };
  window.dispatchEvent(new CustomEvent("yamui-telemetry", { detail }));
}
