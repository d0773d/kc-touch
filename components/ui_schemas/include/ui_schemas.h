#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

const uint8_t *ui_schemas_get_home(size_t *out_size);
const uint8_t *ui_schemas_get_named(const char *name, size_t *out_size);
const char *ui_schemas_get_default_name(void);

#ifdef __cplusplus
}
#endif
