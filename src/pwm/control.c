#include "control.h"

uint16_t pwm_translate_throttle(uint16_t cmd) {
    if (cmd > PWM_CMD_RANGE_MAX) {
        cmd = PWM_CMD_RANGE_MAX;
    }
    return PWM_MIN + ((cmd * (PWM_MAX - PWM_MIN)) / PWM_CMD_RANGE_MAX);
}

bool pwm_process_usb_packet(uint8_t *usb_buf, uint16_t *thruster_values,
                            absolute_time_t *last_comm_time) {
    uint16_t raw_values[NUM_MOTORS];
    if (!usb_parse_packet(usb_buf, INPUT_PACKET_SIZE, raw_values, NUM_MOTORS, last_comm_time)) {
        return false;
    }

    for (int i = 0; i < NUM_MOTORS; ++i) {
        thruster_values[i] = pwm_translate_throttle(raw_values[i]);
    }
    return true;
}
