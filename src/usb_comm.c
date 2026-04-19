#include "usb_comm.h"
#include "log.h"
#include <pico/error.h>
#include <pico/stdio.h>

uint8_t usb_calculate_checksum(const uint8_t *data, size_t len) {
    uint8_t checksum = 0;
    for (size_t i = 0; i < len; ++i) {
        checksum ^= data[i];
    }
    return checksum;
}

usb_packet_kind_t usb_poll_multi(uint8_t *command_buf, size_t command_packet_size,
                                 size_t *command_idx, uint8_t *config_buf,
                                 size_t config_packet_size, size_t *config_idx) {
    int c = getchar_timeout_us(0);
    while (c != PICO_ERROR_TIMEOUT) {
        uint8_t byte = (uint8_t)c;

        if (*command_idx > 0) {
            if (*command_idx < command_packet_size) {
                command_buf[(*command_idx)++] = byte;
            } else {
                *command_idx = 0;
            }

            if (*command_idx >= command_packet_size) {
                return USB_PACKET_COMMAND;
            }
        } else if (*config_idx > 0) {
            if (*config_idx < config_packet_size) {
                config_buf[(*config_idx)++] = byte;
            } else {
                *config_idx = 0;
            }

            if (*config_idx >= config_packet_size) {
                return USB_PACKET_CONFIG;
            }
        } else if (byte == USB_INPUT_START_BYTE) {
            command_buf[0] = byte;
            *command_idx = 1;
        } else if (byte == USB_CONFIG_START_BYTE) {
            config_buf[0] = byte;
            *config_idx = 1;
        }

        c = getchar_timeout_us(0);
    }
    return USB_PACKET_NONE;
}

bool usb_parse_packet(const uint8_t *usb_buf, size_t packet_size, uint16_t *raw_values,
                      int num_motors, absolute_time_t *last_comm_time) {
    if (usb_buf[0] != USB_INPUT_START_BYTE) {
        return false;
    }

    uint8_t received_checksum = usb_buf[packet_size - 1];
    uint8_t calculated_checksum = usb_calculate_checksum(&usb_buf[0], packet_size - 1);
    if (received_checksum != calculated_checksum) {
        return false;
    }

    for (int i = 0; i < num_motors; ++i) {
        raw_values[i] = ((uint16_t)usb_buf[(2 * i) + 2] << 8) | usb_buf[(2 * i) + 1];
    }
    *last_comm_time = get_absolute_time();
    return true;
}

void usb_check_timeout(absolute_time_t last_comm_time, uint16_t *thruster_values, int num_motors,
                       uint16_t neutral_value, size_t *usb_idx, bool *comm_timed_out) {
    if (absolute_time_diff_us(last_comm_time, get_absolute_time()) > USB_COMM_TIMEOUT_MS * 1000) {
        for (int i = 0; i < num_motors; ++i) {
            thruster_values[i] = neutral_value;
        }
        *usb_idx = 0;
        if (!*comm_timed_out) {
            *comm_timed_out = true;
            log_warn("USB comm lost, motors neutral");
        }
    }
}
