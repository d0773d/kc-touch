#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t lvgl_yaml_gui_load_default(void);
esp_err_t lvgl_yaml_gui_load_named(const char *schema_name);
esp_err_t lvgl_yaml_gui_load_from_file(const char *path);

#ifdef __cplusplus
}
#endif
