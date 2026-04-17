/*
 * DShot protocol implementation for RP2040 PIO with bidirectional telemetry.
 * Derived from pio-dshot by Simon Wunderlich.
 * Original repository: https://github.com/simonwunderlich/pio-dshot
 * Licensed under the MIT License.
 *
 * EDT handling follows Betaflight's dshot.c:
 * https://github.com/betaflight/betaflight/blob/master/src/main/drivers/dshot.c
 */

#include "dshot.h"
#include "dshot.pio.h"
#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/pio.h>
#include <hardware/structs/clocks.h>
#include <pico/time.h>
#include <pico/types.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Track PIO program state per PIO block (pio0/pio1) */
static bool dshot_pio_prog_loaded[2] = {false, false};
static uint dshot_pio_prog_offset[2] = {0, 0};

static uint pio_index(PIO pio) {
    return pio == pio0 ? 0 : 1;
}

/*
 * GCR (5-bit) to nibble (4-bit) decoding table.
 * Bidirectional DShot telemetry uses GCR encoding: 4 nibbles encoded as
 * 4 x 5-bit GCR symbols = 20 bits transmitted. 0xFF marks invalid symbols.
 */
static const uint8_t gcr_table[32] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x09, 0x0A, 0x0B, 0xFF, 0x0D, 0x0E, 0x0F,
    0xFF, 0xFF, 0x02, 0x03, 0xFF, 0x05, 0x06, 0x07, 0xFF, 0x00, 0x08, 0x01, 0xFF, 0x04, 0x0C, 0xFF,
};

/*
 * EDT type lookup table, indexed by telemetry_type >> 1.
 * Matches Betaflight's extendedTelemetryLookup[]. The telemetry_type field
 * is 4 bits (bits 11:8 of the 12-bit value). Dropping the tag bit (bit 0)
 * gives a 3-bit index into this table.
 *
 * EDT prefix → index:
 *   0x00 >> 1 = 0  eRPM (handled by conditional, never looked up)
 *   0x02 >> 1 = 1  Temperature (degrees C)
 *   0x04 >> 1 = 2  Voltage (0.25V per LSB, range 0-63.75V)
 *   0x06 >> 1 = 3  Current (1A per LSB, range 0-255A)
 *   0x08 >> 1 = 4  Debug 1
 *   0x0A >> 1 = 5  Debug 2
 *   0x0C >> 1 = 6  Debug 3 (stress level in EDT v2.0.0+)
 *   0x0E >> 1 = 7  State / Events
 */
static const enum dshot_telemetry_type edt_type_lookup[8] = {
    DSHOT_TELEMETRY_TYPE_ERPM,    DSHOT_TELEMETRY_TYPE_TEMPERATURE,  DSHOT_TELEMETRY_TYPE_VOLTAGE,
    DSHOT_TELEMETRY_TYPE_CURRENT, DSHOT_TELEMETRY_TYPE_DEBUG1,       DSHOT_TELEMETRY_TYPE_DEBUG2,
    DSHOT_TELEMETRY_TYPE_DEBUG3,  DSHOT_TELEMETRY_TYPE_STATE_EVENTS,
};

/* Configure PIO state machine pin for the given channel */
static void dshot_sm_config_set_pin(struct dshot_controller *controller, int pin) {
    sm_config_set_out_pins(&controller->c, pin, 1);
    sm_config_set_set_pins(&controller->c, pin, 1);
    sm_config_set_in_pins(&controller->c, pin);
    sm_config_set_jmp_pin(&controller->c, pin);

    pio_gpio_init(controller->pio, pin);
    gpio_set_pulls(pin, true, false);
}

