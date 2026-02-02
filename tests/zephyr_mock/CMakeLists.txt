cmake_minimum_required(VERSION 3.20)
project(zephyr_verification_test)

# Load the Mock Definitions immediately so they are available for the 'module'
include(${CMAKE_CURRENT_SOURCE_DIR}/ZephyrConfig.cmake)

# 1. Verify Module
message(STATUS "Testing Module CMakeLists...")
add_subdirectory(../../zephyr build_module)

# 2. Verify App
# App does find_package(Zephyr), so we ensure it finds our config.
set(Zephyr_DIR ${CMAKE_CURRENT_SOURCE_DIR})
message(STATUS "Testing Example App CMakeLists...")
add_subdirectory(../../examples/zephyr_app build_app)
