#include "yaml_core.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#define YML_MAX_STACK_DEPTH 32

struct yml_node {
    yml_node_type_t type;
    char *key;
    union {
        char *scalar;
        struct {
            struct yml_node *head;
            struct yml_node *tail;
            size_t count;
        } children;
    } data;
    struct yml_node *parent;
    struct yml_node *next;
};

typedef struct {
    int indent;
    bool is_sequence;
    bool has_colon;
    char *key;
    char *value;
} yml_line_t;

typedef struct {
    yml_node_t *node;
    int indent;
} yml_stack_entry_t;

static const char *TAG = "yaml_core";

static inline void *yml_alloc(size_t size)
{
    return calloc(1, size);
}

static char *yml_strdup_range(const char *src, size_t len)
{
    char *out = (char *)malloc(len + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, src, len);
    out[len] = '\0';
    return out;
}

static void yml_trim(char *str)
{
    if (!str) {
        return;
    }
    size_t len = strlen(str);
    size_t start = 0;
    while (start < len && isspace((int)str[start])) {
        start++;
    }
    size_t end = len;
    while (end > start && isspace((int)str[end - 1])) {
        end--;
    }
    if (start > 0 || end < len) {
        memmove(str, str + start, end - start);
        str[end - start] = '\0';
    }
    if (str[0] == '\0') {
        return;
    }
    size_t new_len = strlen(str);
    if ((str[0] == '"' && str[new_len - 1] == '"') || (str[0] == '\'' && str[new_len - 1] == '\'')) {
        if (new_len >= 2) {
            memmove(str, str + 1, new_len - 2);
            str[new_len - 2] = '\0';
        }
    }
}

static char *yml_find_unquoted_colon(char *str)
{
    bool in_quote = false;
    char quote_char = '\0';
    for (char *p = str; *p; ++p) {
        if ((*p == '"' || *p == '\'') && (p == str || p[-1] != '\\')) {
            if (in_quote && *p == quote_char) {
                in_quote = false;
                quote_char = '\0';
            } else if (!in_quote) {
                in_quote = true;
                quote_char = *p;
            }
        } else if (*p == ':' && !in_quote) {
            return p;
        }
    }
    return NULL;
}

static void yml_line_cleanup(yml_line_t *line)
{
    if (!line) {
        return;
    }
    free(line->key);
    free(line->value);
    memset(line, 0, sizeof(*line));
}