void dshot_controller_init(struct dshot_controller *controller, uint16_t dshot_speed, PIO pio,
                           uint8_t sm, int pin, int channels) {
    memset(controller, 0, sizeof(*controller));
    controller->pio = pio;
    controller->sm = sm;
    controller->num_channels = channels;
    controller->speed = dshot_speed;
    controller->pin = pin;
    controller->command_last_time = get_absolute_time();

    /* Invalidate throttle cache so first dshot_throttle call always computes frame */
    for (int i = 0; i < controller->num_channels; i++) {
        controller->motor[i].last_throttle_value = UINT16_MAX;
        dshot_throttle(controller, i, 0);
    }

    uint pi = pio_index(pio);
    if (!dshot_pio_prog_loaded[pi]) {
        dshot_pio_prog_offset[pi] = pio_add_program(pio, &pio_dshot_program);
        dshot_pio_prog_loaded[pi] = true;
    }

    controller->c = pio_dshot_program_get_default_config(dshot_pio_prog_offset[pi]);

    /* MSB-first shift for both TX (out) and RX (in), no autopush/pull */
    sm_config_set_out_shift(&controller->c, false, false, 32);
    sm_config_set_in_shift(&controller->c, false, false, 32);

    dshot_sm_config_set_pin(controller, pin);

    /* Clock divider: 40 PIO cycles per DShot bit */
    float clkdiv = (float)clock_get_hz(clk_sys) / (1000.0F * (float)dshot_speed * 40.0F);
    sm_config_set_clkdiv(&controller->c, clkdiv);

    pio_sm_init(pio, sm, dshot_pio_prog_offset[pi], &controller->c);
    pio_sm_set_enabled(pio, sm, true);
}

void dshot_register_telemetry_cb(struct dshot_controller *controller,
                                 dshot_telemetry_callback_t telemetry_cb, void *context) {
    controller->telemetry_cb = telemetry_cb;
    controller->telemetry_cb_context = context;
}

/*
 * Decode eRPM from the 12-bit telemetry value (format: eeem mmmm mmmm).
 * Returns eRPM / 100 (each LSB = 100 eRPM), 0 for motor stopped,
 * or DSHOT_TELEMETRY_INVALID if the period is zero.
 */
static uint32_t dshot_decode_erpm_telemetry_value(uint16_t value) {
    if (value == 0x0FFF) {
        return 0;
    }

    /* 3-bit exponent (bits 11:9), 9-bit mantissa (bits 8:0) */
    uint16_t period = (value & 0x01FF) << ((value & 0xFE00) >> 9);
    if (period == 0) {
        return DSHOT_TELEMETRY_INVALID;
    }

    /* Convert period to eRPM * 100 with rounding: 60_000_000 / 100 = 600_000 */
    return (600000 + period / 2) / period;
}

/*
 * Decode telemetry value into type + decoded value.
 * EDT detection follows Betaflight: enabled when edt_always_decode is set
 * or when any EDT type has been previously received (bitmask check).
 *
 * EDT frame discrimination (4-bit type field, bits 11:8):
 *   - Bit 0 = 1 (odd) -> always eRPM
 *   - Type = 0x00     -> always eRPM
 *   - Bit 0 = 0, type != 0 -> EDT frame, type >> 1 indexes edt_type_lookup
 */
static void dshot_decode_telemetry_value(struct dshot_controller *controller, uint16_t raw_value,
                                         uint32_t *decoded, enum dshot_telemetry_type *type) {
    struct dshot_motor *motor = &controller->motor[controller->channel];
    bool edt_active = controller->edt_always_decode ||
                      (motor->telemetry_types & DSHOT_EXTENDED_TELEMETRY_MASK) != 0;

    /* Extract 4-bit type field: 3 bits type + 1 bit eRPM/EDT tag */
    unsigned telemetry_type = (raw_value & 0x0F00) >> 8;
    bool is_erpm = !edt_active || (telemetry_type & 0x01) || (telemetry_type == 0);

    if (is_erpm) {
        *decoded = dshot_decode_erpm_telemetry_value(raw_value);
        *type = DSHOT_TELEMETRY_TYPE_ERPM;
    } else {
        unsigned type_index = telemetry_type >> 1;
        *type = edt_type_lookup[type_index];
        *decoded = raw_value & 0x00FF;
    }
}

/* Update stored telemetry data and type bitmask */
static void dshot_update_telemetry_data(struct dshot_motor *motor, enum dshot_telemetry_type type,
                                        uint32_t value) {
    motor->telemetry_data[type] = value;
    motor->telemetry_types |= (1 << type);

    if (type == DSHOT_TELEMETRY_TYPE_TEMPERATURE && value > motor->max_temp) {
        motor->max_temp = value;
    }
}

/*
 * Update windowed telemetry quality statistics.
 * Uses a rotating bucket array to maintain a sliding 600ms window.
 */
