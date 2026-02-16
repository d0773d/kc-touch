#include "ui_schemas.h"

#include <ctype.h>
#include <stdbool.h>
#include <string.h>

extern const uint8_t _binary_home_yml_start[];
extern const uint8_t _binary_home_yml_end[];

typedef struct {
    const char *name;
    const uint8_t *start;
    const uint8_t *end;
} yui_schema_blob_t;

static const yui_schema_blob_t s_schema_blobs[] = {
    {"home", _binary_home_yml_start, _binary_home_yml_end},
};

static const char *s_default_schema_name = "home";

static bool yui_schema_name_equals(const char *lhs, const char *rhs)
{
    while (*lhs && *rhs) {
        char a = (char)tolower((unsigned char)*lhs++);
        char b = (char)tolower((unsigned char)*rhs++);
        if (a != b) {
            return false;
        }
    }
    return *lhs == '\0' && *rhs == '\0';
}

const char *ui_schemas_get_default_name(void)
{
    return s_default_schema_name;
}

static const yui_schema_blob_t *yui_find_schema_blob(const char *name)
{
    if (!name || name[0] == '\0') {
        name = s_default_schema_name;
    }
    for (size_t i = 0; i < sizeof(s_schema_blobs) / sizeof(s_schema_blobs[0]); ++i) {
        if (yui_schema_name_equals(name, s_schema_blobs[i].name)) {
            return &s_schema_blobs[i];
        }
    }
    return NULL;
}

const uint8_t *ui_schemas_get_named(const char *name, size_t *out_size)
{
    const yui_schema_blob_t *blob = yui_find_schema_blob(name);
    if (!blob) {
        if (out_size) {
            *out_size = 0;
        }
        return NULL;
    }
    if (out_size) {
        *out_size = (size_t)(blob->end - blob->start);
    }
    return blob->start;
}

const uint8_t *ui_schemas_get_home(size_t *out_size)
{
    return ui_schemas_get_named("home", out_size);
}
