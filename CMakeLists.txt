cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(microcontroller_firmware)

if(NOT DEFINED FIRMWARE_TYPE)
  message(FATAL_ERROR "You must specify -DFIRMWARE_TYPE=dshot or -DFIRMWARE_TYPE=pwm when running cmake.")
endif()

if(FIRMWARE_TYPE STREQUAL "dshot")
  add_subdirectory("src-dshot")
elseif(FIRMWARE_TYPE STREQUAL "pwm")
  add_subdirectory("src-pwm")
else()
  message(FATAL_ERROR "Unknown FIRMWARE_TYPE: ${FIRMWARE_TYPE}. Use 'dshot' or 'pwm'.")
endif()
