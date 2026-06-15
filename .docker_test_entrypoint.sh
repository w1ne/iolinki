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
# The virtual-master integration tests drive the device over a PTY and are
# timing-sensitive; bound each so a stuck PTY fails fast instead of hanging the
# whole CI run (observed stalling 40+ min on loaded runners). Normal runtime is
# well under 30s each.
run_vm_test() { timeout --kill-after=10 180 python3 "tools/virtual_master/$1"; }
run_vm_test test_automated_mandatory.py
run_vm_test test_pd_variable.py
run_vm_test test_conformance_error_injection.py
run_vm_test test_conformance_isdu.py
run_vm_test test_conformance_state_machine.py
run_vm_test test_conformance_timing.py
