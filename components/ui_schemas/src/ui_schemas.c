#include "ui_schemas.h"

extern const uint8_t _binary_home_yml_start[];
extern const uint8_t _binary_home_yml_end[];

const uint8_t *ui_schemas_get_home(size_t *out_size)
{
    if (out_size) {
        *out_size = (size_t)(_binary_home_yml_end - _binary_home_yml_start);
    }
    return _binary_home_yml_start;
}
