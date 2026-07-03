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

**Dual-licensed: GPLv3 or Commercial.** Pick the one that matches your product.

- **GPLv3 (free, open source)** — use, modify, and ship iolinki at no cost, provided your own work that includes it is also released under the GPLv3. Ideal for open-source projects, research, evaluation, and hobby use.
- **Commercial license** — **required only if you ship a closed-source / proprietary product** and do not want the GPLv3 copyleft obligations. One-time, royalty-free:
  - **Single Developer**: €1,399
  - **Team (5 seats)**: €4,699
  - **Enterprise / site-wide**: custom
  - Includes 12 months of updates + support and up to 8 hours of integration assistance.

**Shipping a closed-source product?** A commercial license removes the GPLv3 obligations — email **andrii@shylenko.com** for terms (fast, no-friction). See [LICENSE](LICENSE) and [LICENSE.COMMERCIAL](LICENSE.COMMERCIAL).

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

### Development Tools (pre-commit)

To ensure code quality, `iolinki` uses `pre-commit` hooks. These hooks run `clang-format`, `cppcheck`, `ruff`, and `shellcheck` automatically before each commit.

1. **Install pre-commit**:
   ```bash
   pip install pre-commit
   ```

2. **Install the git hooks**:
   ```bash
   pre-commit install
   ```

3. **(Optional) Run on all files**:
   ```bash
   pre-commit run --all-files
   ```

## Running Tests

### Docker (Primary & Recommended)
Docker is the primary and recommended environment for running all `iolinki` tests. This ensures a consistent environment with all tools (CMocka, Cppcheck, Doxygen, Clang-Format) pre-configured.

```bash
./run_all_tests_docker.sh
```

### Local (Requires Manual Dependencies)
To run tests locally, ensure you have `libcmocka-dev` installed: -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
cd build && ctest --output-on-failure
```

### Full Validation Suite (All Platforms + Conformance)
```bash
./test_all.sh
```

## IO-Link V1.1.5 Conformance

iolinki includes **49 automated conformance tests** validating compliance with the IO-Link V1.1.5 specification:

- ✅ **State Machine**: DLL transitions, ESTAB_COM, fallback behavior (7 tests)
- ✅ **Timing**: Cycle times, response delays, wake-up timing (5 tests)
- ✅ **ISDU Protocol**: All mandatory indices + error handling (13 tests)
- ✅ **System Commands**: Reset, factory restore, Data Storage upload/download (9 tests)
- ✅ **Error Injection**: Recovery, robustness, edge cases (7 tests)
- ✅ **PD / Events / SIO**: Process Data, event signalling, SIO fallback (3 tests)
- ✅ **Performance**: Sustained operation, stress testing (5 tests)

**Coverage**: 100% of mandatory ISDU indices (0x0010-0x0018, 0x001E, 0x0024), Data Storage (0x0003), state machine transitions, timing requirements, and error handling.

See [docs/CONFORMANCE.md](docs/CONFORMANCE.md) for detailed test specifications and coverage matrix.

### Building for Bare Metal

```bash
cmake -B build_bare -DIOLINK_PLATFORM=BAREMETAL
cmake --build build_bare
./build_bare/examples/bare_metal_app/bare_metal_app
```

### Building for Zephyr

iolinki is a first-class **Zephyr module**: add it to a `west` workspace, enable
it in Kconfig, point it at a UART, and build an IO-Link device. See
**[zephyr/README.md](zephyr/README.md)** for the full guide (west manifest
snippet, Kconfig options, the `zephyr,iolink-uart` devicetree contract, and the
PHY porting note).

**Option 1: Docker (Recommended for testing)**
If you have Docker installed, you can build the Zephyr sample without installing the SDK on your host:
```bash
./tools/build_zephyr_docker.sh
```

**Option 2: Local SDK**
**Prerequisite**: You must have the [Zephyr SDK and tools installed](https://docs.zephyrproject.org/latest/develop/getting_started/index.html) and be running in an initialized Zephyr workspace (or have `ZEPHYR_BASE` set).

```bash
# From your Zephyr workspace root, after adding iolinki as a module
west build -b native_sim modules/lib/iolinki/samples/iolink_device

# For real hardware (UART PHY)
west build -b nucleo_l476rg modules/lib/iolinki/samples/iolink_device -- -DCONFIG_IOLINK_PHY_UART=y
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

## Security

- **[SECURITY.md](./SECURITY.md)** — coordinated disclosure policy and CRA-ready
  vulnerability handling
- **[Threat model](./docs/security/THREAT_MODEL.md)** — STRIDE analysis aligned to
  the IO-Link Security Design and Development Guideline (Order No. 10.512), every
  claim anchored to code and tests
- **[CRA overview](./docs/security/CRA.md)** — what the EU Cyber Resilience Act
  means for devices built on this stack; free vs. commercial deliverables
- **SBOMs** — CycloneDX 1.6 + SPDX 2.3 attached to every tagged release

## Related Projects

- **[iolinki-master](https://github.com/w1ne/iolinki-master)** — the companion
  standalone IO-Link **master** stack (multi-port controller). Split from this
  repository on purpose; it reuses only the narrow shared CRC/frame/PHY pieces
  and is CI-validated against this device stack over a simulated wire.

## Releases

Official releases are available on [GitHub Releases](https://github.com/w1ne/iolinki/releases).

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

This project is dual-licensed under the **GPLv3** (free, for open-source/GPLv3 use)
and a **commercial license** (for closed-source / proprietary products that cannot
accept the GPLv3 copyleft). Shipping a proprietary product? Email
**andrii@shylenko.com**. See [LICENSE](LICENSE) and [LICENSE.COMMERCIAL](LICENSE.COMMERCIAL).

## Contributing

Contributions are welcome! Please see [ROADMAP.md](./docs/ROADMAP.md) for areas where help is needed.
