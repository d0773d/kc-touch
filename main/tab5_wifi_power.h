#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"

esp_err_t tab5_wifi_power_init(void);
esp_err_t tab5_wifi_power_set(bool enable);
esp_err_t tab5_wifi_reset_slave(gpio_num_t reset_gpio);
