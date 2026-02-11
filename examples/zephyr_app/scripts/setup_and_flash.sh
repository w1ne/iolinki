#!/bin/bash
# Copyright (C) 2026 Andrii Shylenko
# Script to automate Zephyr workspace setup and board flashing
set -e

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../../.." && pwd)
VENV_PATH=~/zephyrproject/.venv

# Path to the app relative to REPO_ROOT (iolinki)
APP_DIR="examples/zephyr_app"

echo "=== Zephyr Physical Test Setup Tool ==="

# 1. Activate Environment
if [ -f "$VENV_PATH/bin/activate" ]; then
    echo "Activating virtual environment..."
    source "$VENV_PATH/bin/activate"
else
    echo "ERROR: Virtual environment not found at $VENV_PATH"
    echo "Please run: python3 -m venv ~/zephyrproject/.venv && source ~/zephyrproject/.venv/bin/activate && pip install west"
    exit 1
fi

# 2. Ensure dependencies are installed
echo "Checking dependencies..."
# Zephyr build scripts need elftools and other packages
pip install -q pyserial pyelftools

# 3. Handle West Workspace (Root is ~/Projects)
WORKSPACE_ROOT=$(dirname "$REPO_ROOT")
if [ ! -d "$WORKSPACE_ROOT/.west" ]; then
    echo "Initializing west workspace in $WORKSPACE_ROOT..."
    mkdir -p "$WORKSPACE_ROOT"
    cd "$WORKSPACE_ROOT"
    west init -l iolinki
    west update
    cd "$REPO_ROOT"
else
    # Ensure zephyr is downloaded
    if [ ! -d "$WORKSPACE_ROOT/zephyr" ]; then
        echo "Updating west projects..."
        west update
    fi
fi

# 4. EXPLICITLY set environment to the correct Zephyr and SDK
export ZEPHYR_BASE="$WORKSPACE_ROOT/zephyr"
export ZEPHYR_SDK_INSTALL_DIR="/home/andrii/Projects/mobile_3000/UDS/udslib/zephyr-sdk-0.16.5"
export Zephyr_DIR="$ZEPHYR_BASE/share/zephyr-package/cmake"
export Zephyr_sdk_DIR="$ZEPHYR_SDK_INSTALL_DIR/cmake"

echo "Force set ZEPHYR_BASE to $ZEPHYR_BASE"
echo "Force set ZEPHYR_SDK_INSTALL_DIR to $ZEPHYR_SDK_INSTALL_DIR"

# Source zephyr environment
if [ -f "$ZEPHYR_BASE/zephyr-env.sh" ]; then
    source "$ZEPHYR_BASE/zephyr-env.sh"
fi

# 5. Define common CMake arguments
COMMON_CMAKE_ARGS="-DZEPHYR_SDK_INSTALL_DIR=$ZEPHYR_SDK_INSTALL_DIR -DZephyr-sdk_DIR=$Zephyr_sdk_DIR -DCMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY=ON -DCMAKE_FIND_PACKAGE_NO_SYSTEM_PACKAGE_REGISTRY=ON"

# 6. Build DEVICE (Board A)
echo ""
echo "[1/2] Building DEVICE (Board A)..."
west build -p always -b nucleo_l476rg "$REPO_ROOT/$APP_DIR" -- $COMMON_CMAKE_ARGS

# 7. Build MASTER (Board B)
echo ""
echo "[2/2] Building MASTER (Board B)..."
west build -p always -b nucleo_l476rg -d build_master "$REPO_ROOT/$APP_DIR" -- -DCONFIG_IOLINK_DEMO_MASTER=y $COMMON_CMAKE_ARGS

# 8. Flashing Logic
echo ""
echo "=== Build Successful! Proceeding to Flashing ==="

# Check for JLinkExe
if ! command -v JLinkExe &> /dev/null; then
    echo "WARNING: JLinkExe not found. Will try OpenOCD for Board A (J-Link)."
    RUNNER_A="openocd"
    # For J-Link boards via OpenOCD, we need specific config
    FLASH_ARGS_A="--runner openocd -- --config interface/jlink.cfg --config target/stm32l4x.cfg"
else
    RUNNER_A="jlink"
    FLASH_ARGS_A="--runner jlink -i 000771566457"
fi

echo "Flashing Board A (Device)..."
# If we get permission error, we'll tell the user
if ! west flash $FLASH_ARGS_A; then
    echo ""
    echo "!!! Flashing Board A failed with permission error !!!"
    echo "Try running this command manually with sudo:"
    echo "sudo ~/zephyrproject/.venv/bin/python3 -m west flash $FLASH_ARGS_A"
fi

echo ""
echo "Flashing Board B (Master Generator)..."
# Board B is ST-Link, OpenOCD should work if perms allow
if ! west flash -d build_master --runner openocd --serial 0670FF535155878281121747; then
    echo ""
    echo "!!! Flashing Board B failed !!!"
    echo "Checking if we can use the mounted drive: /media/andrii/NODE_L476RG"
    if [ -d "/media/andrii/NODE_L476RG" ]; then
        echo "Copying binary to NODE_L476RG drive..."
        cp build_master/zephyr/zephyr.bin /media/andrii/NODE_L476RG/
        echo "Flashing via drive copy initiated."
    else
        echo "Try running this command manually with sudo:"
        echo "sudo ~/zephyrproject/.venv/bin/python3 -m west flash -d build_master --runner openocd --serial 0670FF535155878281121747"
    fi
fi

echo ""
echo "=== Setup and Flashing Procedure Complete! ==="
echo "If flashing failed, follow the 'sudo' instructions above."
echo "Then run the monitor tool:"
echo "python3 $SCRIPT_DIR/monitor_nucleos.py"
