#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t yamui_async_reset(const char *operation, const char *message);
esp_err_t yamui_async_begin(const char *operation, const char *message);
esp_err_t yamui_async_progress(const char *operation, int32_t progress, const char *message);
esp_err_t yamui_async_complete(const char *operation, const char *message);
esp_err_t yamui_async_fail(const char *operation, const char *message);

#ifdef __cplusplus
}
#endif
