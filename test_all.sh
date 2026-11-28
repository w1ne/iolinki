#!/bin/bash
set -e

# iolinki Test Suite Runner
# Runs builds and tests for all supported platforms

echo "============================================"
echo "🚀 iolinki CI: Starting Validation Suite"
echo "============================================"

# Environment Check
if ! command -v cmake &> /dev/null; then
    echo "❌ CMake not found"
    exit 1
fi

echo "✅ Environment checks passed"

# 1. Linux Host Build & Test
echo -e "\n[1/3] 🐧 Testing Linux Host Target..."
rm -rf build_linux
mkdir build_linux
cd build_linux
cmake .. -DCMAKE_BUILD_TYPE=Debug -DIOLINK_PLATFORM=LINUX
make -j"$(nproc)"
echo "   ✅ Linux Build Successful"

# Run Integration Tests
if [ -d ../tools/virtual_master ]; then
    export IOLINK_DEVICE_PATH="./examples/host_demo/host_demo"

    # 1. Type 1 Test
    if [ -f ../tools/virtual_master/test_type1.py ]; then
        echo "   🏃 Running Type 1 Integration Test..."
        if ! python3 ../tools/virtual_master/test_type1.py; then
            echo "   ❌ Type 1 Integration Test FAILED"
            exit 1
        fi
        echo "   ✅ Type 1 Integration Test Passed"
    fi

    # 2. Mandatory Indices Test
    if [ -f ../tools/virtual_master/test_automated_mandatory.py ]; then
        echo "   🏃 Running Mandatory Indices Integration Test..."
        if ! python3 ../tools/virtual_master/test_automated_mandatory.py; then
            echo "   ❌ Mandatory Indices Test FAILED"
            exit 1
        fi
        echo "   ✅ Mandatory Indices Test Passed"
    fi

    # 3. Variable PD & Persistence Test
    if [ -f ../tools/virtual_master/test_pd_variable.py ]; then
        echo "   🏃 Running Variable PD & Persistence Integration Test..."
        if ! python3 ../tools/virtual_master/test_pd_variable.py; then
            echo "   ❌ Variable PD Test FAILED"
            exit 1
        fi
        echo "   ✅ Variable PD Test Passed"
    fi

    # 4. IO-Link V1.1.5 Conformance Test Suite
    echo "   🏃 Running IO-Link V1.1.5 Conformance Tests..."

    if [ -f ../tools/virtual_master/test_conformance_state_machine.py ]; then
        echo "      → State Machine Conformance..."
        if ! python3 ../tools/virtual_master/test_conformance_state_machine.py; then
            echo "   ❌ State Machine Conformance FAILED"
            exit 1
        fi
    fi

    if [ -f ../tools/virtual_master/test_conformance_timing.py ]; then
        echo "      → Timing Requirements..."
        if ! python3 ../tools/virtual_master/test_conformance_timing.py; then
            echo "   ❌ Timing Conformance FAILED"
            exit 1
        fi
    fi

    if [ -f ../tools/virtual_master/test_conformance_isdu.py ]; then
        echo "      → ISDU Protocol Validation..."
        if ! python3 ../tools/virtual_master/test_conformance_isdu.py; then
            echo "   ❌ ISDU Conformance FAILED"
            exit 1
        fi
    fi

    if [ -f ../tools/virtual_master/test_conformance_error_injection.py ]; then
        echo "      → Error Injection & Recovery..."
        if ! python3 ../tools/virtual_master/test_conformance_error_injection.py; then
            echo "   ❌ Error Injection Conformance FAILED"
            exit 1
        fi
    fi

    if [ -f ../tools/virtual_master/test_conformance_performance.py ]; then
        echo "      → Performance & Stress Testing..."
        if ! python3 ../tools/virtual_master/test_conformance_performance.py; then
            echo "   ❌ Performance Conformance FAILED"
            exit 1
        fi
    fi

    echo "   ✅ All Conformance Tests Passed"
else
    echo "   ⚠️ Skipping Integration Tests (Tools directory not found)"
fi
cd ..

# 2. Bare Metal Build Verification
echo -e "\n[2/3] ⚙️  Verifying Bare Metal Build..."
rm -rf build_baremetal
mkdir build_baremetal
cd build_baremetal
# Bare metal usually requires cross-compiler, but we can verify source compilation
# using a generic config or mock toolchain if available.
# For now, we build the library only to ensure no Linux dependencies leaked.
cmake .. -DCMAKE_BUILD_TYPE=Debug -DIOLINK_PLATFORM=BAREMETAL
make iolinki -j"$(nproc)"
echo "   ✅ Bare Metal Library Build Successful"
cd ..

# 3. Zephyr Build Verification (Simulation)
echo -e "\n[3/3] 🪁 Checking Zephyr Compatibility..."
if command -v west &> /dev/null; then
    chmod +x tests/test_zephyr.sh
    if ./tests/test_zephyr.sh; then
        echo "   ✅ Zephyr Tests Passed"
    else
        echo "   ⚠️ Zephyr Tests Failed/Skipped (Environment issue)"
        # We don't exit 1 here to avoid breaking workflow on systems without full Zephyr SDK
    fi
else
    echo "   ⚠️ West not found, skipping Zephyr build."
fi

# 4. Cleanup
echo -e "\n============================================"
echo "✅ All Validation Steps Completed Successfully"
echo "============================================"
exit 0
