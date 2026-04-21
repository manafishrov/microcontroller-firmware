#ifndef TESTS_SUPPORT_DSHOT_STUBS_HOST_H
#define TESTS_SUPPORT_DSHOT_STUBS_HOST_H

#include <stdbool.h>
#include <stdint.h>

#define DSHOT_MAX_CHANNELS 26

struct dshot_motor {
    uint16_t current_command;
    uint16_t last_throttle_value;
    uint8_t command_counter;
};

struct dshot_controller {
    uint8_t num_channels;
    struct dshot_motor motor[DSHOT_MAX_CHANNELS];
};

#endif
