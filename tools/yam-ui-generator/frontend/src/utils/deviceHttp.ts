/**
 * HTTP-based device communication for pushing YAML to ESP32 over WiFi.
 *
 * Talks to the yamui_loader_httpd HTTP server running on the device.
 */

export interface DeviceStatus {
  chip: string;
  free_heap: number;
  active_source: string;
  max_yaml_size: number;
}

export interface DeviceHttpOptions {
  onLog?: (message: string) => void;
}

export class DeviceHttp {
  private baseUrl: string;
  private options: DeviceHttpOptions;

  constructor(deviceUrl: string, options: DeviceHttpOptions = {}) {
    this.baseUrl = deviceUrl.replace(/\/+$/, "");
    this.options = options;
  }

  private log(msg: string) {
    this.options.onLog?.(msg);
  }

  /**
   * Push YAML to the device via HTTP POST.
   */
  async sendYaml(yaml: string): Promise<boolean> {
    try {
      this.log(`Uploading ${yaml.length} bytes to ${this.baseUrl}/api/yaml`);
      const response = await fetch(`${this.baseUrl}/api/yaml`, {
        method: "POST",
        headers: { "Content-Type": "text/yaml" },
        body: yaml,
      });

      if (!response.ok) {
        const text = await response.text();
        this.log(`Upload failed (HTTP ${response.status}): ${text}`);
        return false;
      }

      const result = await response.json();
      this.log(`Upload successful: ${result.message ?? "YAML applied"}`);
      return true;
    } catch (err) {
      this.log(`Upload error: ${err}`);
      return false;
    }
  }

  /**
   * Fetch the currently active YAML from the device.
   */
  async getYaml(): Promise<string | null> {
    try {
      const response = await fetch(`${this.baseUrl}/api/yaml`);
      if (!response.ok) return null;
      const contentType = response.headers.get("content-type") ?? "";
      if (contentType.includes("yaml")) {
        return await response.text();
      }
      // JSON response means no custom YAML on device
      return null;
    } catch (err) {
      this.log(`Fetch error: ${err}`);
      return null;
    }
  }

  /**
   * Get device status information.
   */
  async getStatus(): Promise<DeviceStatus | null> {
    try {
      const response = await fetch(`${this.baseUrl}/api/status`);
      if (!response.ok) return null;
      return await response.json();
    } catch (err) {
      this.log(`Status error: ${err}`);
      return null;
    }
  }

  /**
   * Delete custom YAML from device, reverting to embedded schema.
   */
  async resetToEmbedded(): Promise<boolean> {
    try {
      const response = await fetch(`${this.baseUrl}/api/yaml`, {
        method: "DELETE",
      });
      if (!response.ok) return false;
      this.log("Device reverted to embedded schema");
      return true;
    } catch (err) {
      this.log(`Reset error: ${err}`);
      return false;
    }
  }

  /**
   * Test connectivity to the device.
   */
  async ping(): Promise<boolean> {
    try {
      const status = await this.getStatus();
      return status !== null;
    } catch {
      return false;
    }
  }
}
