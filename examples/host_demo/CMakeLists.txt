cmake_minimum_required(VERSION 3.20)

project(host_demo C)

add_executable(host_demo src/main.c)
target_link_libraries(host_demo iolinki)
