# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(stm32u5_tiol112)
get_filename_component(STACK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
target_include_directories(app PRIVATE ${STACK_ROOT}/include ${STACK_ROOT}/examples/reference_device)
# Deliberately exclude module's Zephyr PHY and millisecond-derived clock.
target_sources(app PRIVATE src/main.c src/adapter.c ${STACK_ROOT}/examples/reference_device/reference_device.c)
foreach(core device crc frame dll isdu events data_storage params device_info phy_generic phy_tiol112)
 target_sources(app PRIVATE ${STACK_ROOT}/src/${core}.c)
endforeach()
