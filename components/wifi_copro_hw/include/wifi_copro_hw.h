#pragma once

#include "driver/gpio.h"
#include "driver/i2c_master.h"

/* I2C bus that drives the power/reset expander */
#define WIFI_COPRO_I2C_PORT I2C_NUM_0
#define WIFI_COPRO_I2C_SCL  GPIO_NUM_32
#define WIFI_COPRO_I2C_SDA  GPIO_NUM_31

/* PI4IO expander bit that controls the WLAN regulator */
#define WIFI_COPRO_POWER_BIT 0

/* Hard-wired SDIO/reset pins between host and coprocessor */
#define WIFI_COPRO_RESET_GPIO    GPIO_NUM_15
#define WIFI_COPRO_SDIO_CLK_GPIO GPIO_NUM_12
#define WIFI_COPRO_SDIO_CMD_GPIO GPIO_NUM_13
#define WIFI_COPRO_SDIO_D0_GPIO  GPIO_NUM_11
#define WIFI_COPRO_SDIO_D1_GPIO  GPIO_NUM_10
#define WIFI_COPRO_SDIO_D2_GPIO  GPIO_NUM_9
#define WIFI_COPRO_SDIO_D3_GPIO  GPIO_NUM_8

#define WIFI_COPRO_SDIO_CLOCK_KHZ 25000
#define WIFI_COPRO_SDIO_BUS_WIDTH 4
#define WIFI_COPRO_SDIO_TX_QUEUE  20
#define WIFI_COPRO_SDIO_RX_QUEUE  20
