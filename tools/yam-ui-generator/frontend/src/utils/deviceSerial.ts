/**
 * WebSerial-based device communication for pushing YAML to ESP32 via USB/UART.
 *
 * Protocol: [YAML (4 bytes)] [LENGTH (4 bytes LE)] [PAYLOAD] [CRC32 (4 bytes LE)]
 * Response: [YACK (4 bytes)] on success, [YNAK (4 bytes)] on failure
 */

const START_MARKER = new TextEncoder().encode("YAML");
const ACK_MARKER = "YACK";
const NAK_MARKER = "YNAK";

/** CRC32 lookup table (standard polynomial 0xEDB88320). */
const CRC32_TABLE = (() => {
  const table = new Uint32Array(256);
  for (let i = 0; i < 256; i++) {
    let crc = i;
    for (let j = 0; j < 8; j++) {
      crc = crc & 1 ? (crc >>> 1) ^ 0xedb88320 : crc >>> 1;
    }
    table[i] = crc;
  }
  return table;
})();

function crc32(data: Uint8Array): number {
  let crc = 0xffffffff;
  for (let i = 0; i < data.length; i++) {
    crc = (crc >>> 8) ^ CRC32_TABLE[(crc ^ data[i]) & 0xff];
  }
  return (crc ^ 0xffffffff) >>> 0;
}

function uint32LE(value: number): Uint8Array {
  const buf = new Uint8Array(4);
  buf[0] = value & 0xff;
  buf[1] = (value >>> 8) & 0xff;
  buf[2] = (value >>> 16) & 0xff;
  buf[3] = (value >>> 24) & 0xff;
  return buf;
}

export type ConnectionState = "disconnected" | "connecting" | "connected" | "uploading";

export interface DeviceSerialOptions {
  baudRate?: number;
  onStateChange?: (state: ConnectionState) => void;
  onLog?: (message: string) => void;
}

export class DeviceSerial {
  private port: SerialPort | null = null;
  private reader: ReadableStreamDefaultReader<Uint8Array> | null = null;
  private state: ConnectionState = "disconnected";
  private options: DeviceSerialOptions;

  constructor(options: DeviceSerialOptions = {}) {
    this.options = { baudRate: 115200, ...options };
  }

  get connectionState(): ConnectionState {
    return this.state;
  }

  get isSupported(): boolean {
    return "serial" in navigator;
  }

  private setState(state: ConnectionState) {
    this.state = state;
    this.options.onStateChange?.(state);
  }

  private log(msg: string) {
    this.options.onLog?.(msg);
  }

  async connect(): Promise<boolean> {
    if (!this.isSupported) {
      this.log("WebSerial API not supported in this browser");
      return false;
    }

    try {
      this.setState("connecting");
      this.port = await navigator.serial.requestPort();
      await this.port.open({ baudRate: this.options.baudRate ?? 115200 });
      this.setState("connected");
      this.log(`Connected at ${this.options.baudRate} baud`);
      return true;
    } catch (err) {
      this.log(`Connection failed: ${err}`);
      this.setState("disconnected");
      return false;
    }
  }

  async disconnect(): Promise<void> {
    try {
      if (this.reader) {
        await this.reader.cancel();
        this.reader = null;
      }
      if (this.port) {
        await this.port.close();
        this.port = null;
      }
    } catch {
      // Ignore close errors
    }
    this.setState("disconnected");
    this.log("Disconnected");
  }

  /**
   * Send a framed YAML payload to the device over serial.
   * Returns true on ACK, false on NAK or error.
   */
  async sendYaml(yaml: string): Promise<boolean> {
    if (!this.port || this.state !== "connected") {
      this.log("Not connected");
      return false;
    }

    this.setState("uploading");
    const payload = new TextEncoder().encode(yaml);
    const checksum = crc32(payload);

    // Build the frame
    const frame = new Uint8Array(
      START_MARKER.length + 4 + payload.length + 4
    );
    let offset = 0;

    // Start marker
    frame.set(START_MARKER, offset);
    offset += START_MARKER.length;

    // Length (little-endian)
    frame.set(uint32LE(payload.length), offset);
    offset += 4;

    // Payload
    frame.set(payload, offset);
    offset += payload.length;

    // CRC32 (little-endian)
    frame.set(uint32LE(checksum), offset);

    try {
      // Write the frame
      const writer = this.port.writable?.getWriter();
      if (!writer) {
        this.log("Port not writable");
        this.setState("connected");
        return false;
      }

      this.log(`Sending ${payload.length} bytes...`);
      await writer.write(frame);
      writer.releaseLock();

      // Wait for response (4 bytes: YACK or YNAK)
      const response = await this.readResponse(5000);
      this.setState("connected");

      if (response === ACK_MARKER) {
        this.log("Device acknowledged - YAML applied successfully");
        return true;
      } else if (response === NAK_MARKER) {
        this.log("Device rejected the YAML (parse error or CRC mismatch)");
        return false;
      } else {
        this.log(`Unexpected response: ${response ?? "timeout"}`);
        return false;
      }
    } catch (err) {
      this.log(`Send failed: ${err}`);
      this.setState("connected");
      return false;
    }
  }

  private async readResponse(timeoutMs: number): Promise<string | null> {
    if (!this.port?.readable) {
      return null;
    }

    this.reader = this.port.readable.getReader();
    const collected: number[] = [];

    try {
      const deadline = Date.now() + timeoutMs;

      while (Date.now() < deadline && collected.length < 4) {
        const remaining = deadline - Date.now();
        if (remaining <= 0) break;

        const result = await Promise.race([
          this.reader.read(),
          new Promise<{ done: true; value: undefined }>((resolve) =>
            setTimeout(() => resolve({ done: true, value: undefined }), remaining)
          ),
        ]);

        if (result.done || !result.value) break;

        for (const byte of result.value) {
          collected.push(byte);
          if (collected.length >= 4) break;
        }
      }

      if (collected.length >= 4) {
        return new TextDecoder().decode(new Uint8Array(collected.slice(0, 4)));
      }
      return null;
    } finally {
      this.reader.releaseLock();
      this.reader = null;
    }
  }
}
