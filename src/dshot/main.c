/*
 * DShot motor controller application for RP2040.
 * Manages 8 motors across 2 PIO state machines (4 channels each),
 * handles USB command input, telemetry output, and EDT negotiation.
 */

#include "../log.h"
#include "dshot.h"
#include <hardware/pio.h>
#include <hardware/sync.h>
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
#define NUM_MOTORS_0 4
#define NUM_MOTORS_1 4
#define NUM_MOTORS (NUM_MOTORS_0 + NUM_MOTORS_1)

#define DSHOT_PIO pio0
#define DSHOT_SM_0 0
#define DSHOT_SM_1 1
#define DSHOT_SPEED 300

#define COMM_TIMEOUT_MS 200

/*
 * Throttle command mapping (from USB input to DShot values):
 *   USB 0    = max reverse  -> DShot 1047 (reverse range: 48-1047)
 *   USB 1000 = neutral/stop -> DShot 0
 *   USB 2000 = max forward  -> DShot 2047 (forward range: 1048-2047)
 */
#define CMD_THROTTLE_MIN_REVERSE 0
#define CMD_THROTTLE_NEUTRAL 1000
#define CMD_THROTTLE_MAX_FORWARD 2000
#define DSHOT_CMD_NEUTRAL 0
#define DSHOT_CMD_MIN_REVERSE 48
#define DSHOT_CMD_MAX_REVERSE 1047
#define DSHOT_CMD_MIN_FORWARD 1048
#define DSHOT_CMD_MAX_FORWARD 2047

/*
 * Telemetry USB packet format (8 bytes):
 *   [0xA5] [motor_id] [type] [value_le32...] [xor_checksum]
 * Type IDs are a stable wire format, independent of the dshot_telemetry_type enum.
 */
#define TELEMETRY_START_BYTE 0xA5
#define TELEMETRY_PACKET_SIZE 8
#define TELEMETRY_TYPE_ERPM 0
#define TELEMETRY_TYPE_VOLTAGE 1
#define TELEMETRY_TYPE_TEMPERATURE 2
#define TELEMETRY_TYPE_CURRENT 3
#define TELEMETRY_TYPE_SIGNAL_QUALITY 4

#define TELEMETRY_QUEUE_CAPACITY 256

/* USB input packet format: [0x5A] [throttle_le16 x NUM_MOTORS] [xor_checksum] */
#define INPUT_START_BYTE 0x5A
#define INPUT_PACKET_SIZE (1 + (NUM_MOTORS * 2) + 1)

static uint16_t thruster_values[NUM_MOTORS] = {0};
static absolute_time_t last_comm_time;
static bool edt_enable_scheduled[NUM_MOTORS] = {false};
static absolute_time_t edt_enable_time[NUM_MOTORS];
static absolute_time_t next_quality_report_time;
static bool comm_timed_out = true;
#define QUALITY_WARN_THRESHOLD 5000
static bool quality_warned[NUM_MOTORS] = {false};
static uint8_t telemetry_queue[TELEMETRY_QUEUE_CAPACITY][TELEMETRY_PACKET_SIZE];
static uint16_t telemetry_queue_head = 0;
static uint16_t telemetry_queue_tail = 0;
#define QUALITY_REPORT_INTERVAL_MS 100

typedef struct {
    uint8_t controller_base_global_id;
} telemetry_context_t;

static telemetry_context_t context0 = {.controller_base_global_id = 0};
static telemetry_context_t context1 = {.controller_base_global_id = NUM_MOTORS_0};

static uint8_t calculate_checksum(const uint8_t *data, size_t len) {
    uint8_t checksum = 0;
    for (size_t i = 0; i < len; ++i) {
        checksum ^= data[i];
    }
    return checksum;
}

static uint16_t telemetry_queue_advance(uint16_t index) {
    return (uint16_t)((index + 1) % TELEMETRY_QUEUE_CAPACITY);
}

