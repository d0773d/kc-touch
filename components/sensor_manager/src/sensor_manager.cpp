#include "sensor_manager.h"

#include "sdkconfig.h"

#include <M5Unified.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SENSOR_I2C_ADDR_MIN        0x01
#define SENSOR_I2C_ADDR_MAX        0x7F
#define SENSOR_CMD_TIMEOUT_MS      100
#define SENSOR_INFO_DELAY_MS       300
#define SENSOR_READ_DELAY_MS       400
#define SENSOR_MAX_RESPONSE_LEN    64

typedef struct {
    const char *type;
    const char *unit;
    float min;
    float max;
} sensor_defaults_t;

static const sensor_defaults_t SENSOR_DEFAULTS[] = {
    {"ph",  "pH",     0.0f,   14.0f},
    {"ec",  "uS/cm",  0.0f, 5000.0f},
    {"rtd", "C",    -50.0f,  200.0f},
    {"do",  "mg/L",   0.0f,   20.0f},
    {"orp", "mV",  -500.0f,  500.0f},
    {"co2", "ppm",    0.0f, 5000.0f},
    {"o2",  "%",      0.0f,  100.0f},
};

typedef struct {
    sensor_record_t *items;
    size_t count;
} sensor_list_t;

static const char *TAG = "sensor_mgr";
static sensor_list_t s_sensors = {0};
static bool s_initialized;
static i2c_port_t s_i2c_port = I2C_NUM_0;

static void sensor_list_clear(void);
static sensor_record_t *sensor_list_append(void);

#ifdef CONFIG_SENSOR_MANAGER_ENABLE_FAKE_DATA
typedef struct {
    const char *type;
    const char *name;
    const char *unit;
    const char *firmware;
    float value;
    float min;
    float max;
} fake_sensor_def_t;

static const fake_sensor_def_t FAKE_SENSORS[] = {
    {"ph",  "Simulated pH",        "pH",    "1.98", 7.20f,  0.0f, 14.0f},
    {"ec",  "Simulated EC",        "uS/cm", "1.10", 800.0f, 0.0f, 5000.0f},
    {"rtd", "Simulated RTD",       "C",     "2.05", 23.5f, -50.0f, 200.0f},
    {"do",  "Simulated O2",        "mg/L",  "0.90", 6.80f,  0.0f, 20.0f},
};

static void sensor_populate_fake(void)
{
    sensor_list_clear();

    for (size_t i = 0; i < sizeof(FAKE_SENSORS) / sizeof(FAKE_SENSORS[0]); ++i) {
        sensor_record_t *record = sensor_list_append();
        if (!record) {
            return;
        }
        const fake_sensor_def_t &def = FAKE_SENSORS[i];
        strncpy(record->type, def.type, sizeof(record->type));
        record->type[sizeof(record->type) - 1] = '\0';
        snprintf(record->name, sizeof(record->name), "%s", def.name);
        snprintf(record->id, sizeof(record->id), "sim-%s-%u", def.type, (unsigned)i);
        strncpy(record->unit, def.unit, sizeof(record->unit));
        record->unit[sizeof(record->unit) - 1] = '\0';
        strncpy(record->firmware, def.firmware, sizeof(record->firmware));
        record->firmware[sizeof(record->firmware) - 1] = '\0';
        record->value = def.value;
        record->min = def.min;
        record->max = def.max;
        record->address = 0;
    }
    ESP_LOGI(TAG, "Loaded %u simulated sensor(s) for development", (unsigned)s_sensors.count);
}

static bool sensor_is_fake(const sensor_record_t *record)
{
    return record && record->address == 0;
}

static void sensor_tick_fake(sensor_record_t *record)
{
    if (!record) {
        return;
    }
    const float span = (record->max > record->min) ? (record->max - record->min) : 1.0f;
    const float step = span * 0.01f;
    float next = record->value + step;
    if (next > record->max) {
        next = record->min;
    }
    record->value = next;
}
#else
static inline void sensor_populate_fake(void) {}
static inline bool sensor_is_fake(const sensor_record_t *) { return false; }
static inline void sensor_tick_fake(sensor_record_t *) {}
#endif

static inline TickType_t sensor_timeout_ticks(void)
{
    return pdMS_TO_TICKS(SENSOR_CMD_TIMEOUT_MS);
}

static void sensor_list_clear(void)
{
    free(s_sensors.items);
    s_sensors.items = nullptr;
    s_sensors.count = 0;
}

