#!/bin/bash
# Integration test runner for Virtual Master
# Builds device, runs tests, and reports results

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"

echo "=========================================="
echo "Virtual Master Integration Test Runner"
echo "=========================================="

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if build exists
if [ ! -d "$BUILD_DIR" ]; then
    echo -e "${YELLOW}Build directory not found. Building project...${NC}"
    cd "$PROJECT_ROOT"
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build
fi

# Check if device binary exists
DEVICE_BIN="$BUILD_DIR/examples/host_demo/host_demo"
if [ ! -f "$DEVICE_BIN" ]; then
    DEVICE_BIN="$BUILD_DIR/examples/simple_device/simple_device"
    if [ ! -f "$DEVICE_BIN" ]; then
        echo -e "${RED}ERROR: Device binary not found!${NC}"
        echo "Please build the project first."
        exit 1
    fi
fi

echo -e "${GREEN}Using device binary: $DEVICE_BIN${NC}"
echo ""

# Run device connection tests
echo "=========================================="
echo "Running Device Connection Tests"
echo "=========================================="
cd "$SCRIPT_DIR"
python3 test_device_connection.py
CONNECTION_RESULT=$?

echo ""

# Run M-sequence type tests (manual mode - requires user to connect device)
echo "=========================================="
echo "M-Sequence Type Tests"
echo "=========================================="
echo -e "${YELLOW}Note: These tests require manual device connection${NC}"
echo "To run: python3 test_m_sequence_types.py"
echo ""

# Summary
echo "=========================================="
echo "Integration Test Summary"
echo "=========================================="

if [ $CONNECTION_RESULT -eq 0 ]; then
    echo -e "${GREEN}✅ Device Connection Tests: PASSED${NC}"
else
    echo -e "${RED}❌ Device Connection Tests: FAILED${NC}"
fi

echo ""
echo "=========================================="

exit $CONNECTION_RESULT
