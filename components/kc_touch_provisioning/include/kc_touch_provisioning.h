#pragma once

#include "esp_err.h"
#include "wifi_provisioning/manager.h"

#define KC_TOUCH_PROV_SERVICE_NAME_MAX 12

void kc_touch_prov_init_manager_config(wifi_prov_mgr_config_t *config);
void kc_touch_prov_generate_service_name(char *service_name, size_t max);
esp_err_t kc_touch_prov_start_security1(const char *service_name, const char *pop, const char *transport);
