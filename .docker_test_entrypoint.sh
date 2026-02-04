#!/bin/bash
set -e

cmake -B build -DPLATFORM=LINUX
cmake --build build
export IOLINK_DEVICE_PATH=/workspace/build/examples/host_demo/host_demo
python3 tools/virtual_master/test_automated_mandatory.py
python3 tools/virtual_master/test_pd_variable.py
