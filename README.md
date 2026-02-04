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

### Licensing

**Dual-Licensed**: GPLv3 (Evaluation) + Commercial

- **Evaluation**: Free for 90 days under GPLv3 in non-production environments
- **Commercial**: Required for production deployment
  - **Single Developer**: €1,399 (Royalty-Free)
  - **Team (5 seats)**: €4,699 (Royalty-Free)
  - **Enterprise**: Custom pricing

See [LICENSE.COMMERCIAL](LICENSE.COMMERCIAL) for details or contact andrii@shylenko.com

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

### Running Hosts (Linux)

```bash
cmake -B build -DIOLINK_PLATFORM=LINUX
cmake --build build
./build/examples/host_demo/host_demo
```

## Running Tests

### Docker (Recommended - No Dependencies Required)
```bash
./tools/run_tests_docker.sh
```

### Local (Requires CMocka)
```bash
sudo apt-get install libcmocka-dev
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
cd build && ctest --output-on-failure
```

### Full Validation Suite (All Platforms + Conformance)
```bash
./test_all.sh
```

## IO-Link V1.1.5 Conformance

iolinki includes **33 automated conformance tests** validating compliance with the IO-Link V1.1.5 specification:

- ✅ **State Machine**: DLL transitions, fallback behavior (6 tests)
- ✅ **Timing**: Cycle times, response delays (4 tests)
- ✅ **ISDU Protocol**: All 11 mandatory indices + error handling (12 tests)
- ✅ **Error Injection**: Recovery, robustness, edge cases (6 tests)
- ✅ **Performance**: Sustained operation, stress testing (5 tests)

**Coverage**: 100% of mandatory ISDU indices (0x0010-0x0018, 0x001E, 0x0024), state machine transitions, timing requirements, and error handling.

See [docs/CONFORMANCE.md](docs/CONFORMANCE.md) for detailed test specifications and coverage matrix.

### Building for Bare Metal

```bash
cmake -B build_bare -DIOLINK_PLATFORM=BAREMETAL
cmake --build build_bare
./build_bare/examples/bare_metal_app/bare_metal_app
```

### Building for Zephyr

**Option 1: Docker (Recommended for testing)**
If you have Docker installed, you can build the Zephyr example without installing the SDK on your host:
```bash
./tools/build_zephyr_docker.sh
```

**Option 2: Local SDK**
**Prerequisite**: You must have the [Zephyr SDK and tools installed](https://docs.zephyrproject.org/latest/develop/getting_started/index.html) and be running in an initialized Zephyr workspace (or have `ZEPHYR_BASE` set).

```bash
# From your Zephyr workspace root
west build -b native_sim modules/lib/iolinki/examples/zephyr_app
```

> **Troubleshooting**: If you see `west: error: argument <command>: invalid choice: 'build'`, it means `west` is installed but the build extensions are not loaded. This happens if you are not in a valid Zephyr workspace.

### Building for Bare Metal

```bash
cmake -B build_bare -DIOLINK_PLATFORM=BAREMETAL
cmake --build build_bare
./build_bare/examples/bare_metal_app/bare_metal_app
```

## Project Status

**Phase 3: Ecosystem & Verification (Current)**

The stack is feature-complete for IO-Link V1.1.5, including Process Data, ISDU, Events, and Data Storage. We have achieved 100% core test coverage and established automated IODD generation and strict coding standards (MISRA-oriented).

See [ROADMAP.md](./docs/ROADMAP.md) for detailed development phases.

## Documentation

- **[ROADMAP.md](./docs/ROADMAP.md)** - Development phases and milestones
- **[VISION.md](./docs/VISION.md)** - Project mission and approach
- **[RELEASE_STRATEGY.md](./docs/RELEASE_STRATEGY.md)** - Release workflow and versioning
- **[INSTALL.md](./INSTALL.md)** - Detailed installation instructions
- **[PUPPETEER.md](./docs/PUPPETEER.md)** - Agent task mutex workflow (submodule)

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
