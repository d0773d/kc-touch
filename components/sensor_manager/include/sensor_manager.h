#pragma once

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SENSOR_KIND_TEMPERATURE = 0,
    SENSOR_KIND_HUMIDITY,
    SENSOR_KIND_PRESSURE,
    SENSOR_KIND_LIGHT,
    SENSOR_KIND_UNKNOWN,
} sensor_kind_t;

typedef struct {
    sensor_kind_t kind;
    char name[32];
    char id[16];
    char unit[8];
    float value;
    float min;
    float max;
} sensor_record_t;

esp_err_t sensor_manager_init(void);
const sensor_record_t *sensor_manager_get_snapshot(size_t *out_count);
const char *sensor_manager_kind_name(sensor_kind_t kind);
void sensor_manager_tick(void);

#ifdef __cplusplus
}
#endif
