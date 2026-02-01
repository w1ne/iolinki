cmake_minimum_required(VERSION 3.10)
project(bare_metal_app C)

set(CMAKE_C_STANDARD 99)

add_executable(bare_metal_app src/main.c)
target_link_libraries(bare_metal_app iolinki)
