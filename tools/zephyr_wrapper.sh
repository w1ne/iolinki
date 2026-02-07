#!/bin/bash
# Wrapper to run Zephyr app with IOLINK_PORT env var from first argument
# Usage: ./zephyr_wrapper.sh <tty_path> <type> <pd_len>

export IOLINK_PORT="$1"
# We ignore other args (type/pd_len) for now as zephyr demo is hardcoded or configured via Kconfig?
# The demo app seems to use defaults (Type 1, PD 2).

# Path to actual exe (absolute path in Docker)
EXE="/workdir/build_zephyr/zephyr/zephyr.exe"

if [ ! -f "$EXE" ]; then
    echo "Error: Zephyr executable not found at $EXE"
    exit 1
fi

exec "$EXE" --rt
