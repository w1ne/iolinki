#!/bin/bash
set -e

rm -rf build_docker

cmake -B build_docker -DPLATFORM=LINUX
cmake --build build_docker
export IOLINK_DEVICE_PATH=/workspace/build_docker/examples/host_demo/host_demo
python3 tools/virtual_master/test_automated_mandatory.py
python3 tools/virtual_master/test_pd_variable.py
