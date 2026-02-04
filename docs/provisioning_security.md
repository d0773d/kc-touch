# ESP32 Wi-Fi Provisioning Security Documentation

## Overview

This document explains the security mechanisms used in the ESP32 Wi-Fi provisioning process, specifically regarding the transmission of sensitive data like Wi-Fi credentials.

## Security Scheme: WIFI_PROV_SECURITY_1

Our application uses `WIFI_PROV_SECURITY_1`, which provides a secure channel for data exchange even when the underlying transport layer (SoftAP) is open and unencrypted.

### 1. Key Exchange (X25519)
The provisioning process begins with a secure handshake between the provisioning app (client) and the ESP32 device. They use the **Curve25519** algorithm to perform a key exchange. This results in a shared **Session Key** that is known only to the client and the device, without ever transmitting the key itself over the air.

### 2. Proof of Possession (PoP)
To ensure the client is authorized to configure the device, a **Proof of Possession (PoP)** mechanism is used.
- The device is configured with a secret `service_key` (e.g., "password").
- The client must demonstrate knowledge of this key during the handshake.
- This prevents unauthorized users from connecting to the provisioning service, as the handshake will fail without the correct PoP.

### 3. Data Encryption (AES-256-CTR)
Once the secure session is established and the PoP is verified:
- All subsequent data, including the target Wi-Fi SSID and Password, is encrypted using **AES-256-CTR**.
- The shared Session Key derived from the handshake is used for this encryption.
- This ensures that even if the raw SoftAP packets are intercepted, the Wi-Fi credentials remain unreadable.

## Setup in Code

In `app_main.c`, the security configuration is applied as follows:

```c
const char *service_key = "password"; 

/* 
 * WIFI_PROV_SECURITY_1: Enables the secure handshake and encryption.
 * service_key: Passed as the 'pop' argument to verify the client.
 */
esp_err_t err = wifi_prov_mgr_start_provisioning(
    WIFI_PROV_SECURITY_1, 
    (const char *) service_key, 
    service_name, 
    NULL
);
```

## QR Code Payload

The QR code provides the mobile app with the necessary discovery and security parameters:

```json
{
    "ver": "v1",
    "name": "PROV_DEVICE",
    "pop": "password",
    "transport": "softap"
}
```

- **ver**: Protocol version.
- **name**: The Wi-Fi SSID (SoftAP name) to connect to.
- **pop**: The Proof of Possession string the app must use.
- **transport**: The transport method (SoftAP).
