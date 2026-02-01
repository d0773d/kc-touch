#pragma once

/* Fallback defaults for provisioning options hidden when native Wi-Fi is disabled */
#ifndef CONFIG_WIFI_PROV_SCAN_MAX_ENTRIES
#define CONFIG_WIFI_PROV_SCAN_MAX_ENTRIES 16
#endif

#ifndef CONFIG_WIFI_PROV_AUTOSTOP_TIMEOUT
#define CONFIG_WIFI_PROV_AUTOSTOP_TIMEOUT 120
#endif
