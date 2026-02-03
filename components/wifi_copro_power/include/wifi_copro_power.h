#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "wifi_copro_hw.h"

esp_err_t wifi_copro_power_init(void);
esp_err_t wifi_copro_power_set(bool enable);
esp_err_t wifi_copro_reset_slave(gpio_num_t reset_gpio);
