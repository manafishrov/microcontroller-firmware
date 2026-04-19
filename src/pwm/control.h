#ifndef PWM_CONTROL_H
#define PWM_CONTROL_H

#include "../usb_comm.h"
#include "pwm.h"
#include <pico/time.h>
#include <stdbool.h>
#include <stdint.h>

#define NUM_MOTORS 8

#define PWM_MIN 1000
#define PWM_NEUTRAL 1500
#define PWM_MAX 2000
#define PWM_CMD_RANGE_MAX 2000

#define INPUT_PACKET_SIZE USB_INPUT_PACKET_SIZE(NUM_MOTORS)

uint16_t pwm_translate_throttle(uint16_t cmd);
bool pwm_process_usb_packet(uint8_t *usb_buf, uint16_t *thruster_values,
                            absolute_time_t *last_comm_time);

#endif