static void flush_telemetry_queue(void) {
    if (telemetry_queue_tail == telemetry_queue_head) {
        return;
    }

    while (true) {
        uint8_t packet[TELEMETRY_PACKET_SIZE];
        uint32_t irq_state = save_and_disable_interrupts();

        if (telemetry_queue_tail == telemetry_queue_head) {
            restore_interrupts(irq_state);
            break;
        }

        memcpy(packet, telemetry_queue[telemetry_queue_tail], TELEMETRY_PACKET_SIZE);
        telemetry_queue_tail = telemetry_queue_advance(telemetry_queue_tail);
        restore_interrupts(irq_state);

        fwrite(packet, 1, TELEMETRY_PACKET_SIZE, stdout);
    }

    fflush(stdout);
}

/* Map USB throttle command (0-2000) to DShot value (0, 48-2047) */
static uint16_t translate_throttle_to_dshot(uint16_t cmd_throttle) {
    if (cmd_throttle == CMD_THROTTLE_NEUTRAL) {
        return DSHOT_CMD_NEUTRAL;
    }
    if (cmd_throttle > CMD_THROTTLE_NEUTRAL && cmd_throttle <= CMD_THROTTLE_MAX_FORWARD) {
        return (cmd_throttle - CMD_THROTTLE_NEUTRAL - 1) + DSHOT_CMD_MIN_FORWARD;
    }
    if (cmd_throttle < CMD_THROTTLE_NEUTRAL && cmd_throttle >= CMD_THROTTLE_MIN_REVERSE) {
        return DSHOT_CMD_MAX_REVERSE - cmd_throttle;
    }
    return DSHOT_CMD_NEUTRAL;
}

static void send_telemetry(uint8_t motor_id, uint8_t type, int32_t value) {
    uint32_t irq_state = save_and_disable_interrupts();
    uint16_t next_head = telemetry_queue_advance(telemetry_queue_head);

    if (next_head == telemetry_queue_tail) {
        telemetry_queue_tail = telemetry_queue_advance(telemetry_queue_tail);
    }

    uint8_t *packet = telemetry_queue[telemetry_queue_head];
    packet[0] = TELEMETRY_START_BYTE;
    packet[1] = motor_id;
    packet[2] = type;
    memcpy(&packet[3], &value, 4);
    packet[TELEMETRY_PACKET_SIZE - 1] = calculate_checksum(&packet[0], TELEMETRY_PACKET_SIZE - 1);
    telemetry_queue_head = next_head;
    restore_interrupts(irq_state);
}

/* DShot telemetry callback: map internal type enum to wire format and send over USB */
static void telemetry_callback(void *context, int channel, enum dshot_telemetry_type type,
                               uint32_t value) {
    telemetry_context_t *ctx = (telemetry_context_t *)context;
    uint8_t global_motor_id = ctx->controller_base_global_id + channel;

    switch (type) {
    case DSHOT_TELEMETRY_TYPE_ERPM:
        send_telemetry(global_motor_id, TELEMETRY_TYPE_ERPM, (int32_t)value);
        break;
    case DSHOT_TELEMETRY_TYPE_VOLTAGE: {
        send_telemetry(global_motor_id, TELEMETRY_TYPE_VOLTAGE, (int32_t)value);
        char buf[40];
        int len = snprintf(buf, sizeof(buf), "EDT voltage: motor=%u val=%lu", global_motor_id,
                           (unsigned long)value);
        if (len > 0)
            log_info(buf);
        break;
    }
    case DSHOT_TELEMETRY_TYPE_TEMPERATURE: {
        send_telemetry(global_motor_id, TELEMETRY_TYPE_TEMPERATURE, (int32_t)value);
        char buf[40];
        int len = snprintf(buf, sizeof(buf), "EDT temp: motor=%u val=%lu", global_motor_id,
                           (unsigned long)value);
        if (len > 0)
            log_info(buf);
        break;
    }
    case DSHOT_TELEMETRY_TYPE_CURRENT: {
        send_telemetry(global_motor_id, TELEMETRY_TYPE_CURRENT, (int32_t)value);
        char buf[40];
        int len = snprintf(buf, sizeof(buf), "EDT current: motor=%u val=%lu", global_motor_id,
                           (unsigned long)value);
        if (len > 0)
            log_info(buf);
        break;
    }
    default:
        break;
    }
}

