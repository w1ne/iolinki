cmake_minimum_required(VERSION 3.10)
project(iolinki C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

# Code Coverage Support
option(ENABLE_COVERAGE "Enable code coverage instrumentation" OFF)
if(ENABLE_COVERAGE)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --coverage -fprofile-arcs -ftest-coverage")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
    endif()
endif()

# Include directories
include_directories(include)

# Core Library
set(IOLINKI_SOURCES
    src/iolink_core.c
)
add_library(iolinki STATIC
    src/iolink_core.c
    src/phy_virtual.c
    src/crc.c
    src/dll.c
    src/isdu.c
    src/events.c
    src/data_storage.c
    src/time_utils.c
)

# Examples
add_subdirectory(examples/simple_device)
add_subdirectory(examples/host_demo)

# Testing
option(BUILD_TESTING "Build unit tests" ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# Documentation
find_package(Doxygen)
if(DOXYGEN_FOUND)
    add_custom_target(doc
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )
endif()

# IODD Generation
find_package(Python3)
if(Python3_FOUND)
    add_custom_target(iodd
        COMMAND ${Python3_EXECUTABLE} tools/iodd_gen.py examples/simple_device/simple_device.json
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating IODD XML for simple_device"
        VERBATIM
    )
endif()
