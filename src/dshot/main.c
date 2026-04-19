/*
 * DShot motor controller application for RP2040.
 * Manages 8 motors across 2 PIO state machines (4 channels each),
 * handles USB command input, telemetry output, and EDT negotiation.
 */

#include "../log.h"
#include "../usb_comm.h"
#include "control.h"
#include "dshot.h"
#include "telemetry_usb.h"
#include <hardware/pio.h>
#include <pico/error.h>
#include <pico/stdio.h>
#include <pico/time.h>
#include <pico/types.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Hardware pin mapping: motors 0-3 on pins 6-9, motors 4-7 on pins 18-21 */
#define MOTOR0_PIN_BASE 6
#define MOTOR1_PIN_BASE 18

#define DSHOT_PIO pio0
#define DSHOT_SM_0 0
#define DSHOT_SM_1 1
#define DSHOT_SPEED 300

#define INPUT_PACKET_SIZE USB_INPUT_PACKET_SIZE(NUM_MOTORS)
#define QUALITY_WARN_THRESHOLD 5000
#define QUALITY_REPORT_INTERVAL_MS 100

static uint16_t thruster_values[NUM_MOTORS] = {0};
static absolute_time_t last_comm_time;
static bool edt_enable_scheduled[NUM_MOTORS] = {false};
static absolute_time_t edt_enable_time[NUM_MOTORS];
static absolute_time_t next_quality_report_time;
static bool comm_timed_out = true;
static bool quality_warned[NUM_MOTORS] = {false};

static dshot_telemetry_context_t context0 = {.controller_base_global_id = 0};
static dshot_telemetry_context_t context1 = {.controller_base_global_id = NUM_MOTORS_0};

static bool process_usb_packet(uint8_t *usb_buf, uint16_t *thruster_values,
                               absolute_time_t *last_comm_time) {
    if (!usb_parse_packet(usb_buf, INPUT_PACKET_SIZE, thruster_values, NUM_MOTORS,
                          last_comm_time)) {
        return false;
    }
    return true;
}

static void send_quality_reports(struct dshot_controller *controller0,
                                 struct dshot_controller *controller1) {
    for (int i = 0; i < NUM_MOTORS; i++) {
        struct dshot_controller *ctrl;
        int channel;
        dshot_get_motor_controller(i, &ctrl, &channel, controller0, controller1);
        int16_t quality = dshot_get_telemetry_quality_percent(ctrl, channel);
        dshot_telemetry_usb_send(i, TELEMETRY_TYPE_SIGNAL_QUALITY, (int32_t)quality);

        if (quality < QUALITY_WARN_THRESHOLD && !quality_warned[i]) {
            quality_warned[i] = true;
            log_warnf("Motor %d: signal degraded, cause: %s", i,
                      dshot_dominant_failure_name(&ctrl->motor[channel].stats));
        } else if (quality > QUALITY_WARN_THRESHOLD + 1000 && quality_warned[i]) {
            quality_warned[i] = false;
        }
    }
}

int main() {
    stdio_init_all();
    log_init();
    dshot_telemetry_usb_init();

    struct dshot_controller controller0;
    struct dshot_controller controller1;
    dshot_controller_init(&controller0, DSHOT_SPEED, DSHOT_PIO, DSHOT_SM_0, MOTOR0_PIN_BASE,
                          NUM_MOTORS_0);
    controller0.edt_always_decode = true;
    dshot_register_telemetry_cb(&controller0, dshot_telemetry_callback, &context0);
    dshot_controller_init(&controller1, DSHOT_SPEED, DSHOT_PIO, DSHOT_SM_1, MOTOR1_PIN_BASE,
                          NUM_MOTORS_1);
    controller1.edt_always_decode = true;
    dshot_register_telemetry_cb(&controller1, dshot_telemetry_callback, &context1);

    for (int i = 0; i < NUM_MOTORS; ++i) {
        thruster_values[i] = CMD_THROTTLE_NEUTRAL;
        edt_enable_time[i] = get_absolute_time();
    }
    last_comm_time = get_absolute_time();
    next_quality_report_time = get_absolute_time();

    dshot_send_commands(thruster_values, &controller0, &controller1);
    dshot_run_frame_cycles(&controller0, &controller1, NUM_MOTORS * 4);
    dshot_send_command_to_all(&controller0, &controller1, DSHOT_CMD_3D_MODE_ON, 10);
    dshot_send_command_to_all(&controller0, &controller1, DSHOT_CMD_SAVE_SETTINGS, 10);
    dshot_send_command_to_all(&controller0, &controller1, DSHOT_EXTENDED_TELEMETRY_ENABLE, 10);
    dshot_wait_for_telemetry(&controller0, &controller1);

    static uint8_t usb_buf[INPUT_PACKET_SIZE];
    static size_t usb_idx = 0;

    while (true) {
        usb_idx = usb_poll_input(usb_buf, INPUT_PACKET_SIZE, usb_idx);

        if (usb_idx >= INPUT_PACKET_SIZE) {
            if (process_usb_packet(usb_buf, thruster_values, &last_comm_time)) {
                dshot_mark_activity(&controller0);
                dshot_mark_activity(&controller1);
                if (comm_timed_out) {
                    comm_timed_out = false;
                    log_info("DShot300, 8 motors, USB comm active");

                    char status[32];
                    memcpy(status, "Motor telemetry: ", 17);
                    for (int i = 0; i < NUM_MOTORS; i++) {
                        struct dshot_controller *ctrl;
                        int channel;
                        dshot_get_motor_controller(i, &ctrl, &channel, &controller0, &controller1);
                        bool active =
                            ctrl->motor[channel].telemetry_types & (1 << DSHOT_TELEMETRY_TYPE_ERPM);
                        status[17 + i] = active ? '1' : '0';
                    }
                    status[17 + NUM_MOTORS] = '\0';

                    if (dshot_is_telemetry_active(&controller0) &&
                        dshot_is_telemetry_active(&controller1)) {
                        log_info(status);
                    } else {
                        log_warn(status);
                    }
                }
            }
            usb_idx = 0;
        }

        usb_check_timeout(last_comm_time, thruster_values, NUM_MOTORS, CMD_THROTTLE_NEUTRAL,
                          &usb_idx, &comm_timed_out);
        dshot_send_commands(thruster_values, &controller0, &controller1);
        dshot_enable_edt_if_idle(thruster_values, edt_enable_scheduled, edt_enable_time,
                                 &controller0, &controller1);

        if (dshot_quality_report_due(&next_quality_report_time, QUALITY_REPORT_INTERVAL_MS,
                                     get_absolute_time())) {
            send_quality_reports(&controller0, &controller1);
        }

        dshot_loop(&controller0);
        dshot_loop(&controller1);

        dshot_telemetry_usb_flush();
    }
    return 0;
}
