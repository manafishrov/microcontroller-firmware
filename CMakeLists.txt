cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(microcontroller_firmware)

pico_sdk_init()

set(PWM_EXE_NAME pwm)

add_executable(${PWM_EXE_NAME} src/pwm/main.c src/pwm/pwm.c)

target_link_libraries(${PWM_EXE_NAME} pico_stdlib pico_stdio_usb hardware_clocks hardware_pwm)

pico_add_extra_outputs(${PWM_EXE_NAME})

set(DSHOT_EXE_NAME dshot)

add_executable(${DSHOT_EXE_NAME} src/dshot/main.c src/dshot/dshot.c)

pico_generate_pio_header(${DSHOT_EXE_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/src/dshot/dshot.pio)

target_link_libraries(${DSHOT_EXE_NAME} pico_stdlib pico_stdio_usb hardware_clocks hardware_pio)

pico_add_extra_outputs(${DSHOT_EXE_NAME})
