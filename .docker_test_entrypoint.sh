#!/bin/bash
set -e

if [ -f "build/CMakeCache.txt" ]; then
    cached_src=$(grep -E '^CMAKE_HOME_DIRECTORY:INTERNAL=' "build/CMakeCache.txt" | cut -d= -f2-)
    if [ -n "${cached_src}" ] && [ "${cached_src}" != "$(pwd)" ]; then
        rm -rf build
    fi
fi

cmake -B build -DPLATFORM=LINUX
cmake --build build
export IOLINK_DEVICE_PATH=/workspace/build/examples/host_demo/host_demo
python3 tools/virtual_master/test_automated_mandatory.py
python3 tools/virtual_master/test_pd_variable.py
