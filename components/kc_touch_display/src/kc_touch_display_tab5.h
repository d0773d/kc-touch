#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t kc_touch_tab5_init_hw(void);
esp_err_t kc_touch_tab5_flush(int32_t x1, int32_t y1, int32_t x2, int32_t y2, const void *color_data);
bool kc_touch_tab5_touch_sample(uint16_t *x, uint16_t *y);
esp_err_t kc_touch_tab5_backlight_set(bool enable);

#ifdef __cplusplus
}
#endif
