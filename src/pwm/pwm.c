#include "pwm.h"
#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/platform_defs.h>
#include <hardware/pwm.h>
#include <hardware/structs/clocks.h>
#include <hardware/structs/io_bank0.h>
#include <pico/types.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

void pwm_controller_init(struct pwm_controller *controller, uint *pins, uint num_channels) {
    memset(controller, 0, sizeof(*controller));
    controller->num_channels = num_channels;

    uint32_t clock_hz = clock_get_hz(clk_sys);
    float divider = (float)clock_hz / (float)(PWM_FREQUENCY * PWM_STEPS);
    bool slice_configured[NUM_PWM_SLICES] = {false};

    for (uint i = 0; i < num_channels; ++i) {
        controller->pin[i] = pins[i];
        gpio_set_function(pins[i], GPIO_FUNC_PWM);

        uint slice = pwm_gpio_to_slice_num(pins[i]);
        controller->slice[i] = slice;

        if (!slice_configured[slice]) {
            pwm_set_clkdiv(slice, divider);
            pwm_set_wrap(slice, PWM_WRAP);
            pwm_set_enabled(slice, true);
            slice_configured[slice] = true;
        }
    }
}

void pwm_controller_deinit(struct pwm_controller *controller) {
    for (uint i = 0; i < controller->num_channels; ++i) {
        uint slice = controller->slice[i];
        pwm_set_enabled(slice, false);
        gpio_set_function(controller->pin[i], GPIO_FUNC_SIO);
        gpio_set_dir(controller->pin[i], GPIO_OUT);
        gpio_put(controller->pin[i], 0);
    }
}

void pwm_set_throttle(struct pwm_controller *controller, uint channel, uint value) {
    if (channel >= controller->num_channels) {
        return;
    }

    if (value > PWM_WRAP) {
        value = PWM_WRAP;
    }

    pwm_set_gpio_level(controller->pin[channel], value);
}
