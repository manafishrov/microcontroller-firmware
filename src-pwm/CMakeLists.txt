project(pwm_firmware C CXX ASM)

pico_sdk_init()

add_executable(pwm main.c pwm.c)

target_link_libraries(pwm pico_stdlib pico_stdio_usb hardware_clocks hardware_pwm)

pico_add_extra_outputs(pwm)

