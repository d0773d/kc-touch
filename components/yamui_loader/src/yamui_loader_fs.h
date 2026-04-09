#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t yamui_fs_init(const char *partition_label, const char *mount_point);
void yamui_fs_deinit(const char *partition_label);
bool yamui_fs_exists(const char *path);
esp_err_t yamui_fs_save(const char *path, const char *data, size_t length);
esp_err_t yamui_fs_load(const char *path, char **out_data, size_t *out_length);
esp_err_t yamui_fs_delete(const char *path);

#ifdef __cplusplus
}
#endif
