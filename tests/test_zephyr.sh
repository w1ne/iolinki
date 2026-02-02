#!/bin/bash
# Test IO-Link Zephyr App (Native Simulator)
set -e

APP_DIR="examples/zephyr_app"
BUILD_DIR="build_zephyr"
BINARY="$BUILD_DIR/zephyr/zephyr.exe"

echo "========================================"
echo "🪁 Testing Zephyr Native Simulation"
echo "========================================"

if ! command -v west &> /dev/null; then
    echo "⚠️ 'west' not found. Skipping Zephyr tests."
    echo "   (This validation requires a Zephyr development environment)"
    exit 0
fi

# 1. Build
echo "   🔨 Building Native Sim..."
# Check if west build extension is available
if ! west help build >/dev/null 2>&1; then
    echo "⚠️ 'west build' extension not found. Please initialize a Zephyr workspace or source zephyr-env.sh."
    echo "   Skipping Zephyr integration test."
    exit 0
fi

if west build -p auto -b native_sim -d $BUILD_DIR $APP_DIR > /dev/null; then
    echo "   ✅ Build Successful"
else
    echo "   ❌ Build Failed (Check Zephyr environment)"
    exit 1
fi

# 2. Run Integration Test
echo "   🏃 Running Integration Test..."
chmod +x tools/zephyr_wrapper.sh
export IOLINK_DEVICE_PATH="$(pwd)/tools/zephyr_wrapper.sh"

# Run the Python Test
if python3 tools/virtual_master/test_type1.py; then
   echo "   ✅ Zephyr Integration Test Passed"
else
   echo "   ❌ Zephyr Integration Test FAILED"
   exit 1
fi

exit 0
