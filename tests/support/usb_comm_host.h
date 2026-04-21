#ifndef TESTS_SUPPORT_USB_COMM_HOST_H
#define TESTS_SUPPORT_USB_COMM_HOST_H

#include "../mocks/pico/types.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define USB_INPUT_START_BYTE 0x5A
#define USB_CONFIG_START_BYTE 0xC5
#define USB_INPUT_PACKET_SIZE(num_motors) (1 + ((num_motors) * 2) + 1)
#define USB_COMM_TIMEOUT_MS 200

typedef enum {
    USB_PACKET_NONE = 0,
    USB_PACKET_COMMAND,
    USB_PACKET_CONFIG,
} usb_packet_kind_t;

uint8_t usb_calculate_checksum(const uint8_t *data, size_t len);
usb_packet_kind_t usb_poll_multi(uint8_t *command_buf, size_t command_packet_size,
                                 size_t *command_idx, uint8_t *config_buf,
                                 size_t config_packet_size, size_t *config_idx);
bool usb_parse_packet(const uint8_t *usb_buf, size_t packet_size, uint16_t *raw_values,
                      int num_motors, absolute_time_t *last_comm_time);
void usb_check_timeout(absolute_time_t last_comm_time, uint16_t *thruster_values, int num_motors,
                       uint16_t neutral_value, size_t *usb_idx, bool *comm_timed_out);

#endif
