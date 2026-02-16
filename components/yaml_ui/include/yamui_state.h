#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "yaml_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define YUI_STATE_KEY_MAX 64
#define YUI_STATE_VALUE_MAX 128

typedef void (*yui_state_watch_cb_t)(const char *key, const char *value, void *user_ctx);
/** Listener handle used to unsubscribe from state changes. */
typedef uint32_t yui_state_watch_handle_t;


/** Optional helper used when seeding the state store programmatically. */
typedef struct {
    const char *key;
    const char *value;
} yui_state_seed_t;

/**
 * @brief Initialize the global YamUI state store.
 *
 * Safe to call multiple times; subsequent calls become no-ops.
 */
esp_err_t yui_state_init(void);

/**
 * @brief Destroy all state entries, watchers, and synchronization primitives.
 */
void yui_state_deinit(void);

/**
 * @brief Remove all key/value pairs but keep the store and watchers allocated.
 */
void yui_state_clear(void);

/**
 * @brief Seed the state store with the provided key/value pairs without notifying watchers.
 */
esp_err_t yui_state_seed(const yui_state_seed_t *entries, size_t entry_count);

/**
 * @brief Seed the state store from a YAML mapping node (typically the top-level `state:` block).
 */
esp_err_t yui_state_seed_from_yaml(const yml_node_t *state_node);

/**
 * @brief Store a string value. Passing NULL resets the key to an empty string.
 */
esp_err_t yui_state_set(const char *key, const char *value);

/** Convenience helpers for numeric and boolean values. */
esp_err_t yui_state_set_int(const char *key, int32_t value);
esp_err_t yui_state_set_bool(const char *key, bool value);

/**
 * @brief Fetch a value. Returns @p default_value when the key is unknown.
 */
const char *yui_state_get(const char *key, const char *default_value);
int32_t yui_state_get_int(const char *key, int32_t default_value);
bool yui_state_get_bool(const char *key, bool default_value);

/**
 * @brief Register a watcher for @p key.
 *
 * Passing NULL (or an empty string) subscribes to all state changes.
 */
esp_err_t yui_state_watch(const char *key, yui_state_watch_cb_t cb, void *user_ctx, yui_state_watch_handle_t *out_handle);

/**
 * @brief Remove a previously registered watcher.
 */
void yui_state_unwatch(yui_state_watch_handle_t handle);

#ifdef __cplusplus
}
#endif
