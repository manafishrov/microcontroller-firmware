project(dshot_firmware C CXX ASM)

pico_sdk_init()

add_executable(dshot main.c dshot.c)

pico_generate_pio_header(dshot ${CMAKE_CURRENT_LIST_DIR}/dshot.pio)

target_link_libraries(dshot pico_stdlib pico_stdio_usb hardware_clocks hardware_pio)

pico_add_extra_outputs(dshot)
