#!/bin/bash
# iolinki Unified Docker Test Runner
set -e

echo "============================================"
echo "🐳 iolinki Docker-based Validation Suite"
echo "============================================"

# 1. Linux Host Tests
echo -e "\n[1/2] 🐧 Running Linux Host Tests..."
docker build -f Dockerfile.test -t iolinki-test .
docker run --rm iolinki-test

# 2. Zephyr Simulation Tests
echo -e "\n[2/2] 🪁 Running Zephyr Simulation Tests..."

# Check if Zephyr base image exists
if [[ "$(docker images -q iolinki-zephyr-base 2> /dev/null)" == "" ]]; then
    echo "   ⚠️ Zephyr base image not found. Building (this will download ~5GB, first time only)..."
    docker build -f Dockerfile.zephyr-base -t iolinki-zephyr-base .
else
    echo "   ✅ Zephyr base image found. Skipping heavy download."
fi

echo "   🔨 Building test image..."
docker build -f Dockerfile.zephyr -t iolinki-zephyr-test .

echo "   🏃 Running Zephyr tests..."
docker run --rm iolinki-zephyr-test

echo -e "\n============================================"
echo "✅ All Dockerized Tests Completed Successfully"
echo "============================================"
