#include "telemetry_usb.h"
#include "../usb_comm.h"
#include "dshot.h"
#include <hardware/sync.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TELEMETRY_QUEUE_CAPACITY 256
#define TELEMETRY_FLUSH_BATCH_PACKETS 16
#define TELEMETRY_BATCH_HEADER_SIZE 2
#define TELEMETRY_BATCH_FOOTER_SIZE 1

typedef struct {
    uint8_t motor_id;
    uint8_t type;
    int32_t value;
} telemetry_queue_entry_t;

static telemetry_queue_entry_t telemetry_queue[TELEMETRY_QUEUE_CAPACITY];
static uint16_t telemetry_queue_head = 0;
static uint16_t telemetry_queue_tail = 0;

static uint16_t telemetry_queue_advance(uint16_t index) {
    return (uint16_t)((index + 1) % TELEMETRY_QUEUE_CAPACITY);
}

void dshot_telemetry_usb_init(void) {
    telemetry_queue_head = 0;
    telemetry_queue_tail = 0;
}

void dshot_telemetry_usb_reset(void) {
    telemetry_queue_head = 0;
    telemetry_queue_tail = 0;
}

void dshot_telemetry_usb_flush(void) {
    if (telemetry_queue_tail == telemetry_queue_head) {
        return;
    }

    while (true) {
        uint8_t batch[TELEMETRY_BATCH_HEADER_SIZE +
                      (TELEMETRY_FLUSH_BATCH_PACKETS * TELEMETRY_BATCH_ENTRY_SIZE) +
                      TELEMETRY_BATCH_FOOTER_SIZE];
        size_t batch_len = 0;
        uint32_t irq_state = save_and_disable_interrupts();

        if (telemetry_queue_tail == telemetry_queue_head) {
            restore_interrupts(irq_state);
            break;
        }

        batch[0] = TELEMETRY_BATCH_START_BYTE;
        batch[1] = 0;
        batch_len = TELEMETRY_BATCH_HEADER_SIZE;

        while (telemetry_queue_tail != telemetry_queue_head &&
               batch[1] < TELEMETRY_FLUSH_BATCH_PACKETS &&
               batch_len + TELEMETRY_BATCH_ENTRY_SIZE + TELEMETRY_BATCH_FOOTER_SIZE <=
                   sizeof(batch)) {
            const telemetry_queue_entry_t *entry = &telemetry_queue[telemetry_queue_tail];
            batch[batch_len] = entry->motor_id;
            batch[batch_len + 1] = entry->type;
            memcpy(&batch[batch_len + 2], &entry->value, sizeof(entry->value));
            batch_len += TELEMETRY_BATCH_ENTRY_SIZE;
            batch[1]++;
            telemetry_queue_tail = telemetry_queue_advance(telemetry_queue_tail);
        }

        restore_interrupts(irq_state);

        batch[batch_len] = usb_calculate_checksum(batch, batch_len);
        batch_len += TELEMETRY_BATCH_FOOTER_SIZE;
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

    telemetry_queue_entry_t *entry = &telemetry_queue[telemetry_queue_head];
    entry->motor_id = motor_id;
    entry->type = type;
    entry->value = value;
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
