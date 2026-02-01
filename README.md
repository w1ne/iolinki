# iolinki: Open-Source IO-Link Device Stack

**Hardware-Agnostic IO-Link Protocol Stack for Zephyr RTOS and Bare-Metal Embedded Systems**

## Overview

`iolinki` is a professional-grade, open-source IO-Link Device Stack (compliant with Spec V1.1.5) designed with complete hardware independence and comprehensive test coverage. Built from the ground up using test-driven development, every component is testable without hardware.

### Key Features

- **Hardware-Agnostic**: Runs on any platform via clean PHY abstraction
- **Test-Driven**: 100% mock-based unit testing from day one
- **Portable**: Zephyr-native with bare-metal compatibility
- **Virtual Testing**: Conformance verification against virtual IO-Link Master
- **Open Source**: Transparent, vendor-agnostic implementation

## Quick Start

### Installation

#### Ubuntu/Debian
```bash
sudo apt-get update
sudo apt-get install -y cmake build-essential libcmocka-dev clang-format cppcheck
```

#### macOS
```bash
brew install cmake cmocka clang-format cppcheck
```

### Building

```bash
cmake -B build
cmake --build build
```

### Running Tests

```bash
cd build
ctest --output-on-failure
```

### Running Example

```bash
./build/examples/simple_device/simple_device
```

## Project Status

**Phase 1: Technical Foundation (In Progress)**

We are currently setting up the test-driven development infrastructure and preparing to implement the Physical Layer abstraction.

See [ROADMAP.md](./docs/ROADMAP.md) for detailed development phases.

## Documentation

- **[ROADMAP.md](./docs/ROADMAP.md)** - Development phases and milestones
- **[VISION.md](./docs/VISION.md)** - Project mission and approach
- **[RELEASE_STRATEGY.md](./docs/RELEASE_STRATEGY.md)** - Release workflow and versioning
- **[INSTALL.md](./INSTALL.md)** - Detailed installation instructions

## Releases

Official releases are available on [GitHub Releases](https://github.com/yourusername/iolinki/releases).

Each release includes:
- **Test Results**: Complete test suite validation
- **Build Artifacts**: Pre-compiled examples and test binaries
- **Documentation**: Updated docs and guides

To create a new release:
```bash
git tag -a v0.1.0 -m "Release version 0.1.0"
git push origin v0.1.0
```

## Development Philosophy

**Test-Driven from Ground Zero**: All development is built on mocks and abstractions. Every component is testable without hardware. Conformance verification runs against a virtual IO-Link Master on each release.

## License

[To be determined - likely MIT or Apache 2.0]

## Contributing

Contributions are welcome! Please see [ROADMAP.md](./docs/ROADMAP.md) for areas where help is needed.