static sensor_record_t *sensor_list_append(void)
{
    sensor_record_t *next = static_cast<sensor_record_t *>(realloc(s_sensors.items,
        sizeof(sensor_record_t) * (s_sensors.count + 1)));
    if (!next) {
        ESP_LOGE(TAG, "Out of memory while tracking sensors");
        return nullptr;
    }
    s_sensors.items = next;
    sensor_record_t *record = &s_sensors.items[s_sensors.count++];
    memset(record, 0, sizeof(*record));
    return record;
}

static bool type_equals(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower(static_cast<unsigned char>(*a)) != tolower(static_cast<unsigned char>(*b))) {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static const sensor_defaults_t *find_defaults(const char *type)
{
    for (const auto &entry : SENSOR_DEFAULTS) {
        if (type_equals(entry.type, type)) {
            return &entry;
        }
    }
    return nullptr;
}

static void apply_defaults(sensor_record_t *record, const char *type)
{
    const sensor_defaults_t *defaults = find_defaults(type);
    if (defaults) {
        strncpy(record->unit, defaults->unit, sizeof(record->unit));
        record->unit[sizeof(record->unit) - 1] = '\0';
        record->min = defaults->min;
        record->max = defaults->max;
    } else {
        record->unit[0] = '\0';
        record->min = 0.0f;
        record->max = 0.0f;
    }
}

static esp_err_t ezo_send_command(uint8_t address, char command)
{
    uint8_t payload = static_cast<uint8_t>(command);
    return i2c_master_write_to_device(s_i2c_port, address, &payload, 1, sensor_timeout_ticks());
}

static esp_err_t ezo_read_response(uint8_t address, char *buffer, size_t length)
{
    if (!buffer || length < 2) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(buffer, 0, length);
    esp_err_t err = i2c_master_read_from_device(s_i2c_port,
                                                address,
                                                reinterpret_cast<uint8_t *>(buffer),
                                                length - 1,
                                                sensor_timeout_ticks());
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "I2C read failed for 0x%02X (%s)", address, esp_err_to_name(err));
        return err;
    }
    buffer[length - 1] = '\0';
    return ESP_OK;
}

static bool ezo_parse_info(const char *response, char *out_type, size_t type_len, char *out_fw, size_t fw_len)
{
    if (!response || response[0] != '1') {
        return false;
    }
    const char *payload = strchr(response, ' ');
    if (!payload) {
        return false;
    }
    payload++;
    if (strncmp(payload, "?i,", 3) != 0) {
        return false;
    }
    const char *type_start = payload + 3;
    const char *type_end = strchr(type_start, ',');
    if (!type_end) {
        return false;
    }
    size_t copy_len = (size_t)(type_end - type_start);
    if (copy_len >= type_len) {
        copy_len = type_len - 1;
    }
    memcpy(out_type, type_start, copy_len);
    out_type[copy_len] = '\0';

    const char *fw_start = type_end + 1;
    const char *fw_end = fw_start;
    while (*fw_end && *fw_end != ' ' && *fw_end != '\r' && *fw_end != '\n') {
        ++fw_end;
    }
    size_t fw_copy = (size_t)(fw_end - fw_start);
    if (fw_copy >= fw_len) {
        fw_copy = fw_len - 1;
    }
    memcpy(out_fw, fw_start, fw_copy);
    out_fw[fw_copy] = '\0';
    return true;
}

static void normalize_type(const char *src, char *dst, size_t dst_len)
{
    if (!src || !dst || dst_len == 0) {
        return;
    }
    size_t i = 0;
    for (; src[i] && i < dst_len - 1; ++i) {
        dst[i] = (char)tolower(static_cast<unsigned char>(src[i]));
    }
    dst[i] = '\0';
}

