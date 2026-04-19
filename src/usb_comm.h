#ifndef USB_COMM_H
#define USB_COMM_H

#include <pico/time.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define USB_INPUT_START_BYTE 0x5A
#define USB_INPUT_PACKET_SIZE(num_motors) (1 + ((num_motors) * 2) + 1)
#define USB_COMM_TIMEOUT_MS 200

uint8_t usb_calculate_checksum(const uint8_t *data, size_t len);
size_t usb_poll_input(uint8_t *buf, size_t packet_size, size_t usb_idx);
bool usb_parse_packet(const uint8_t *usb_buf, size_t packet_size, uint16_t *raw_values,
                      int num_motors, absolute_time_t *last_comm_time);
void usb_check_timeout(absolute_time_t last_comm_time, uint16_t *thruster_values, int num_motors,
                       uint16_t neutral_value, size_t *usb_idx, bool *comm_timed_out);

#endif
