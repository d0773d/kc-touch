#include "yamui_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "yamui_logging.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define YUI_STATE_DEFAULT_CAPACITY 8
#define YUI_STATE_WATCH_DEFAULT_CAPACITY 4

typedef struct {
    char *key;
    char *value;
} yui_state_entry_t;

struct yui_state_watch {
    uint32_t id;
    char *key;
    yui_state_watch_cb_t cb;
    void *user_ctx;
};

typedef struct {
    yui_state_watch_cb_t cb;
    void *user_ctx;
} yui_state_notification_t;

static SemaphoreHandle_t s_lock;
static yui_state_entry_t *s_entries;
static size_t s_entry_count;
static size_t s_entry_capacity;
static struct yui_state_watch *s_watchers;
static size_t s_watch_count;
static size_t s_watch_capacity;
static uint32_t s_watch_next_id = 1;

static inline const char *yui_empty_if_null(const char *value)
{
    return value ? value : "";
}

static char *yui_state_strdup(const char *value)
{
    value = yui_empty_if_null(value);
    size_t len = strlen(value);
    char *copy = (char *)malloc(len + 1U);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, value, len + 1U);
    return copy;
}

static esp_err_t yui_state_ensure_mutex(void)
{
    if (s_lock) {
        return ESP_OK;
    }
    s_lock = xSemaphoreCreateRecursiveMutex();
    if (!s_lock) {
        yamui_log(YAMUI_LOG_LEVEL_ERROR, YAMUI_LOG_CAT_STATE, "Failed to allocate state lock");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static inline void yui_state_lock(void)
{
    if (s_lock) {
        xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
    }
}

static inline void yui_state_unlock(void)
{
    if (s_lock) {
        xSemaphoreGiveRecursive(s_lock);
    }
}

static esp_err_t yui_state_reserve_entries(size_t desired)
{
    if (desired <= s_entry_capacity) {
        return ESP_OK;
    }
    size_t new_capacity = s_entry_capacity == 0 ? YUI_STATE_DEFAULT_CAPACITY : s_entry_capacity * 2U;
    while (new_capacity < desired) {
        new_capacity *= 2U;
    }
    yui_state_entry_t *next = (yui_state_entry_t *)realloc(s_entries, new_capacity * sizeof(yui_state_entry_t));
    if (!next) {
        return ESP_ERR_NO_MEM;
    }
    s_entries = next;
    s_entry_capacity = new_capacity;
    return ESP_OK;
}

static esp_err_t yui_state_reserve_watchers(size_t desired)
{
    if (desired <= s_watch_capacity) {
        return ESP_OK;
    }
    size_t new_capacity = s_watch_capacity == 0 ? YUI_STATE_WATCH_DEFAULT_CAPACITY : s_watch_capacity * 2U;
    while (new_capacity < desired) {
        new_capacity *= 2U;
    }
    struct yui_state_watch *next = (struct yui_state_watch *)realloc(s_watchers, new_capacity * sizeof(struct yui_state_watch));
    if (!next) {
        return ESP_ERR_NO_MEM;
    }
    s_watchers = next;
    s_watch_capacity = new_capacity;
    return ESP_OK;
}

static int yui_state_find_index(const char *key)
{
    for (size_t i = 0; i < s_entry_count; ++i) {
        if (strcmp(s_entries[i].key, key) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static void yui_state_free_entry(yui_state_entry_t *entry)
{
    if (!entry) {
        return;
    }
    free(entry->key);
    free(entry->value);
    entry->key = NULL;
    entry->value = NULL;
}

static void yui_state_free_watch(struct yui_state_watch *watch)
{
    if (!watch) {
        return;
    }
    free(watch->key);
    watch->key = NULL;
    watch->cb = NULL;
    watch->user_ctx = NULL;
    watch->id = 0;
}

static bool yui_state_is_wildcard(const struct yui_state_watch *watch)
{
    if (!watch || !watch->key) {
        return true;
    }
    return watch->key[0] == '\0';
}

static bool yui_state_watch_matches(const struct yui_state_watch *watch, const char *key)
{
    if (!watch || !watch->cb) {
        return false;
    }
    if (yui_state_is_wildcard(watch)) {
        return true;
    }
    return strcmp(watch->key, key) == 0;
}

static esp_err_t yui_state_set_internal(const char *key, const char *value, bool notify)
{
    if (!key || key[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = yui_state_ensure_mutex();
    if (err != ESP_OK) {
        return err;
    }

    key = yui_empty_if_null(key);
    value = yui_empty_if_null(value);

    yui_state_lock();

    int index = yui_state_find_index(key);
    char *new_value = NULL;
    bool updated = false;

    if (index >= 0) {
        if (strcmp(s_entries[index].value, value) == 0) {
            notify = false;
        } else {
            new_value = yui_state_strdup(value);
            if (!new_value) {
                yui_state_unlock();
                return ESP_ERR_NO_MEM;
            }
            free(s_entries[index].value);
            s_entries[index].value = new_value;
            updated = true;
        }
    } else {
        err = yui_state_reserve_entries(s_entry_count + 1);
        if (err != ESP_OK) {
            yui_state_unlock();
            return err;
        }
        char *key_copy = yui_state_strdup(key);
        char *value_copy = yui_state_strdup(value);
        if (!key_copy || !value_copy) {
            free(key_copy);
            free(value_copy);
            yui_state_unlock();
            return ESP_ERR_NO_MEM;
        }
        s_entries[s_entry_count].key = key_copy;
        s_entries[s_entry_count].value = value_copy;
        index = (int)s_entry_count;
        s_entry_count++;
        updated = true;
    }

    yui_state_notification_t *notifications = NULL;
    size_t notify_count = 0;
    if (notify && updated && s_watch_count > 0U) {
        for (size_t i = 0; i < s_watch_count; ++i) {
            if (yui_state_watch_matches(&s_watchers[i], key)) {
                notify_count++;
            }
        }
        if (notify_count > 0U) {
            notifications = (yui_state_notification_t *)malloc(notify_count * sizeof(yui_state_notification_t));
            if (!notifications) {
                yamui_log(YAMUI_LOG_LEVEL_WARN, YAMUI_LOG_CAT_STATE, "State updated but notifications dropped (OOM)");
                notify_count = 0;
            } else {
                size_t cursor = 0;
                for (size_t i = 0; i < s_watch_count; ++i) {
                    if (yui_state_watch_matches(&s_watchers[i], key)) {
                        notifications[cursor].cb = s_watchers[i].cb;
                        notifications[cursor].user_ctx = s_watchers[i].user_ctx;
                        cursor++;
                    }
                }
            }
        }
    }

    const char *final_value = s_entries[index].value;
    yui_state_unlock();

    if (notify && updated) {
        yamui_telemetry_state_change(key, final_value);
        yamui_log(YAMUI_LOG_LEVEL_DEBUG, YAMUI_LOG_CAT_STATE, "%s = %s", key, final_value ? final_value : "");
    }

    for (size_t i = 0; i < notify_count; ++i) {
        if (notifications[i].cb) {
            notifications[i].cb(key, final_value, notifications[i].user_ctx);
        }
    }
    free(notifications);

    return ESP_OK;
}

esp_err_t yui_state_init(void)
{
    return yui_state_ensure_mutex();
}

void yui_state_deinit(void)
{
    if (!s_lock) {
        return;
    }
    yui_state_lock();
    for (size_t i = 0; i < s_entry_count; ++i) {
        yui_state_free_entry(&s_entries[i]);
    }
    free(s_entries);
    s_entries = NULL;
    s_entry_count = 0;
    s_entry_capacity = 0;

    for (size_t i = 0; i < s_watch_count; ++i) {
        yui_state_free_watch(&s_watchers[i]);
    }
    free(s_watchers);
    s_watchers = NULL;
    s_watch_count = 0;
    s_watch_capacity = 0;
    s_watch_next_id = 1;

    yui_state_unlock();
    vSemaphoreDelete(s_lock);
    s_lock = NULL;
}

void yui_state_clear(void)
{
    if (yui_state_ensure_mutex() != ESP_OK) {
        return;
    }
    yui_state_lock();
    for (size_t i = 0; i < s_entry_count; ++i) {
        yui_state_free_entry(&s_entries[i]);
    }
    free(s_entries);
    s_entries = NULL;
    s_entry_count = 0;
    s_entry_capacity = 0;
    yui_state_unlock();
}

esp_err_t yui_state_seed(const yui_state_seed_t *entries, size_t entry_count)
{
    if (!entries || entry_count == 0U) {
        return ESP_OK;
    }
    for (size_t i = 0; i < entry_count; ++i) {
        const char *key = entries[i].key;
        if (!key || key[0] == '\0') {
            continue;
        }
        esp_err_t err = yui_state_set_internal(key, entries[i].value, false);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t yui_state_seed_from_yaml(const yml_node_t *state_node)
{
    if (!state_node) {
        return ESP_OK;
    }
    if (yml_node_get_type(state_node) != YML_NODE_MAPPING) {
        yamui_log(YAMUI_LOG_LEVEL_ERROR, YAMUI_LOG_CAT_STATE, "state block must be a mapping");
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = yui_state_init();
    if (err != ESP_OK) {
        return err;
    }
    for (const yml_node_t *child = yml_node_child_at(state_node, 0); child; child = yml_node_next(child)) {
        const char *key = yml_node_get_key(child);
        if (!key || key[0] == '\0') {
            continue;
        }
        const char *existing = yui_state_get(key, NULL);
        if (existing) {
            continue;
        }
        const char *scalar = yml_node_get_scalar(child);
        err = yui_state_set_internal(key, scalar, false);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t yui_state_set(const char *key, const char *value)
{
    return yui_state_set_internal(key, value, true);
}

esp_err_t yui_state_set_int(const char *key, int32_t value)
{
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%ld", (long)value);
    return yui_state_set(key, buffer);
}

esp_err_t yui_state_set_bool(const char *key, bool value)
{
    return yui_state_set(key, value ? "true" : "false");
}

const char *yui_state_get(const char *key, const char *default_value)
{
    if (!key || key[0] == '\0' || yui_state_ensure_mutex() != ESP_OK) {
        return default_value;
    }
    const char *result = default_value;
    yui_state_lock();
    int index = yui_state_find_index(key);
    if (index >= 0) {
        result = s_entries[index].value;
    }
    yui_state_unlock();
    return result;
}

int32_t yui_state_get_int(const char *key, int32_t default_value)
{
    const char *value = yui_state_get(key, NULL);
    if (!value || value[0] == '\0') {
        return default_value;
    }
    return (int32_t)strtol(value, NULL, 10);
}

bool yui_state_get_bool(const char *key, bool default_value)
{
    const char *value = yui_state_get(key, NULL);
    if (!value) {
        return default_value;
    }
    if (strcasecmp(value, "true") == 0 || strcmp(value, "1") == 0) {
        return true;
    }
    if (strcasecmp(value, "false") == 0 || strcmp(value, "0") == 0) {
        return false;
    }
    return default_value;
}

esp_err_t yui_state_watch(const char *key, yui_state_watch_cb_t cb, void *user_ctx, yui_state_watch_handle_t *out_handle)
{
    if (!cb) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = yui_state_ensure_mutex();
    if (err != ESP_OK) {
        return err;
    }

    char *key_copy = NULL;
    if (key && key[0] != '\0') {
        key_copy = yui_state_strdup(key);
        if (!key_copy) {
            return ESP_ERR_NO_MEM;
        }
    }

    yui_state_lock();
    err = yui_state_reserve_watchers(s_watch_count + 1U);
    if (err != ESP_OK) {
        yui_state_unlock();
        free(key_copy);
        return err;
    }
    struct yui_state_watch *watch = &s_watchers[s_watch_count++];
    watch->id = s_watch_next_id++;
    watch->key = key_copy;
    watch->cb = cb;
    watch->user_ctx = user_ctx;
    uint32_t handle = watch->id;
    yui_state_unlock();

    if (out_handle) {
        *out_handle = handle;
    }
    return ESP_OK;
}

void yui_state_unwatch(yui_state_watch_handle_t handle)
{
    if (handle == 0U || !s_lock) {
        return;
    }
    yui_state_lock();
    for (size_t i = 0; i < s_watch_count; ++i) {
        if (s_watchers[i].id == handle) {
            yui_state_free_watch(&s_watchers[i]);
            if (i + 1U < s_watch_count) {
                s_watchers[i] = s_watchers[s_watch_count - 1U];
            }
            s_watch_count--;
            break;
        }
    }
    yui_state_unlock();
}
