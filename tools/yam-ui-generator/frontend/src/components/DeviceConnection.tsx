import { useCallback, useMemo, useRef, useState } from "react";
import { useProject } from "../context/ProjectContext";
import { DeviceSerial, ConnectionState } from "../utils/deviceSerial";
import { DeviceHttp, DeviceStatus } from "../utils/deviceHttp";
import { dump as yamlDump } from "js-yaml";

type ConnectionMode = "serial" | "http";

export default function DeviceConnection(): JSX.Element {
  const { project } = useProject();
  const [mode, setMode] = useState<ConnectionMode>("serial");
  const [serialState, setSerialState] = useState<ConnectionState>("disconnected");
  const [httpUrl, setHttpUrl] = useState("http://yamui.local");
  const [httpStatus, setHttpStatus] = useState<DeviceStatus | null>(null);
  const [logs, setLogs] = useState<string[]>([]);
  const [uploading, setUploading] = useState(false);

  const serialRef = useRef<DeviceSerial | null>(null);

  const addLog = useCallback((msg: string) => {
    const timestamp = new Date().toLocaleTimeString();
    setLogs((prev) => [...prev.slice(-49), `[${timestamp}] ${msg}`]);
  }, []);

  const serial = useMemo(() => {
    if (!serialRef.current) {
      serialRef.current = new DeviceSerial({
        baudRate: 115200,
        onStateChange: setSerialState,
        onLog: addLog,
      });
    }
    return serialRef.current;
  }, [addLog]);

  const httpClient = useMemo(
    () => new DeviceHttp(httpUrl, { onLog: addLog }),
    [httpUrl, addLog]
  );

  const projectYaml = useMemo(() => {
    try {
      return yamlDump(project, { lineWidth: -1, noRefs: true });
    } catch {
      return "";
    }
  }, [project]);

  const handleSerialConnect = async () => {
    if (serialState === "connected") {
      await serial.disconnect();
    } else {
      await serial.connect();
    }
  };

  const handleSerialUpload = async () => {
    if (!projectYaml) {
      addLog("No YAML to send");
      return;
    }
    setUploading(true);
    await serial.sendYaml(projectYaml);
    setUploading(false);
  };

  const handleHttpPing = async () => {
    addLog(`Testing connection to ${httpUrl}...`);
    const status = await httpClient.getStatus();
    setHttpStatus(status);
    if (status) {
      addLog(`Connected: ${status.chip}, source=${status.active_source}, heap=${status.free_heap}`);
    } else {
      addLog("Device not reachable");
    }
  };

  const handleHttpUpload = async () => {
    if (!projectYaml) {
      addLog("No YAML to send");
      return;
    }
    setUploading(true);
    await httpClient.sendYaml(projectYaml);
    setUploading(false);
  };

  const handleHttpReset = async () => {
    setUploading(true);
    await httpClient.resetToEmbedded();
    setUploading(false);
  };

  const webSerialSupported = typeof navigator !== "undefined" && "serial" in navigator;

  return (
    <section>
      <p className="section-title">Device Connection</p>

      {/* Mode selector */}
      <div style={{ display: "flex", gap: 4, marginBottom: 8 }}>
        <button
          className={`btn btn-sm ${mode === "serial" ? "btn-primary" : ""}`}
          onClick={() => setMode("serial")}
        >
          USB/Serial
        </button>
        <button
          className={`btn btn-sm ${mode === "http" ? "btn-primary" : ""}`}
          onClick={() => setMode("http")}
        >
          WiFi/HTTP
        </button>
      </div>

      {/* Serial mode */}
      {mode === "serial" && (
        <div>
          {!webSerialSupported ? (
            <p style={{ color: "#ef4444", fontSize: "0.85rem" }}>
              WebSerial not supported. Use Chrome/Edge.
            </p>
          ) : (
            <>
              <div style={{ display: "flex", gap: 4, marginBottom: 8 }}>
                <button
                  className="btn btn-sm"
                  onClick={handleSerialConnect}
                  disabled={serialState === "connecting" || serialState === "uploading"}
                >
                  {serialState === "connected" ? "Disconnect" : "Connect"}
                </button>
                <button
                  className="btn btn-sm btn-primary"
                  onClick={handleSerialUpload}
                  disabled={serialState !== "connected" || uploading}
                >
                  {uploading ? "Uploading..." : "Push YAML"}
                </button>
              </div>
              <p style={{ fontSize: "0.8rem", color: "#6b7280" }}>
                Status: <strong>{serialState}</strong>
              </p>
            </>
          )}
        </div>
      )}

      {/* HTTP mode */}
      {mode === "http" && (
        <div>
          <input
            className="input-field"
            placeholder="Device URL"
            value={httpUrl}
            onChange={(e) => setHttpUrl(e.target.value)}
            style={{ marginBottom: 8 }}
          />
          <div style={{ display: "flex", gap: 4, marginBottom: 8 }}>
            <button className="btn btn-sm" onClick={handleHttpPing} disabled={uploading}>
              Ping
            </button>
            <button
              className="btn btn-sm btn-primary"
              onClick={handleHttpUpload}
              disabled={uploading}
            >
              {uploading ? "Uploading..." : "Push YAML"}
            </button>
            <button
              className="btn btn-sm"
              onClick={handleHttpReset}
              disabled={uploading}
            >
              Reset
            </button>
          </div>
          {httpStatus && (
            <p style={{ fontSize: "0.8rem", color: "#6b7280" }}>
              {httpStatus.chip} | Source: {httpStatus.active_source} | Heap: {httpStatus.free_heap}
            </p>
          )}
        </div>
      )}

      {/* Log output */}
      {logs.length > 0 && (
        <div
          style={{
            marginTop: 8,
            maxHeight: 120,
            overflowY: "auto",
            fontSize: "0.75rem",
            fontFamily: "monospace",
            background: "#1e1e1e",
            color: "#d4d4d4",
            padding: 6,
            borderRadius: 4,
          }}
        >
          {logs.map((log, i) => (
            <div key={i}>{log}</div>
          ))}
        </div>
      )}
    </section>
  );
}