static esp_err_t scan_bus(void)
{
    sensor_list_clear();
    size_t discovered = 0;

    for (uint8_t address = SENSOR_I2C_ADDR_MIN; address < SENSOR_I2C_ADDR_MAX; ++address) {
        if (ezo_send_command(address, 'i') != ESP_OK) {
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(SENSOR_INFO_DELAY_MS));

        char response[SENSOR_MAX_RESPONSE_LEN];
        if (ezo_read_response(address, response, sizeof(response)) != ESP_OK) {
            continue;
        }

        char type_raw[16] = {0};
        char firmware[16] = {0};
        if (!ezo_parse_info(response, type_raw, sizeof(type_raw), firmware, sizeof(firmware))) {
            ESP_LOGW(TAG, "Unrecognized info response from 0x%02X: %s", address, response);
            continue;
        }

        sensor_record_t *record = sensor_list_append();
        if (!record) {
            return ESP_ERR_NO_MEM;
        }

        normalize_type(type_raw, record->type, sizeof(record->type));
        snprintf(record->name, sizeof(record->name), "EZO-%s", type_raw);
        snprintf(record->id, sizeof(record->id), "0x%02X", address);
        strncpy(record->firmware, firmware, sizeof(record->firmware));
        record->firmware[sizeof(record->firmware) - 1] = '\0';
        record->address = address;
        apply_defaults(record, record->type);
        discovered++;
    }

    if (discovered == 0) {
        ESP_LOGW(TAG, "No EZO sensors discovered on I2C%d", (int)s_i2c_port);
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "Discovered %u sensor(s)", (unsigned)discovered);
    return ESP_OK;
}

static esp_err_t ezo_read_value(uint8_t address, float *out_value)
{
    if (!out_value) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = ezo_send_command(address, 'R');
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(SENSOR_READ_DELAY_MS));

    char response[SENSOR_MAX_RESPONSE_LEN];
    err = ezo_read_response(address, response, sizeof(response));
    if (err != ESP_OK) {
        return err;
    }
    if (response[0] != '1') {
        ESP_LOGW(TAG, "Sensor 0x%02X reported error: %s", address, response);
        return ESP_FAIL;
    }
    const char *value_str = response + 1;
    while (*value_str == ' ') {
        ++value_str;
    }
    char *end_ptr = nullptr;
    float value = strtof(value_str, &end_ptr);
    if (end_ptr == value_str) {
        ESP_LOGW(TAG, "Invalid reading from 0x%02X: %s", address, response);
        return ESP_ERR_INVALID_STATE;
    }
    *out_value = value;
    return ESP_OK;
}

esp_err_t sensor_manager_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
#ifdef CONFIG_SENSOR_MANAGER_FORCE_FAKE_ONLY
    s_initialized = true;
    sensor_populate_fake();
    return sensor_manager_update();
#endif
    if (!M5.In_I2C.isEnabled()) {
        ESP_LOGE(TAG, "Internal I2C bus is not initialized by M5Unified");
        return ESP_ERR_INVALID_STATE;
    }

    s_i2c_port = M5.In_I2C.getPort();
    s_initialized = true;
    sensor_list_clear();

    esp_err_t err = scan_bus();
#ifdef CONFIG_SENSOR_MANAGER_ENABLE_FAKE_DATA
    if (err == ESP_ERR_NOT_FOUND) {
        sensor_populate_fake();
        err = ESP_OK;
    }
#endif
    if (err != ESP_OK) {
        return err;
    }
    return sensor_manager_update();
}

const sensor_record_t *sensor_manager_get_snapshot(size_t *out_count)
{
    if (out_count) {
        *out_count = s_sensors.count;
    }
    return (s_initialized && s_sensors.count > 0) ? s_sensors.items : nullptr;
}

esp_err_t sensor_manager_update(void)
{
#ifdef CONFIG_SENSOR_MANAGER_FORCE_FAKE_ONLY
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    for (size_t i = 0; i < s_sensors.count; ++i) {
        sensor_tick_fake(&s_sensors.items[i]);
    }
    return ESP_OK;
#endif
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_sensors.count == 0) {
        esp_err_t scan_err = scan_bus();
#ifdef CONFIG_SENSOR_MANAGER_ENABLE_FAKE_DATA
        if (scan_err == ESP_ERR_NOT_FOUND) {
            sensor_populate_fake();
            scan_err = ESP_OK;
        }
#endif
        if (scan_err != ESP_OK) {
            return scan_err;
        }
    }

    esp_err_t last_err = ESP_OK;
    for (size_t i = 0; i < s_sensors.count; ++i) {
        if (sensor_is_fake(&s_sensors.items[i])) {
            sensor_tick_fake(&s_sensors.items[i]);
            continue;
        }
        float value = 0.0f;
        esp_err_t err = ezo_read_value(s_sensors.items[i].address, &value);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to update sensor %s (addr 0x%02X): %s",
                     s_sensors.items[i].name,
                     s_sensors.items[i].address,
                     esp_err_to_name(err));
            last_err = err;
            continue;
        }
        s_sensors.items[i].value = value;
    }
    return last_err;
}
