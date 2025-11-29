cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(microcontroller_firmware)

add_subdirectory("src-dshot")
add_subdirectory("src-pwm")
