#!/bin/bash
set -e

rm -rf build_docker

cmake -B build_docker -DPLATFORM=LINUX
cmake --build build_docker

# Run Unit Tests
cd build_docker
ctest --output-on-failure
cd ..

# Run Bare Metal Build Verification
echo -e "\nRunning Bare Metal Build Verification..."
rm -rf build_bare
cmake -B build_bare -DIOLINK_PLATFORM=BAREMETAL
cmake --build build_bare
export IOLINK_DEVICE_PATH=/workspace/build_docker/examples/host_demo/host_demo
python3 tools/virtual_master/test_automated_mandatory.py
python3 tools/virtual_master/test_pd_variable.py
