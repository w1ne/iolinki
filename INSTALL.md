# Installation Guide

## Dependencies

### Ubuntu/Debian
```bash
sudo apt-get update
sudo apt-get install -y cmake build-essential libcmocka-dev clang-format cppcheck
```

### macOS
```bash
brew install cmake cmocka clang-format cppcheck
```

## Building

```bash
cmake -B build
cmake --build build
```

## Running Tests

```bash
cd build
ctest --output-on-failure
```

## Running Example

```bash
./build/examples/simple_device/simple_device
```
