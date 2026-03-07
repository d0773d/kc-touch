#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    YML_NODE_UNSET = 0,
    YML_NODE_SCALAR,
    YML_NODE_MAPPING,
    YML_NODE_SEQUENCE,
} yml_node_type_t;

typedef struct yml_node yml_node_t;

yml_node_type_t yml_node_get_type(const yml_node_t *node);
const char *yml_node_get_key(const yml_node_t *node);
const char *yml_node_get_scalar(const yml_node_t *node);
size_t yml_node_child_count(const yml_node_t *node);
const yml_node_t *yml_node_child_at(const yml_node_t *node, size_t index);
const yml_node_t *yml_node_get_child(const yml_node_t *node, const char *key);
const yml_node_t *yml_node_next(const yml_node_t *node);

esp_err_t yaml_core_parse_buffer(const char *data, size_t length, yml_node_t **out_root);
esp_err_t yaml_core_parse_string(const char *data, yml_node_t **out_root);
void yml_node_free(yml_node_t *node);

#ifdef __cplusplus
}
#endif
