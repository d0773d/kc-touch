#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    YUI_NAV_REQUEST_GOTO = 0,
    YUI_NAV_REQUEST_PUSH,
    YUI_NAV_REQUEST_POP,
} yui_nav_request_type_t;

typedef esp_err_t (*yui_nav_request_executor_t)(yui_nav_request_type_t type, const char *arg, void *user_ctx);

void yui_nav_queue_init(yui_nav_request_executor_t executor, void *user_ctx);
void yui_nav_queue_reset(void);
esp_err_t yui_nav_queue_submit(yui_nav_request_type_t type, const char *arg);
esp_err_t yui_nav_queue_begin_render(void);
void yui_nav_queue_end_render(bool success);
size_t yui_nav_queue_depth(void);

#ifdef __cplusplus
}
#endif
