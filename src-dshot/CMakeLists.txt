project(microcontroller_firmware C CXX ASM)

pico_sdk_init()

add_executable(microcontroller_firmware main.c dshot.c)

pico_generate_pio_header(microcontroller_firmware ${CMAKE_CURRENT_LIST_DIR}/dshot.pio)

target_link_libraries(microcontroller_firmware pico_stdlib pico_stdio_usb hardware_clocks hardware_pio)

pico_add_extra_outputs(microcontroller_firmware)