static bool process_usb_packet(uint8_t *usb_buf, uint16_t *thruster_values,
                               absolute_time_t *last_comm_time) {
    if (usb_buf[0] != INPUT_START_BYTE) {
        return false;
    }

    uint8_t received_checksum = usb_buf[INPUT_PACKET_SIZE - 1];
    uint8_t calculated_checksum = calculate_checksum(&usb_buf[0], INPUT_PACKET_SIZE - 1);
    if (received_checksum != calculated_checksum) {
        return false;
    }

    for (int i = 0; i < NUM_MOTORS; ++i) {
        thruster_values[i] = ((uint16_t)usb_buf[(2 * i) + 2] << 8) | usb_buf[(2 * i) + 1];
    }
    *last_comm_time = get_absolute_time();
    return true;
}

static bool quality_report_due(absolute_time_t now) {
    if (absolute_time_diff_us(next_quality_report_time, now) < 0) {
        return false;
    }

    do {
        next_quality_report_time =
            delayed_by_ms(next_quality_report_time, QUALITY_REPORT_INTERVAL_MS);
    } while (absolute_time_diff_us(next_quality_report_time, now) >= 0);

    return true;
}

static void check_timeout(absolute_time_t last_comm_time, uint16_t *thruster_values,
                          size_t *usb_idx) {
    if (absolute_time_diff_us(last_comm_time, get_absolute_time()) > COMM_TIMEOUT_MS * 1000) {
        for (int i = 0; i < NUM_MOTORS; ++i) {
            thruster_values[i] = CMD_THROTTLE_NEUTRAL;
        }
        *usb_idx = 0;
        if (!comm_timed_out) {
            comm_timed_out = true;
            log_warn("USB comm lost, motors neutral");
        }
    }
}

static bool controller_has_pending_work(const struct dshot_controller *controller) {
    for (int i = 0; i < controller->num_channels; ++i) {
        if (controller->motor[i].command_counter > 0) {
            return true;
        }
    }
    return false;
}

static void get_motor_controller(int motor_index, struct dshot_controller **ctrl, int *channel,
                                 struct dshot_controller *controller0,
                                 struct dshot_controller *controller1) {
    *ctrl = (motor_index < NUM_MOTORS_0) ? controller0 : controller1;
    *channel = (motor_index < NUM_MOTORS_0) ? motor_index : (motor_index - NUM_MOTORS_0);
}

/* Run DShot loop on both controllers until all pending commands are sent */
static void run_until_idle(struct dshot_controller *controller0,
                           struct dshot_controller *controller1) {
    while (controller_has_pending_work(controller0) || controller_has_pending_work(controller1)) {
        dshot_loop(controller0);
        dshot_loop(controller1);
    }
}

static void run_frame_cycles(struct dshot_controller *controller0,
                             struct dshot_controller *controller1, int cycles) {
    for (int i = 0; i < cycles; ++i) {
        dshot_loop(controller0);
        dshot_loop(controller1);
    }
}

/*
 * Send a DShot command to all motors with proper timing.
 * DShot commands must be sent multiple times (typically 6-10) for ESC to accept.
 * Each repetition runs a full DShot cycle with 1ms spacing between bursts.
 */
