#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char type[16];
    char name[32];
    char id[16];
    char unit[8];
    char firmware[16];
    float value;
    float min;
    float max;
    uint8_t address;
} sensor_record_t;

esp_err_t sensor_manager_init(void);
const sensor_record_t *sensor_manager_get_snapshot(size_t *out_count);
esp_err_t sensor_manager_update(void);

#ifdef __cplusplus
}
#endif
