#include <string.h>

#include "sdkconfig.h"
#include "unity.h"

#include "yui_navigation_queue.h"

#ifndef CONFIG_YAMUI_NAV_QUEUE_MAX_DEPTH
#define CONFIG_YAMUI_NAV_QUEUE_MAX_DEPTH 0
#endif

#if CONFIG_YAMUI_NAV_QUEUE_MAX_DEPTH > 0
#define NAV_CALL_CAPACITY (CONFIG_YAMUI_NAV_QUEUE_MAX_DEPTH + 4)
#else
#define NAV_CALL_CAPACITY 16
#endif

typedef struct {
    yui_nav_request_type_t type;
    char arg[32];
} nav_call_t;

static nav_call_t s_calls[NAV_CALL_CAPACITY];
static size_t s_call_count;

static esp_err_t nav_queue_executor(yui_nav_request_type_t type, const char *arg, void *user_ctx)
{
    (void)user_ctx;
    if (s_call_count < sizeof(s_calls) / sizeof(s_calls[0])) {
        s_calls[s_call_count].type = type;
        if (arg) {
            size_t len = strlen(arg);
            if (len >= sizeof(s_calls[s_call_count].arg)) {
                len = sizeof(s_calls[s_call_count].arg) - 1U;
            }
            memcpy(s_calls[s_call_count].arg, arg, len);
            s_calls[s_call_count].arg[len] = '\0';
        } else {
            s_calls[s_call_count].arg[0] = '\0';
        }
    }
    s_call_count++;
    return ESP_OK;
}

static void nav_queue_test_reset(void)
{
    memset(s_calls, 0, sizeof(s_calls));
    s_call_count = 0;
    yui_nav_queue_init(nav_queue_executor, NULL);
}

TEST_CASE("nav queue executes immediately when idle", "[yamui][nav]")
{
    nav_queue_test_reset();
    TEST_ASSERT_EQUAL(ESP_OK, yui_nav_queue_submit(YUI_NAV_REQUEST_GOTO, "home"));
    TEST_ASSERT_EQUAL_UINT32(1, s_call_count);
    TEST_ASSERT_EQUAL(YUI_NAV_REQUEST_GOTO, s_calls[0].type);
    TEST_ASSERT_EQUAL_STRING("home", s_calls[0].arg);
    TEST_ASSERT_EQUAL_UINT32(0, yui_nav_queue_depth());
}

TEST_CASE("nav queue defers during render", "[yamui][nav]")
{
    nav_queue_test_reset();
    TEST_ASSERT_EQUAL(ESP_OK, yui_nav_queue_begin_render());
    TEST_ASSERT_EQUAL(ESP_OK, yui_nav_queue_submit(YUI_NAV_REQUEST_PUSH, "details"));
    TEST_ASSERT_EQUAL(ESP_OK, yui_nav_queue_submit(YUI_NAV_REQUEST_POP, NULL));
    TEST_ASSERT_EQUAL_UINT32(2, yui_nav_queue_depth());
    TEST_ASSERT_EQUAL_UINT32(0, s_call_count);

    yui_nav_queue_end_render(true);

    TEST_ASSERT_EQUAL_UINT32(0, yui_nav_queue_depth());
    TEST_ASSERT_EQUAL_UINT32(2, s_call_count);
    TEST_ASSERT_EQUAL(YUI_NAV_REQUEST_PUSH, s_calls[0].type);
    TEST_ASSERT_EQUAL_STRING("details", s_calls[0].arg);
    TEST_ASSERT_EQUAL(YUI_NAV_REQUEST_POP, s_calls[1].type);
    TEST_ASSERT_EQUAL_STRING("", s_calls[1].arg);
}

TEST_CASE("nav queue reset drops pending work", "[yamui][nav]")
{
    nav_queue_test_reset();
    TEST_ASSERT_EQUAL(ESP_OK, yui_nav_queue_begin_render());
    TEST_ASSERT_EQUAL(ESP_OK, yui_nav_queue_submit(YUI_NAV_REQUEST_GOTO, "overlay"));
    TEST_ASSERT_EQUAL_UINT32(1, yui_nav_queue_depth());

    yui_nav_queue_reset();
    TEST_ASSERT_EQUAL_UINT32(0, yui_nav_queue_depth());

    TEST_ASSERT_EQUAL(ESP_OK, yui_nav_queue_submit(YUI_NAV_REQUEST_POP, NULL));
    TEST_ASSERT_EQUAL_UINT32(1, s_call_count);
    TEST_ASSERT_EQUAL(YUI_NAV_REQUEST_POP, s_calls[0].type);
}

#if CONFIG_YAMUI_NAV_QUEUE_MAX_DEPTH > 0
TEST_CASE("nav queue enforces depth guard", "[yamui][nav]")
{
    const size_t guard = (size_t)CONFIG_YAMUI_NAV_QUEUE_MAX_DEPTH;
    nav_queue_test_reset();
    TEST_ASSERT_EQUAL(ESP_OK, yui_nav_queue_begin_render());

    for (size_t i = 0; i < guard; ++i) {
        TEST_ASSERT_EQUAL(ESP_OK, yui_nav_queue_submit(YUI_NAV_REQUEST_PUSH, NULL));
    }

    TEST_ASSERT_EQUAL_UINT32(guard, yui_nav_queue_depth());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, yui_nav_queue_submit(YUI_NAV_REQUEST_PUSH, NULL));
    TEST_ASSERT_EQUAL_UINT32(guard, yui_nav_queue_depth());
    TEST_ASSERT_EQUAL_UINT32(0, s_call_count);

    yui_nav_queue_end_render(true);

    TEST_ASSERT_EQUAL_UINT32(guard, s_call_count);
    TEST_ASSERT_EQUAL_UINT32(0, yui_nav_queue_depth());
}
#endif

void app_main(void)
{
    UNITY_BEGIN();
    unity_run_menu();
    UNITY_END();
}
