#!/bin/bash
set -e

echo "=== Building IO-Link Test Environment ==="
docker build -f Dockerfile.test -t iolinki-test .

echo ""
echo "=== Running All Tests ==="
echo "  - CMocka Unit Tests"
echo "  - Virtual Master Unit Tests"
echo "  - Virtual Master + Device Integration Test"
docker run --rm iolinki-test

echo ""
echo "=== Test Execution Complete ==="