static void dshot_update_telemetry_quality(struct dshot_telemetry_quality *quality,
                                           bool packet_valid, uint32_t current_ms) {
    uint8_t bucket_index =
        (current_ms / DSHOT_TELEMETRY_QUALITY_BUCKET_MS) % DSHOT_TELEMETRY_QUALITY_BUCKET_COUNT;

    if (bucket_index != quality->last_bucket_index) {
        quality->packet_count_sum -= quality->packet_count_array[bucket_index];
        quality->invalid_count_sum -= quality->invalid_count_array[bucket_index];
        quality->packet_count_array[bucket_index] = 0;
        quality->invalid_count_array[bucket_index] = 0;
        quality->last_bucket_index = bucket_index;
    }

    quality->packet_count_sum++;
    quality->packet_count_array[bucket_index]++;
    if (!packet_valid) {
        quality->invalid_count_sum++;
        quality->invalid_count_array[bucket_index]++;
    }
}

/*
 * Process received telemetry from PIO.
 * Decodes GCR, verifies CRC, extracts telemetry type/value, updates state.
 *
 * Bidirectional DShot response: 21 bits from PIO (1 start + 20 GCR data).
 * The 20 GCR bits decode to 4 nibbles = 16 bits (12-bit data + 4-bit CRC).
 */
static void dshot_receive(struct dshot_controller *controller, uint32_t value) {
    struct dshot_motor *motor = &controller->motor[controller->channel];
    uint32_t now_ms = to_ms_since_boot(get_absolute_time());

    if (value == 0) {
        motor->stats.rx_timeout++;
        dshot_update_telemetry_quality(&motor->quality, false, now_ms);
        return;
    }

    /* Invert (bidirectional DShot uses inverted output) and decode NRZI→GCR */
    value = ~value & 0x1FFFFF;
    uint32_t gcr = value ^ (value >> 1);

    /* Decode 4 GCR symbols (5 bits each) into 4 nibbles */
    uint8_t n3 = gcr_table[(gcr >> 15) & 0x1F];
    uint8_t n2 = gcr_table[(gcr >> 10) & 0x1F];
    uint8_t n1 = gcr_table[(gcr >> 5) & 0x1F];
    uint8_t n0 = gcr_table[gcr & 0x1F];

    if ((n3 | n2 | n1 | n0) & 0xF0) {
        motor->stats.rx_bad_gcr++;
        dshot_update_telemetry_quality(&motor->quality, false, now_ms);
        return;
    }

    uint16_t frame = (n3 << 12) | (n2 << 8) | (n1 << 4) | n0;

    /* CRC: inverted XOR of nibbles (bidirectional DShot always uses inverted CRC) */
    uint16_t crc = ~((frame >> 4) ^ (frame >> 8) ^ (frame >> 12)) & 0x0F;
    if (crc != (frame & 0x0F)) {
        motor->stats.rx_bad_crc++;
        dshot_update_telemetry_quality(&motor->quality, false, now_ms);
        return;
    }

    uint16_t raw_value = frame >> 4;

    /* Decode telemetry type and value */
    enum dshot_telemetry_type type;
    uint32_t decoded;
    dshot_decode_telemetry_value(controller, raw_value, &decoded, &type);

    if (decoded == DSHOT_TELEMETRY_INVALID) {
        motor->stats.rx_bad_type++;
        dshot_update_telemetry_quality(&motor->quality, false, now_ms);
        return;
    }

    motor->stats.rx_frames++;
    dshot_update_telemetry_data(motor, type, decoded);
    dshot_update_telemetry_quality(&motor->quality, true, now_ms);

    if (controller->telemetry_cb) {
        controller->telemetry_cb(controller->telemetry_cb_context, controller->channel, type,
                                 decoded);
    }
}

/* Switch PIO state machine to the next channel (round-robin multiplexing) */
static void dshot_cycle_channel(struct dshot_controller *controller) {
    pio_sm_set_enabled(controller->pio, controller->sm, false);

    controller->channel = (controller->channel + 1) % controller->num_channels;
    dshot_sm_config_set_pin(controller, controller->pin + controller->channel);
    pio_sm_init(controller->pio, controller->sm, dshot_pio_prog_offset[pio_index(controller->pio)],
                &controller->c);

    pio_sm_set_enabled(controller->pio, controller->sm, true);
}

/* Begin async DShot frame transmission for the current channel */
void dshot_loop_async_start(struct dshot_controller *controller) {
    if (controller->num_channels > 1) {
        dshot_cycle_channel(controller);
    }

    struct dshot_motor *motor = &controller->motor[controller->channel];
    if (pio_sm_is_tx_fifo_empty(controller->pio, controller->sm)) {
        motor->stats.tx_frames++;
        /* Frame is inverted for PIO (active-low signaling with pull-up) */
        pio_sm_put(controller->pio, controller->sm, ~(uint32_t)motor->frame << 16);
        /* Wait cycles between TX and RX: 25us at DShot bit rate */
        uint32_t cycles = (25 * controller->speed * 40) / 1000;
        pio_sm_put(controller->pio, controller->sm, cycles);
    }
}

