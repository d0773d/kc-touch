#include "yui_navigation_queue.h"

#include <stdlib.h>
#include <string.h>

#include "yamui_logging.h"

typedef struct {
    yui_nav_request_type_t type;
    char *arg;
} yui_nav_request_t;

static yui_nav_request_t *s_queue;
static size_t s_queue_count;
static size_t s_queue_capacity;
static bool s_rendering;
static yui_nav_request_executor_t s_executor;
static void *s_executor_ctx;

static char *yui_nav_queue_strdup(const char *src)
{
    if (!src) {
        return NULL;
    }
    size_t len = strlen(src) + 1U;
    char *copy = (char *)malloc(len);
    if (copy) {
        memcpy(copy, src, len);
    }
    return copy;
}

static void yui_nav_queue_reset_request(yui_nav_request_t *request)
{
    if (!request) {
        return;
    }
    free(request->arg);
    request->arg = NULL;
}

static esp_err_t yui_nav_queue_push(yui_nav_request_type_t type, const char *arg)
{
    if (s_queue_count == s_queue_capacity) {
        size_t new_capacity = s_queue_capacity == 0 ? 4U : s_queue_capacity * 2U;
        yui_nav_request_t *resized = (yui_nav_request_t *)realloc(s_queue, new_capacity * sizeof(yui_nav_request_t));
        if (!resized) {
            return ESP_ERR_NO_MEM;
        }
        memset(resized + s_queue_capacity, 0, (new_capacity - s_queue_capacity) * sizeof(yui_nav_request_t));
        s_queue = resized;
        s_queue_capacity = new_capacity;
    }
    char *copy = arg ? yui_nav_queue_strdup(arg) : NULL;
    if (arg && !copy) {
        return ESP_ERR_NO_MEM;
    }
    s_queue[s_queue_count].type = type;
    s_queue[s_queue_count].arg = copy;
    s_queue_count++;
    return ESP_OK;
}

static bool yui_nav_queue_pop(yui_nav_request_t *out)
{
    if (s_queue_count == 0U) {
        return false;
    }
    if (out) {
        *out = s_queue[0];
    } else {
        yui_nav_queue_reset_request(&s_queue[0]);
    }
    if (s_queue_count > 1U) {
        memmove(s_queue, s_queue + 1, (s_queue_count - 1U) * sizeof(yui_nav_request_t));
    }
    s_queue_count--;
    return true;
}

static void yui_nav_queue_process(void)
{
    if (s_rendering || !s_executor) {
        return;
    }
    while (s_queue_count > 0U) {
        yui_nav_request_t request = {0};
        if (!yui_nav_queue_pop(&request)) {
            break;
        }
        esp_err_t err = s_executor(request.type, request.arg, s_executor_ctx);
        yui_nav_queue_reset_request(&request);
        if (err != ESP_OK) {
            yamui_log(YAMUI_LOG_LEVEL_WARN, YAMUI_LOG_CAT_NAV, "Queued navigation request failed (%s)", esp_err_to_name(err));
            break;
        }
        if (s_rendering) {
            break;
        }
    }
}

void yui_nav_queue_init(yui_nav_request_executor_t executor, void *user_ctx)
{
    s_executor = executor;
    s_executor_ctx = user_ctx;
    yui_nav_queue_reset();
}

void yui_nav_queue_reset(void)
{
    if (s_queue) {
        for (size_t i = 0; i < s_queue_count; ++i) {
            yui_nav_queue_reset_request(&s_queue[i]);
        }
    }
    s_queue_count = 0;
    s_rendering = false;
}

size_t yui_nav_queue_depth(void)
{
    return s_queue_count;
}

esp_err_t yui_nav_queue_submit(yui_nav_request_type_t type, const char *arg)
{
    if (!s_executor) {
        return ESP_ERR_INVALID_STATE;
    }
    bool queue_pending = s_rendering || s_queue_count > 0U;
    if (!queue_pending) {
        return s_executor(type, arg, s_executor_ctx);
    }
    return yui_nav_queue_push(type, arg);
}

esp_err_t yui_nav_queue_begin_render(void)
{
    if (s_rendering) {
        return ESP_ERR_INVALID_STATE;
    }
    s_rendering = true;
    return ESP_OK;
}

void yui_nav_queue_end_render(bool success)
{
    if (!s_rendering) {
        return;
    }
    s_rendering = false;
    if (success) {
        yui_nav_queue_process();
    }
}
