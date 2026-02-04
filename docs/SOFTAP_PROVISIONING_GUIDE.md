# Wi-Fi Provisioning over SoftAP: Documentation & Implementation Guide

This guide explains how the current codebase handles Wi-Fi provisioning via SoftAP and provides a blueprint for implementing similar functionality in other ESP-IDF projects.

## 1. Overview

**Wi-Fi Provisioning** is the process of providing a new device with the network name (SSID) and password it needs to connect to a local Wi-Fi network.

**SoftAP Provisioning** works as follows:
1.  **Device acts as an Access Point:** The device creates its own Wi-Fi network (e.g., `PROV_XXXX`).
2.  **User Connects:** The user connects their phone/computer to this network.
3.  **Data Exchange:** The user sends the target Wi-Fi credentials (home router SSID/Password) via a mobile app or script.
4.  **Connection:** The device receives credentials, shuts down the SoftAP, and connects to the user's home network.

## 2. Configuration (`sdkconfig`)

To enable SoftAP provisioning and disable unnecessary Bluetooth stacks (as currently configured in this project), the following Kconfig options are required:

**File:** `sdkconfig.defaults` or `sdkconfig`

```kconfig
# Enable the Wi-Fi Provisioning Manager
CONFIG_ESP_WIFI_PROVISIONING=y

# Enable SoftAP Transport
CONFIG_EXAMPLE_PROV_TRANSPORT_SOFTAP=y

# Select Security Version (Security Level 2 is recommended)
CONFIG_EXAMPLE_PROV_SECURITY_VERSION_2=y

# Disable Bluetooth to save memory (since we are strictly using SoftAP)
CONFIG_BT_ENABLED=n
CONFIG_BT_NIMBLE_ENABLED=n
```

## 3. How the Code Operates (`app_main.c`)

The core logic resides in `app_main.c`. Below is the operational flow:

### Step A: Initialization

The application initializes the Non-Volatile Storage (NVS) to save credentials permanently, initializes the network stack (TCP/IP), and sets up the Wi-Fi driver.

```c
/* Initialize NVS */
esp_err_t ret = nvs_flash_init();
if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
}
ESP_ERROR_CHECK(ret);

/* Initialize TCP/IP and Wi-Fi */
ESP_ERROR_CHECK(esp_netif_init());
ESP_ERROR_CHECK(esp_event_loop_create_default());
```

### Step B: Provisioning Manager Setup

The project uses the `wifi_prov_mgr` component. It checks if the device is already provisioned.

```c
/* Configuration for the Provisioning Manager */
wifi_prov_mgr_config_t config = {
    /* Use the SoftAP scheme */
    .scheme = wifi_prov_scheme_softap, 
    .scheme_event_handler = WIFI_PROV_SCHEME_SOFTAP_EVENT_HANDLER
};

/* Initialize the manager */
ESP_ERROR_CHECK(wifi_prov_mgr_init(config));

/* Check if device is already provisioned */
bool provisioned = false;
ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));
```

### Step C: Starting Provisioning (SoftAP Mode)

If the device is **not** provisioned, it starts the SoftAP service. The device broadcasts a Wi-Fi SSID derived from its MAC address (e.g., `PROV_30EDA0`).

*   **Security:** Uses Proof-of-Possession (PoP). To connect/provision, the client needs the secret `abcd1234`.
*   **Username:** Fixed to `wifiprov`.
*   **Service Name:** The SSID of the SoftAP.

```c
if (!provisioned) {
    ESP_LOGI(TAG, "Starting provisioning");
    
    /* Security settings usually come from the factory partition in production */
    const char *service_name = "PROV_XXXXXX"; // Dynamically generated in code
    const char *pop = "abcd1234";             // Proof of Possession (Password)
    const char *username = "wifiprov";

    /* Start the provisioning service */
    wifi_prov_mgr_start_provisioning(
        security_ver,      // WIFI_PROV_SECURITY_1 or WIFI_PROV_SECURITY_2
        pop,               // The secret password needed by the phone app
        service_name,      // The SoftAP SSID
        username           // Username for handshake
    );
    
    /* Print QR Code for easy app connection */
    wifi_prov_print_qr(service_name, username, pop, "softap");
}
```

### Step D: Event Handling

The system defines an `event_handler` to react to state changes:

1.  **`WIFI_PROV_CRED_RECV`:** The device received SSID/Password from the phone.
2.  **`WIFI_PROV_CRED_SUCCESS`:** The device successfully used those credentials to connect to the router.
3.  **`WIFI_PROV_END`:** Logic to stop the provisioning manager and free resources.

### Step E: Main Execution

If the device **is** provisioned (or after successful provisioning), the manager de-initializes, and the app continues to standard Wi-Fi station operations.

```c
/* Wait for Wi-Fi connection */
xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_EVENT, true, true, portMAX_DELAY);

/* Provisioning is done, app logic starts here */
start_my_application();
```

## 4. Implementation Checklist for New Projects

To port this to a new project:

1.  **Dependencies:** Ensure your `idf_component.yml` includes:
    ```yaml
    dependencies:
      espressif/qrcode: "^0.1.0" # Optional, for printing QR code
    ```

2.  **Source Code:**
    *   Find the logic in `app_main.c`.
    *   Include headers: `#include <wifi_provisioning/manager.h>` and `#include <wifi_provisioning/scheme_softap.h>`.
    *   Copy the `event_handler` logic for `WIFI_PROV_EVENT`.

3.  **Mobile App:**
    *   Use the **Espressif ESP SoftAP Provisioning** app (iOS/Android).
    *   Or scan the QR code printed in the terminal during the provisioning phase.

## 5. Specific Hardware Note (ESP-Hosted)

**Note:** This specific `kc-touch-main` project uses **ESP-Hosted** (a specific driver where the Wi-Fi runs on a slave chip). 
*   However, the `wifi_provisioning` code acts at the **Application Layer**.
*   This means the exact same C code shown above works on a standard ESP32, ESP32-C3, or ESP32-S3 without the hosted/slave configuration.

## 6. Troubleshooting

*   **"Wi-Fi access-point not found"**: The credentials sent by the phone were incorrect, or the router is out of range.
*   **App fails to connect to SoftAP**: Ensure you are not forcing 5GHz if the chip only supports 2.4GHz.
*   **Controller Unresponsive (Error 19)**: This happens if `transport: ble` is selected but the Bluetooth stack is disabled. Ensure `wifi_prov_scheme_softap` is selected in code and config.
