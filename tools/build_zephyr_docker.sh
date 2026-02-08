#!/bin/bash
set -e

# Build the docker image (if not exists)
# Note: pulling 'zephyrprojectrtos/zephyr-build:latest' is approx 2-4GB.
# We assume the user has docker installed.


# Create minimal Dockerfile locally if needed, but we can usually run directly
# if we just want to compile.

echo "=== Building IO-Link Zephyr App via Docker ==="

# Using official Zephyr Build image directly to avoid building a custom one if possible
# We map the current directory to /workdir/modules/lib/iolinki
# We use a volume for the 'west' workspace to avoid re-downloading every time?
# For simplicity, we assume ephemeral build.

# Standard Zephyr Build Command using the official container
docker run --rm \
    -v "$(pwd)":/workdir/modules/lib/iolinki \
    -w /workdir/modules/lib/iolinki \
    zephyrprojectrtos/cmockunity:latest \
    /bin/bash -c "
    west init -l . && \
    west update && \
    west build -b native_sim examples/zephyr_app"

echo "=== Build Complete ==="
