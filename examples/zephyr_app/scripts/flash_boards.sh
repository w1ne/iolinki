#!/bin/bash
set -e

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../../.." && pwd)
APP_DIR="examples/zephyr_app"

# Detected Boards
# Board A (Device): J-Link (ttyACM0) -> Serial 000771566457
# Board B (Master): ST-Link (ttyACM1) -> Serial 0670FF535155878281121747

echo "=== Flashing Setup for Nucleo-to-Nucleo Test ==="
echo ""

# 1. Flash Board A (Device)
echo "[1/2] Building and Flashing DEVICE (Board A - J-Link)..."
west build -p always -b nucleo_l476rg "$REPO_ROOT/$APP_DIR"
west flash --runner jlink -i 000771566457

# 2. Flash Board B (Master Generator)
echo ""
echo "[2/2] Building and Flashing MASTER GENERATOR (Board B - ST-Link)..."
west build -p always -b nucleo_l476rg "$REPO_ROOT/$APP_DIR" -- -DCONFIG_IOLINK_DEMO_MASTER=y
west flash --runner openocd --serial 0670FF535155878281121747

echo ""
echo "=== Flashing Complete! ==="
echo "Now run 'python3 $SCRIPT_DIR/monitor_nucleos.py' to observe the test."