/* Complete async DShot cycle: receive telemetry and manage command state */
void dshot_loop_async_complete(struct dshot_controller *controller) {
    uint32_t recv = pio_sm_get_blocking(controller->pio, controller->sm);
    dshot_receive(controller, recv);

    /* Decrement command counter; revert to throttle when command sequence completes */
    struct dshot_motor *motor = &controller->motor[controller->channel];
    if (motor->command_counter > 0) {
        motor->command_counter--;
        if (motor->command_counter == 0) {
            motor->frame = motor->last_throttle_frame;
            motor->current_command = 0;
        }
    }

    /* Safety: zero all channels if no commands received within idle threshold */
    if (absolute_time_diff_us(controller->command_last_time, get_absolute_time()) >
        DSHOT_IDLE_THRESHOLD) {
        for (int i = 0; i < controller->num_channels; i++) {
            dshot_throttle(controller, i, 0);
        }
    }
}

/* Synchronous DShot loop: transmit frame and receive telemetry for one channel */
void dshot_loop(struct dshot_controller *controller) {
    dshot_loop_async_start(controller);
    dshot_loop_async_complete(controller);
}

void dshot_mark_activity(struct dshot_controller *controller) {
    controller->command_last_time = get_absolute_time();
}

/*
 * Compute a 16-bit DShot frame from throttle/command value.
 * Format: [11-bit value][1-bit telemetry][4-bit CRC]
 * CRC is inverted for bidirectional DShot (signals ESC to respond on same wire).
 */
static uint16_t dshot_compute_frame(uint16_t throttle, int telemetry) {
    uint16_t value = (throttle << 1) | telemetry;

    uint16_t crc = value ^ (value >> 4) ^ (value >> 8);
    crc = ~crc;

    return (value << 4) | (crc & 0x0F);
}

/* Queue a DShot command to be sent for repeat_count consecutive frames */
void dshot_command(struct dshot_controller *controller, uint16_t channel, uint16_t command,
                   uint8_t repeat_count) {
    if (channel >= controller->num_channels) {
        return;
    }

    struct dshot_motor *motor = &controller->motor[channel];

    /* Commands use telemetry bit = 1 */
    motor->frame = dshot_compute_frame(command, 1);
    motor->current_command = command;
    motor->command_counter = repeat_count;

    /* Clear EDT state when EDT is explicitly disabled */
    if (command == DSHOT_EXTENDED_TELEMETRY_DISABLE) {
        motor->telemetry_types = 0;
    }

    dshot_mark_activity(controller);
}

/* Set throttle value for a channel. Deferred while a command sequence is active. */
void dshot_throttle(struct dshot_controller *controller, uint16_t channel, uint16_t throttle) {
    if (channel >= controller->num_channels) {
        return;
    }

    struct dshot_motor *motor = &controller->motor[channel];

    if (motor->last_throttle_value == throttle) {
        if (motor->command_counter == 0) {
            motor->frame = motor->last_throttle_frame;
        }
        return;
    }

    motor->last_throttle_value = throttle;
    motor->last_throttle_frame = dshot_compute_frame(throttle, 0);
    if (motor->command_counter == 0) {
        motor->frame = motor->last_throttle_frame;
    }
}

bool dshot_is_telemetry_active(const struct dshot_controller *controller) {
    for (int i = 0; i < controller->num_channels; i++) {
        if (!(controller->motor[i].telemetry_types & (1 << DSHOT_TELEMETRY_TYPE_ERPM))) {
            return false;
        }
    }
    return controller->num_channels > 0;
}

int16_t dshot_get_telemetry_invalid_percent(const struct dshot_controller *controller,
                                            uint8_t channel) {
    if (channel >= controller->num_channels) {
        return 10000;
    }
    const struct dshot_motor *motor = &controller->motor[channel];
    if (!(motor->telemetry_types & (1 << DSHOT_TELEMETRY_TYPE_ERPM))) {
        return 10000;
    }
    if (motor->quality.packet_count_sum == 0) {
        return 0;
    }
    return (int16_t)(motor->quality.invalid_count_sum * 10000 / motor->quality.packet_count_sum);
}
