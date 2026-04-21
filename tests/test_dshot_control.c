#include "support/dshot_control_host.h"
#include "unity/unity.h"

static void test_translate_throttle_to_command_maps_neutral_to_zero(void) {
    TEST_ASSERT_EQUAL_UINT16(DSHOT_CMD_NEUTRAL,
                             dshot_translate_throttle_to_command(CMD_THROTTLE_NEUTRAL));
}

static void test_translate_throttle_to_command_maps_forward_range(void) {
    TEST_ASSERT_EQUAL_UINT16(DSHOT_CMD_MIN_FORWARD,
                             dshot_translate_throttle_to_command(CMD_THROTTLE_NEUTRAL + 1));
    TEST_ASSERT_EQUAL_UINT16(DSHOT_CMD_MAX_FORWARD,
                             dshot_translate_throttle_to_command(CMD_THROTTLE_MAX_FORWARD));
}

static void test_translate_throttle_to_command_maps_reverse_range(void) {
    TEST_ASSERT_EQUAL_UINT16(DSHOT_CMD_MIN_REVERSE,
                             dshot_translate_throttle_to_command(CMD_THROTTLE_NEUTRAL - 1));
    TEST_ASSERT_EQUAL_UINT16(DSHOT_CMD_MAX_REVERSE,
                             dshot_translate_throttle_to_command(CMD_THROTTLE_MIN_REVERSE));
}

static void test_translate_throttle_to_command_rejects_out_of_range_values(void) {
    TEST_ASSERT_EQUAL_UINT16(DSHOT_CMD_NEUTRAL, dshot_translate_throttle_to_command(2001));
}

static void test_dominant_failure_name_defaults_to_timeout(void) {
    const struct dshot_statistics stats = {0};

    TEST_ASSERT_EQUAL_STRING("timeout", dshot_dominant_failure_name(&stats));
}

static void test_dominant_failure_name_reports_bad_gcr_when_highest(void) {
    const struct dshot_statistics stats = {.rx_timeout = 1, .rx_bad_gcr = 2};

    TEST_ASSERT_EQUAL_STRING("bad_gcr", dshot_dominant_failure_name(&stats));
}

static void test_dominant_failure_name_reports_bad_crc_when_highest(void) {
    const struct dshot_statistics stats = {.rx_timeout = 1, .rx_bad_gcr = 2, .rx_bad_crc = 3};

    TEST_ASSERT_EQUAL_STRING("bad_crc", dshot_dominant_failure_name(&stats));
}

static void test_dominant_failure_name_reports_bad_type_when_highest(void) {
    const struct dshot_statistics stats = {
        .rx_timeout = 1,
        .rx_bad_gcr = 2,
        .rx_bad_crc = 3,
        .rx_bad_type = 4,
    };

    TEST_ASSERT_EQUAL_STRING("bad_type", dshot_dominant_failure_name(&stats));
}

static void test_dominant_failure_name_keeps_timeout_when_it_is_highest(void) {
    const struct dshot_statistics stats = {
        .rx_timeout = 5,
        .rx_bad_gcr = 4,
        .rx_bad_crc = 3,
        .rx_bad_type = 2,
    };

    TEST_ASSERT_EQUAL_STRING("timeout", dshot_dominant_failure_name(&stats));
}

void test_dshot_control(void) {
    RUN_TEST(test_translate_throttle_to_command_maps_neutral_to_zero);
    RUN_TEST(test_translate_throttle_to_command_maps_forward_range);
    RUN_TEST(test_translate_throttle_to_command_maps_reverse_range);
    RUN_TEST(test_translate_throttle_to_command_rejects_out_of_range_values);
    RUN_TEST(test_dominant_failure_name_defaults_to_timeout);
    RUN_TEST(test_dominant_failure_name_reports_bad_gcr_when_highest);
    RUN_TEST(test_dominant_failure_name_reports_bad_crc_when_highest);
    RUN_TEST(test_dominant_failure_name_reports_bad_type_when_highest);
    RUN_TEST(test_dominant_failure_name_keeps_timeout_when_it_is_highest);
}