static esp_err_t yml_next_line(const char *data, size_t length, size_t *cursor, yml_line_t *out)
{
    while (*cursor < length) {
        size_t line_start = *cursor;
        size_t line_end = line_start;
        while (line_end < length && data[line_end] != '\n' && data[line_end] != '\r') {
            line_end++;
        }
        size_t next = line_end;
        while (next < length && (data[next] == '\n' || data[next] == '\r')) {
            next++;
        }
        *cursor = next;
        if (line_end == line_start) {
            continue;
        }

        size_t indent = 0;
        bool seen_char = false;
        while (line_start + indent < line_end) {
            char c = data[line_start + indent];
            if (c == ' ') {
                indent++;
            } else if (c == '\t') {
                ESP_LOGE(TAG, "Tabs are not supported in YAML input");
                return ESP_ERR_INVALID_RESPONSE;
            } else {
                seen_char = true;
                break;
            }
        }
        if (!seen_char) {
            continue;
        }
        size_t content_len = line_end - (line_start + indent);
        if (content_len == 0) {
            continue;
        }

        char *content = yml_strdup_range(data + line_start + indent, content_len);
        if (!content) {
            return ESP_ERR_NO_MEM;
        }

        bool in_quote = false;
        char quote_char = '\0';
        for (size_t i = 0; content[i]; ++i) {
            char c = content[i];
            if ((c == '"' || c == '\'') && (i == 0 || content[i - 1] != '\\')) {
                if (in_quote && c == quote_char) {
                    in_quote = false;
                    quote_char = '\0';
                } else if (!in_quote) {
                    in_quote = true;
                    quote_char = c;
                }
            } else if (c == '#' && !in_quote) {
                content[i] = '\0';
                break;
            }
        }
        yml_trim(content);
        if (content[0] == '\0') {
            free(content);
            continue;
        }

        char *payload = content;
        bool is_sequence = false;
        if (payload[0] == '-' && (payload[1] == '\0' || isspace((int)payload[1]))) {
            is_sequence = true;
            payload++;
            while (isspace((int)*payload)) {
                payload++;
            }
        }

        char *key = NULL;
        char *value = NULL;
        bool has_colon = false;
        char *colon = yml_find_unquoted_colon(payload);
        if (colon) {
            has_colon = true;
            key = yml_strdup_range(payload, colon - payload);
            if (!key) {
                free(content);
                return ESP_ERR_NO_MEM;
            }
            yml_trim(key);
            value = strdup(colon + 1);
            if (!value) {
                free(key);
                free(content);
                return ESP_ERR_NO_MEM;
            }
            yml_trim(value);
            if (value[0] == '\0') {
                free(value);
                value = NULL;
            }
        } else {
            if (!is_sequence) {
                ESP_LOGE(TAG, "Invalid YAML line, missing ':' separator");
                free(content);
                return ESP_ERR_INVALID_RESPONSE;
            }
            value = strdup(payload);
            if (!value) {
                free(content);
                return ESP_ERR_NO_MEM;
            }
            yml_trim(value);
            if (value[0] == '\0') {
                free(value);
                value = NULL;
            }
        }

        out->indent = (int)indent;
        out->is_sequence = is_sequence;
        out->has_colon = has_colon;
        out->key = key;
        out->value = value;
        free(content);
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

static yml_node_t *yml_node_create(yml_node_type_t type)
{
    yml_node_t *node = (yml_node_t *)yml_alloc(sizeof(yml_node_t));
    if (!node) {
        return NULL;
    }
    node->type = type;
    return node;
}

static void yml_node_append_child(yml_node_t *parent, yml_node_t *child)
{
    if (!parent || !child) {
        return;
    }
    child->parent = parent;
    child->next = NULL;
    if (!parent->data.children.head) {
        parent->data.children.head = child;
        parent->data.children.tail = child;
    } else {
        parent->data.children.tail->next = child;
        parent->data.children.tail = child;
    }
    parent->data.children.count++;
}

static esp_err_t yml_attach_scalar(yml_node_t *node, char *value)
{
    node->type = YML_NODE_SCALAR;
    node->data.scalar = value;
    return ESP_OK;
}

static esp_err_t yml_process_sequence_line(yml_line_t *line, yml_stack_entry_t *stack, size_t *depth)
{
    if (*depth == 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    yml_node_t *parent = stack[*depth - 1].node;
    if (parent->type == YML_NODE_UNSET) {
        parent->type = YML_NODE_SEQUENCE;
    }
    if (parent->type != YML_NODE_SEQUENCE) {
        ESP_LOGE(TAG, "Sequence entry encountered but parent is not a sequence");
        return ESP_ERR_INVALID_RESPONSE;
    }

    yml_node_t *entry = yml_node_create(YML_NODE_UNSET);
    if (!entry) {
        return ESP_ERR_NO_MEM;
    }
    yml_node_append_child(parent, entry);

    if (line->has_colon && line->key) {
        entry->type = YML_NODE_MAPPING;
        yml_node_t *child = yml_node_create(YML_NODE_UNSET);
        if (!child) {
            return ESP_ERR_NO_MEM;
        }
        child->key = line->key;
        line->key = NULL;
        if (line->value) {
            yml_attach_scalar(child, line->value);
            line->value = NULL;
        }
        yml_node_append_child(entry, child);
        if (*depth >= YML_MAX_STACK_DEPTH) {
            ESP_LOGE(TAG, "YAML nesting too deep");
            return ESP_ERR_INVALID_RESPONSE;
        }
        stack[*depth].node = entry;
        stack[*depth].indent = line->indent;
        (*depth)++;
        return ESP_OK;
    }

    if (line->value) {
        yml_attach_scalar(entry, line->value);
        line->value = NULL;
        return ESP_OK;
    }

    if (*depth >= YML_MAX_STACK_DEPTH) {
        ESP_LOGE(TAG, "YAML nesting too deep");
        return ESP_ERR_INVALID_RESPONSE;
    }
    stack[*depth].node = entry;
    stack[*depth].indent = line->indent;
    (*depth)++;
    return ESP_OK;
}

static esp_err_t yml_process_mapping_line(yml_line_t *line, yml_stack_entry_t *stack, size_t *depth)
{
    if (!line->key) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (*depth == 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    yml_node_t *parent = stack[*depth - 1].node;
    if (parent->type == YML_NODE_UNSET) {
        parent->type = YML_NODE_MAPPING;
    }
    if (parent->type != YML_NODE_MAPPING) {
        ESP_LOGE(TAG, "Mapping entry encountered but parent is not a mapping");
        return ESP_ERR_INVALID_RESPONSE;
    }

    yml_node_t *node = yml_node_create(YML_NODE_UNSET);
    if (!node) {
        return ESP_ERR_NO_MEM;
    }
    node->key = line->key;
    line->key = NULL;
    if (line->value) {
        yml_attach_scalar(node, line->value);
        line->value = NULL;
    }
    yml_node_append_child(parent, node);

    if (node->type != YML_NODE_SCALAR) {
        if (*depth >= YML_MAX_STACK_DEPTH) {
            ESP_LOGE(TAG, "YAML nesting too deep");
            return ESP_ERR_INVALID_RESPONSE;
        }
        stack[*depth].node = node;
        stack[*depth].indent = line->indent;
        (*depth)++;
    }
    return ESP_OK;
}

esp_err_t yaml_core_parse_buffer(const char *data, size_t length, yml_node_t **out_root)
{
    if (!data || !out_root) {
        return ESP_ERR_INVALID_ARG;
    }
    yml_node_t *root = yml_node_create(YML_NODE_MAPPING);
    if (!root) {
        return ESP_ERR_NO_MEM;
    }

    yml_stack_entry_t stack[YML_MAX_STACK_DEPTH] = {
        {.node = root, .indent = -1},
    };
    size_t depth = 1;
    size_t cursor = 0;
    yml_line_t line = {0};

    while (true) {
        yml_line_cleanup(&line);
        esp_err_t err = yml_next_line(data, length, &cursor, &line);
        if (err == ESP_ERR_NOT_FOUND) {
            break;
        } else if (err != ESP_OK) {
            yml_node_free(root);
            yml_line_cleanup(&line);
            return err;
        }

        while (depth > 0 && line.indent <= stack[depth - 1].indent) {
            depth--;
        }
        if (depth == 0) {
            yml_node_free(root);
            return ESP_ERR_INVALID_RESPONSE;
        }

        if (line.is_sequence) {
            err = yml_process_sequence_line(&line, stack, &depth);
        } else {
            err = yml_process_mapping_line(&line, stack, &depth);
        }
        if (err != ESP_OK) {
            yml_node_free(root);
            yml_line_cleanup(&line);
            return err;
        }
    }

    yml_line_cleanup(&line);
    *out_root = root;
    return ESP_OK;
}

esp_err_t yaml_core_parse_string(const char *data, yml_node_t **out_root)
{
    if (!data) {
        return ESP_ERR_INVALID_ARG;
    }
    return yaml_core_parse_buffer(data, strlen(data), out_root);
}

void yml_node_free(yml_node_t *node)
{
    if (!node) {
        return;
    }
    if (node->type == YML_NODE_SCALAR) {
        free(node->data.scalar);
    } else {
        yml_node_t *child = node->data.children.head;
        while (child) {
            yml_node_t *next = child->next;
            yml_node_free(child);
            child = next;
        }
    }
    free(node->key);
    free(node);
}

yml_node_type_t yml_node_get_type(const yml_node_t *node)
{
    if (!node) {
        return YML_NODE_UNSET;
    }
    return node->type;
}

const char *yml_node_get_key(const yml_node_t *node)
{
    return node ? node->key : NULL;
}

const char *yml_node_get_scalar(const yml_node_t *node)
{
    if (!node || node->type != YML_NODE_SCALAR) {
        return NULL;
    }
    return node->data.scalar;
}

size_t yml_node_child_count(const yml_node_t *node)
{
    if (!node || (node->type != YML_NODE_MAPPING && node->type != YML_NODE_SEQUENCE)) {
        return 0;
    }
    return node->data.children.count;
}

const yml_node_t *yml_node_child_at(const yml_node_t *node, size_t index)
{
    if (!node || (node->type != YML_NODE_MAPPING && node->type != YML_NODE_SEQUENCE)) {
        return NULL;
    }
    const yml_node_t *child = node->data.children.head;
    size_t idx = 0;
    while (child && idx < index) {
        child = child->next;
        idx++;
    }
    return child;
}

const yml_node_t *yml_node_get_child(const yml_node_t *node, const char *key)
{
    if (!node || node->type != YML_NODE_MAPPING || !key) {
        return NULL;
    }
    for (const yml_node_t *child = node->data.children.head; child; child = child->next) {
        if (child->key && strcmp(child->key, key) == 0) {
            return child;
        }
    }
    return NULL;
}

const yml_node_t *yml_node_next(const yml_node_t *node)
{
    return node ? node->next : NULL;
}
