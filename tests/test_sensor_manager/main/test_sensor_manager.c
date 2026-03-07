#include <stdio.h>

#include "unity.h"

#include "sensor_manager.h"

static void ensure_sensor_fields(const sensor_record_t *sensor)
{
    TEST_ASSERT_NOT_NULL(sensor);
    TEST_ASSERT_NOT_EQUAL(0, sensor->name[0]);
    TEST_ASSERT_NOT_EQUAL(0, sensor->type[0]);
    TEST_ASSERT_NOT_EQUAL(0, sensor->id[0]);
    TEST_ASSERT_NOT_EQUAL(0, sensor->firmware[0]);
}

TEST_CASE("fake sensors are exposed", "[sensor][fake]")
{
    TEST_ASSERT_EQUAL(ESP_OK, sensor_manager_init());

    size_t count = 0;
    const sensor_record_t *sensors = sensor_manager_get_snapshot(&count);
    TEST_ASSERT_NOT_NULL(sensors);
    TEST_ASSERT_GREATER_THAN(0, count);

    for (size_t i = 0; i < count; ++i) {
        ensure_sensor_fields(&sensors[i]);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, sensors[i].address, "Simulated sensors should have address 0");
    }
}

TEST_CASE("fake sensors tick forward", "[sensor][fake]")
{
    TEST_ASSERT_EQUAL(ESP_OK, sensor_manager_init());

    size_t count = 0;
    const sensor_record_t *before = sensor_manager_get_snapshot(&count);
    TEST_ASSERT_NOT_NULL(before);
    TEST_ASSERT_GREATER_THAN(0, count);

    const float initial = before[0].value;
    TEST_ASSERT_EQUAL(ESP_OK, sensor_manager_update());

    const sensor_record_t *after = sensor_manager_get_snapshot(&count);
    TEST_ASSERT_NOT_NULL(after);
    TEST_ASSERT_NOT_EQUAL(initial, after[0].value);
}

void app_main(void)
{
    UNITY_BEGIN();
    unity_run_menu();
    UNITY_END();
}
