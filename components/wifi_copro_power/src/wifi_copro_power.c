#include "wifi_copro_power.h"

#include <stddef.h>
#include "esp_bit_defs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PI4IO_ADDR            0x44
#define PI4IO_REG_CHIP_RESET  0x01
#define PI4IO_REG_IO_DIR      0x03
#define PI4IO_REG_OUT_SET     0x05
#define PI4IO_REG_OUT_H_IM    0x07
#define PI4IO_REG_IN_DEF_STA  0x09
#define PI4IO_REG_PULL_EN     0x0B
#define PI4IO_REG_PULL_SEL    0x0D
#define PI4IO_REG_INT_MASK    0x11

#define I2C_TIMEOUT_MS 100
#define I2C_GLITCH_FILTER 7

static const char *TAG = "wifi_copro_power";
static bool s_expander_ready;
static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_pi4io;

static esp_err_t ensure_i2c(void)
{
    if (s_pi4io) {
        return ESP_OK;
    }

    if (!s_i2c_bus) {
        i2c_master_bus_config_t bus_cfg = {
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .i2c_port = WIFI_COPRO_I2C_PORT,
            .scl_io_num = WIFI_COPRO_I2C_SCL,
            .sda_io_num = WIFI_COPRO_I2C_SDA,
            .glitch_ignore_cnt = I2C_GLITCH_FILTER,
            .flags.enable_internal_pullup = true,
        };
        esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
        if (err != ESP_OK) {
            return err;
        }
    }

    if (!s_pi4io) {
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = PI4IO_ADDR,
            .scl_speed_hz = 400000,
        };
        esp_err_t err = i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_pi4io);
        if (err != ESP_OK) {
            return err;
        }
    }

    return ESP_OK;
}

static esp_err_t pi4io_write(uint8_t reg, uint8_t value)
{
    uint8_t buffer[2] = {reg, value};
    return i2c_master_transmit(s_pi4io, buffer, sizeof(buffer), I2C_TIMEOUT_MS);
}

static esp_err_t pi4io_read(uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(s_pi4io, &reg, 1, value, 1, I2C_TIMEOUT_MS);
}

esp_err_t wifi_copro_power_init(void)
{
    if (s_expander_ready) {
        return ESP_OK;
    }

    esp_err_t err = ensure_i2c();
    if (err != ESP_OK) {
        return err;
    }

    err = pi4io_write(PI4IO_REG_CHIP_RESET, 0xFF);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    const struct {
        uint8_t reg;
        uint8_t value;
    } init_seq[] = {
        {PI4IO_REG_IO_DIR,     0b10111001},
        {PI4IO_REG_OUT_H_IM,   0b00000110},
        {PI4IO_REG_PULL_SEL,   0b10111001},
        {PI4IO_REG_PULL_EN,    0b11111001},
        {PI4IO_REG_IN_DEF_STA, 0b01000000},
        {PI4IO_REG_INT_MASK,   0b10111111},
        {PI4IO_REG_OUT_SET,    0b00001001},
    };

    for (size_t i = 0; i < sizeof(init_seq) / sizeof(init_seq[0]); ++i) {
        err = pi4io_write(init_seq[i].reg, init_seq[i].value);
        if (err != ESP_OK) {
            return err;
        }
    }

    s_expander_ready = true;
    ESP_LOGI(TAG, "PI4IO expander ready");
    return ESP_OK;
}

esp_err_t wifi_copro_power_set(bool enable)
{
    esp_err_t err = wifi_copro_power_init();
    if (err != ESP_OK) {
        return err;
    }

    uint8_t current = 0;
    err = pi4io_read(PI4IO_REG_OUT_SET, &current);
    if (err != ESP_OK) {
        return err;
    }

    if (enable) {
        current |= BIT(WIFI_COPRO_POWER_BIT);
    } else {
        current &= (uint8_t)~BIT(WIFI_COPRO_POWER_BIT);
    }

    err = pi4io_write(PI4IO_REG_OUT_SET, current);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "WLAN power %s", enable ? "enabled" : "disabled");
        if (enable) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
    return err;
}

esp_err_t wifi_copro_reset_slave(gpio_num_t reset_gpio)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << reset_gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        return err;
    }

    gpio_set_level(reset_gpio, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(reset_gpio, 1);
    vTaskDelay(pdMS_TO_TICKS(20));

    ESP_LOGI(TAG, "Reset Wi-Fi coprocessor on GPIO %d", reset_gpio);
    return ESP_OK;
}
