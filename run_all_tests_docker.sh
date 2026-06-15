#!/bin/bash
# iolinki Unified Docker Test Runner
#
# Usage: run_all_tests_docker.sh [host|zephyr|all]   (default: all)
#   host   - code-quality + unit + conformance (the lightweight required gate)
#   zephyr - heavy Zephyr simulation build (~10GB image); run as a separate,
#            non-blocking CI job so it cannot exhaust the gate runner.
set -e

STAGE="${1:-all}"

echo "============================================"
echo "🐳 iolinki Docker-based Validation Suite (stage: $STAGE)"
echo "============================================"

# 1. Linux Host Tests
if [[ "$STAGE" == "all" || "$STAGE" == "host" ]]; then
    echo -e "\n[1/2] 🐧 Running Linux Host Tests..."
    if [[ "$(docker images -q iolinki-test 2> /dev/null)" == "" ]]; then
        echo "   ⚠️ Test image not found. Building..."
        docker build -f Dockerfile.test -t iolinki-test .
    else
        echo "   ✅ Test image found. Reusing cached image."
    fi
    docker run --rm -v "$(pwd)":/workspace iolinki-test bash -c "./check_quality.sh && bash /workspace/.docker_test_entrypoint.sh"
fi

# 2. Zephyr Simulation Tests
if [[ "$STAGE" == "all" || "$STAGE" == "zephyr" ]]; then
    echo -e "\n[2/2] 🪁 Running Zephyr Simulation Tests..."

    # Check if Zephyr base image exists
    if [[ "$(docker images -q iolinki-zephyr-base 2> /dev/null)" == "" ]]; then
        echo "   ⚠️ Zephyr base image not found. Building (this will download ~5GB, first time only)..."
        docker build -f Dockerfile.zephyr-base -t iolinki-zephyr-base .
    else
        echo "   ✅ Zephyr base image found. Skipping heavy download."
    fi

    if [[ "$(docker images -q iolinki-zephyr-test 2> /dev/null)" == "" ]]; then
        echo "   🔨 Building test image..."
        docker build -f Dockerfile.zephyr -t iolinki-zephyr-test .
    else
        echo "   ✅ Zephyr test image found. Reusing cached image."
    fi

    echo "   🏃 Running Zephyr tests..."
    docker run --rm -v "$(pwd)":/workdir/modules/lib/iolinki iolinki-zephyr-test
fi

echo -e "\n============================================"
echo "✅ Dockerized Tests Completed Successfully (stage: $STAGE)"
echo "============================================"