static void send_command_to_all(struct dshot_controller *controller0,
                                struct dshot_controller *controller1, uint16_t command,
                                uint8_t repeat_count) {
    sleep_us(10000);
    for (int repeat = 0; repeat < repeat_count; ++repeat) {
        for (int i = 0; i < NUM_MOTORS; ++i) {
            struct dshot_controller *ctrl;
            int channel;
            get_motor_controller(i, &ctrl, &channel, controller0, controller1);
            dshot_command(ctrl, channel, command, 1);
        }

        run_until_idle(controller0, controller1);
        flush_telemetry_queue();
        if (repeat + 1 < repeat_count) {
            sleep_us(1000);
        }
    }

    if (command == DSHOT_CMD_SAVE_SETTINGS) {
        sleep_ms(35);
    } else if (command == DSHOT_CMD_ESC_INFO) {
        sleep_ms(12);
    }
}

/*
 * Retry EDT enable for motors that haven't activated yet.
 * Only attempts when all motors are idle (ESCs require motor stop for commands).
 * Uses repeat_count=10 to send a proper command burst per motor.
 */
static void enable_edt_if_idle(const uint16_t *thruster_values,
                               struct dshot_controller *controller0,
                               struct dshot_controller *controller1) {
    absolute_time_t now = get_absolute_time();

    for (int i = 0; i < NUM_MOTORS; ++i) {
        if (thruster_values[i] != CMD_THROTTLE_NEUTRAL) {
            for (int j = 0; j < NUM_MOTORS; ++j) {
                edt_enable_scheduled[j] = false;
            }
            return;
        }
    }

    for (int i = 0; i < NUM_MOTORS; ++i) {
        struct dshot_controller *ctrl;
        int channel;
        get_motor_controller(i, &ctrl, &channel, controller0, controller1);
        struct dshot_motor *motor = &ctrl->motor[channel];

        /* Skip motors that already have EDT active (detected via bitmask) */
        if (motor->telemetry_types & DSHOT_EXTENDED_TELEMETRY_MASK) {
            edt_enable_scheduled[i] = false;
            continue;
        }

        if (!edt_enable_scheduled[i]) {
            edt_enable_scheduled[i] = true;
            edt_enable_time[i] = delayed_by_us(now, 10000);
        }

        if (motor->current_command == 0 && absolute_time_diff_us(edt_enable_time[i], now) >= 0) {
            dshot_command(ctrl, channel, DSHOT_EXTENDED_TELEMETRY_ENABLE, 10);
            edt_enable_time[i] = delayed_by_us(now, 10000);
        }
    }
}

static void send_dshot_commands(uint16_t *thruster_values, struct dshot_controller *controller0,
                                struct dshot_controller *controller1) {
    for (int i = 0; i < NUM_MOTORS; i++) {
        struct dshot_controller *ctrl;
        int channel;
        get_motor_controller(i, &ctrl, &channel, controller0, controller1);
        dshot_throttle(ctrl, channel, translate_throttle_to_dshot(thruster_values[i]));
    }
}

static void wait_for_telemetry(struct dshot_controller *controller0,
                               struct dshot_controller *controller1) {
    absolute_time_t deadline = delayed_by_ms(get_absolute_time(), 500);
    while (!dshot_is_telemetry_active(controller0) || !dshot_is_telemetry_active(controller1)) {
        dshot_loop(controller0);
        dshot_loop(controller1);
        flush_telemetry_queue();
        if (absolute_time_diff_us(get_absolute_time(), deadline) <= 0) {
            break;
        }
    }
}

static size_t poll_usb_input(uint8_t *usb_buf, size_t usb_idx) {
    int c = getchar_timeout_us(0);
    while (c != PICO_ERROR_TIMEOUT) {
        if (usb_idx == 0 && (uint8_t)c != INPUT_START_BYTE) {
            usb_idx = 0;
        } else if (usb_idx < INPUT_PACKET_SIZE) {
            usb_buf[usb_idx++] = (uint8_t)c;
        } else {
            usb_idx = 0;
        }
        c = getchar_timeout_us(0);
    }
    return usb_idx;
}

