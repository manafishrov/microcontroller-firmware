/*
 * PWM motor controller application for RP2040.
 * Receives throttle commands over USB and outputs standard PWM signals.
 * No telemetry support (unlike DShot variant).
 */

#include "../log.h"
#include "../usb_comm.h"
#include "control.h"
#include "pwm.h"
#include <pico/error.h>
#include <pico/stdio.h>
#include <pico/time.h>
#include <pico/types.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint16_t thruster_values[NUM_MOTORS] = {PWM_NEUTRAL};
static absolute_time_t last_comm_time;
static bool comm_timed_out = true;

int main() {
    stdio_init_all();
    log_init();

    struct pwm_controller controller;
    uint pins[NUM_MOTORS] = {6, 7, 8, 9, 18, 19, 20, 21};
    for (int i = 0; i < NUM_MOTORS; ++i) {
        thruster_values[i] = PWM_NEUTRAL;
    }
    pwm_controller_init(&controller, pins, NUM_MOTORS);
    last_comm_time = get_absolute_time();

    static uint8_t usb_buf[INPUT_PACKET_SIZE];
    static size_t usb_idx = 0;

    while (true) {
        usb_idx = usb_poll_input(usb_buf, sizeof(usb_buf), usb_idx);

        if (usb_idx >= sizeof(usb_buf)) {
            if (pwm_process_usb_packet(usb_buf, thruster_values, &last_comm_time) &&
                comm_timed_out) {
                comm_timed_out = false;
                log_info("PWM, 8 motors, USB comm active");
            }
            usb_idx = 0;
        }

        usb_check_timeout(last_comm_time, thruster_values, NUM_MOTORS, PWM_NEUTRAL, &usb_idx,
                          &comm_timed_out);
        for (int i = 0; i < NUM_MOTORS; ++i) {
            pwm_set_throttle(&controller, i, thruster_values[i]);
        }
    }
    return 0;
}
