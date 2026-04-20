#include "telemetry_usb.h"
#include "../usb_comm.h"
#include <hardware/sync.h>
#include <stdio.h>
#include <string.h>

#define TELEMETRY_QUEUE_CAPACITY 256
#define TELEMETRY_FLUSH_BATCH_PACKETS 16

static uint8_t telemetry_queue[TELEMETRY_QUEUE_CAPACITY][TELEMETRY_PACKET_SIZE];
static uint16_t telemetry_queue_head = 0;
static uint16_t telemetry_queue_tail = 0;

static uint16_t telemetry_queue_advance(uint16_t index) {
    return (uint16_t)((index + 1) % TELEMETRY_QUEUE_CAPACITY);
}

void dshot_telemetry_usb_init(void) {
    telemetry_queue_head = 0;
    telemetry_queue_tail = 0;
}

void dshot_telemetry_usb_flush(void) {
    if (telemetry_queue_tail == telemetry_queue_head) {
        return;
    }

    while (true) {
        uint8_t batch[TELEMETRY_FLUSH_BATCH_PACKETS * TELEMETRY_PACKET_SIZE];
        size_t batch_len = 0;
        uint32_t irq_state = save_and_disable_interrupts();

        if (telemetry_queue_tail == telemetry_queue_head) {
            restore_interrupts(irq_state);
            break;
        }

        while (telemetry_queue_tail != telemetry_queue_head &&
               batch_len + TELEMETRY_PACKET_SIZE <= sizeof(batch)) {
            memcpy(&batch[batch_len], telemetry_queue[telemetry_queue_tail], TELEMETRY_PACKET_SIZE);
            batch_len += TELEMETRY_PACKET_SIZE;
            telemetry_queue_tail = telemetry_queue_advance(telemetry_queue_tail);
        }

        restore_interrupts(irq_state);

        fwrite(batch, 1, batch_len, stdout);
    }

    fflush(stdout);
}

void dshot_telemetry_usb_send(uint8_t motor_id, uint8_t type, int32_t value) {
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
    packet[TELEMETRY_PACKET_SIZE - 1] =
        usb_calculate_checksum(&packet[0], TELEMETRY_PACKET_SIZE - 1);
    telemetry_queue_head = next_head;
    restore_interrupts(irq_state);
}

void dshot_telemetry_callback(void *context, int channel, enum dshot_telemetry_type type,
                              uint32_t value) {
    dshot_telemetry_context_t *ctx = (dshot_telemetry_context_t *)context;
    uint8_t global_motor_id = ctx->controller_base_global_id + channel;

    switch (type) {
    case DSHOT_TELEMETRY_TYPE_ERPM:
        dshot_telemetry_usb_send(global_motor_id, TELEMETRY_TYPE_ERPM, (int32_t)value);
        break;
    case DSHOT_TELEMETRY_TYPE_VOLTAGE: {
        dshot_telemetry_usb_send(global_motor_id, TELEMETRY_TYPE_VOLTAGE, (int32_t)value);
        break;
    }
    case DSHOT_TELEMETRY_TYPE_TEMPERATURE: {
        dshot_telemetry_usb_send(global_motor_id, TELEMETRY_TYPE_TEMPERATURE, (int32_t)value);
        break;
    }
    case DSHOT_TELEMETRY_TYPE_CURRENT: {
        dshot_telemetry_usb_send(global_motor_id, TELEMETRY_TYPE_CURRENT, (int32_t)value);
        break;
    }
    default:
        break;
    }
}
