#include "sensor_manager.h"

#include <stdbool.h>

#include "esp_log.h"

static const char *TAG = "sensor_mgr";

static sensor_record_t s_sensors[] = {
    {.kind = SENSOR_KIND_TEMPERATURE, .name = "Ambient Temp", .id = "tmp0", .unit = "°C", .value = 23.5f, .min = -20.0f, .max = 80.0f},
    {.kind = SENSOR_KIND_TEMPERATURE, .name = "Water Temp", .id = "tmp1", .unit = "°C", .value = 19.0f, .min = -5.0f, .max = 60.0f},
    {.kind = SENSOR_KIND_HUMIDITY, .name = "Humidity", .id = "hum0", .unit = "%", .value = 48.0f, .min = 0.0f, .max = 100.0f},
    {.kind = SENSOR_KIND_PRESSURE, .name = "Barometer", .id = "bar0", .unit = "kPa", .value = 101.3f, .min = 90.0f, .max = 110.0f},
    {.kind = SENSOR_KIND_LIGHT, .name = "Lux Sensor", .id = "lux0", .unit = "lx", .value = 320.0f, .min = 0.0f, .max = 1000.0f},
};

static bool s_initialized;

esp_err_t sensor_manager_init(void)
{
    s_initialized = true;
    ESP_LOGI(TAG, "Sensor manager initialized with %u simulated sensors", (unsigned)(sizeof(s_sensors) / sizeof(s_sensors[0])));
    return ESP_OK;
}

const sensor_record_t *sensor_manager_get_snapshot(size_t *out_count)
{
    if (out_count) {
        *out_count = sizeof(s_sensors) / sizeof(s_sensors[0]);
    }
    return s_initialized ? s_sensors : NULL;
}

static float sensor_wrap_value(float value, float min, float max)
{
    if (value > max) {
        return min + (value - max);
    }
    if (value < min) {
        return max - (min - value);
    }
    return value;
}

void sensor_manager_tick(void)
{
    if (!s_initialized) {
        return;
    }
    for (size_t i = 0; i < sizeof(s_sensors) / sizeof(s_sensors[0]); ++i) {
        float delta = 0.25f + 0.1f * (float)i;
        if ((i & 1U) != 0U) {
            delta = -delta;
        }
        s_sensors[i].value = sensor_wrap_value(s_sensors[i].value + delta, s_sensors[i].min, s_sensors[i].max);
    }
}

const char *sensor_manager_kind_name(sensor_kind_t kind)
{
    switch (kind) {
        case SENSOR_KIND_TEMPERATURE:
            return "temperature";
        case SENSOR_KIND_HUMIDITY:
            return "humidity";
        case SENSOR_KIND_PRESSURE:
            return "pressure";
        case SENSOR_KIND_LIGHT:
            return "light";
        default:
            return "unknown";
    }
}
