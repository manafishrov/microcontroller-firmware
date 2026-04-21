#ifndef TESTS_SUPPORT_DSHOT_CONTROL_HOST_H
#define TESTS_SUPPORT_DSHOT_CONTROL_HOST_H

#include <stdint.h>

#define CMD_THROTTLE_MIN_REVERSE 0
#define CMD_THROTTLE_NEUTRAL 1000
#define CMD_THROTTLE_MAX_FORWARD 2000
#define DSHOT_CMD_NEUTRAL 0
#define DSHOT_CMD_MIN_REVERSE 48
#define DSHOT_CMD_MAX_REVERSE 1047
#define DSHOT_CMD_MIN_FORWARD 1048
#define DSHOT_CMD_MAX_FORWARD 2047

struct dshot_statistics {
    uint32_t tx_frames;
    uint32_t rx_frames;
    uint32_t rx_timeout;
    uint32_t rx_bad_gcr;
    uint32_t rx_bad_crc;
    uint32_t rx_bad_type;
};

uint16_t dshot_translate_throttle_to_command(uint16_t cmd_throttle);
const char *dshot_dominant_failure_name(const struct dshot_statistics *stats);

#endif