static const char *dominant_failure_name(const struct dshot_statistics *stats) {
    uint32_t max_val = stats->rx_timeout;
    const char *name = "timeout";
    if (stats->rx_bad_gcr > max_val) {
        max_val = stats->rx_bad_gcr;
        name = "bad_gcr";
    }
    if (stats->rx_bad_crc > max_val) {
        max_val = stats->rx_bad_crc;
        name = "bad_crc";
    }
    if (stats->rx_bad_type > max_val) {
        name = "bad_type";
    }
    return name;
}

static void send_quality_reports(struct dshot_controller *controller0,
                                 struct dshot_controller *controller1) {
    for (int i = 0; i < NUM_MOTORS; i++) {
        struct dshot_controller *ctrl;
        int channel;
        get_motor_controller(i, &ctrl, &channel, controller0, controller1);
        int16_t quality = dshot_get_telemetry_quality_percent(ctrl, channel);
        send_telemetry(i, TELEMETRY_TYPE_SIGNAL_QUALITY, (int32_t)quality);

        if (quality < QUALITY_WARN_THRESHOLD && !quality_warned[i]) {
            quality_warned[i] = true;
            char buf[48];
            snprintf(buf, sizeof(buf), "Motor %d: signal degraded, cause: %s", i,
                     dominant_failure_name(&ctrl->motor[channel].stats));
            log_warn(buf);
        } else if (quality > QUALITY_WARN_THRESHOLD + 1000 && quality_warned[i]) {
            quality_warned[i] = false;
        }
    }
}

int main() {
    stdio_init_all();
    log_init();

    /* Initialize two DShot controllers sharing one PIO block, one SM each */
    struct dshot_controller controller0;
    struct dshot_controller controller1;
    dshot_controller_init(&controller0, DSHOT_SPEED, DSHOT_PIO, DSHOT_SM_0, MOTOR0_PIN_BASE,
                          NUM_MOTORS_0);
    controller0.edt_always_decode = true;
    dshot_register_telemetry_cb(&controller0, telemetry_callback, &context0);
    dshot_controller_init(&controller1, DSHOT_SPEED, DSHOT_PIO, DSHOT_SM_1, MOTOR1_PIN_BASE,
                          NUM_MOTORS_1);
    controller1.edt_always_decode = true;
    dshot_register_telemetry_cb(&controller1, telemetry_callback, &context1);

    for (int i = 0; i < NUM_MOTORS; ++i) {
        thruster_values[i] = CMD_THROTTLE_NEUTRAL;
        edt_enable_time[i] = get_absolute_time();
    }
    last_comm_time = get_absolute_time();
    next_quality_report_time = get_absolute_time();

    /* Startup sequence: arm ESCs, configure 3D mode, enable EDT */
    send_dshot_commands(thruster_values, &controller0, &controller1);
    run_frame_cycles(&controller0, &controller1, NUM_MOTORS * 4);
    send_command_to_all(&controller0, &controller1, DSHOT_CMD_3D_MODE_ON, 10);
    send_command_to_all(&controller0, &controller1, DSHOT_CMD_SAVE_SETTINGS, 10);
    send_command_to_all(&controller0, &controller1, DSHOT_EXTENDED_TELEMETRY_ENABLE, 10);
    wait_for_telemetry(&controller0, &controller1);

    static uint8_t usb_buf[INPUT_PACKET_SIZE];
    static size_t usb_idx = 0;

    while (true) {
        usb_idx = poll_usb_input(usb_buf, usb_idx);

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
                        get_motor_controller(i, &ctrl, &channel, &controller0, &controller1);
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

        check_timeout(last_comm_time, thruster_values, &usb_idx);
        send_dshot_commands(thruster_values, &controller0, &controller1);
        enable_edt_if_idle(thruster_values, &controller0, &controller1);

        if (quality_report_due(get_absolute_time())) {
            send_quality_reports(&controller0, &controller1);
        }

        dshot_loop(&controller0);
        dshot_loop(&controller1);

        flush_telemetry_queue();
    }
    return 0;
}
