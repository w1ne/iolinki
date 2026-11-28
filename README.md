# iolinki: Open-Source IO-Link Device Stack

**Hardware-Agnostic IO-Link Protocol Stack for Zephyr RTOS and Bare-Metal Embedded Systems**

The companion master stack is [iolinki-master](https://github.com/w1ne/iolinki-master).

## Overview

`iolinki` is a professional-grade, open-source IO-Link Device Stack (implements IO-Link V1.1.5, Package 2025 — the latest published specification package) designed with complete hardware independence and comprehensive test coverage. Built from the ground up using test-driven development, every component is testable without hardware.

### Key Features

- **Hardware-Agnostic**: Runs on any platform via clean PHY abstraction
- **Test-Driven**: 100% mock-based unit testing from day one
- **Portable**: Zephyr-native with bare-metal compatibility
- **Virtual Testing**: Conformance verification against virtual IO-Link Master
- **Open Source**: Transparent, vendor-agnostic implementation

### Licensing

**Dual-licensed: GPLv3 or Commercial.** Pick the one that matches your product.

- **GPLv3 (free, open source)** — use, modify, and ship iolinki at no cost, including in production, provided your own work that includes it is also released under the GPLv3.
- **Commercial license** — **required only if you ship a closed-source / proprietary product** and do not want the GPLv3 copyleft obligations. One-time, royalty-free:
  - **Indie**: €1,399 — issued to a named individual for their own independent product family; two onboarding hours for build/setup, MCU/PHY wiring and callback review, and a written bring-up checklist; 48-hour email response target
  - **Company**: €4,699 — held by the named legal company for one agreed product family, with the same source; eight total scoped engineering hours, 24-hour priority email response target and quarterly technical reviews during the first year within those hours
  - **Enterprise**: custom for multiple families, subsidiaries or broader SDK redistribution
  - Indie is for independent developers, including sole traders, and does not grant product rights to an employer or separate company. Company permits unlimited authorized employees and contractors working on the licensed family; company projects require company-held rights.
  - Includes 12 months of updates and bug fixes. Agree board/compiler/PHY/tasks and booking window before scheduling. Company assistance deliverables within the eight-hour allowance include build/configuration review, MCU/PHY callback and wiring review, a written checklist and findings from the agreed build/test review. Larger ports are quoted separately.
  - New quotes cover one named commercial product range, including variants, successors and hardware revisions within that range: unlimited manufactured units and customers, royalty-free. An unrelated line needs a separate license. MCU/PHY changes within the agreed family keep the license valid; porting and validation are separately quoted. Already granted or separately agreed rights remain governed by their accepted agreement.
  - Both packages receive the same source, documentation and reference release. Reproducible stack bug corrections do not consume custom engineering hours. Company contractors receive access for the licensee's project; independent reuse, subsidiaries and broader redistribution require agreed additional scope.

**Shipping a closed-source product?** A commercial license removes the GPLv3 obligations — email **andrii@shylenko.com** for terms (fast, no-friction). See [LICENSE](LICENSE) and [LICENSE.COMMERCIAL](LICENSE.COMMERCIAL).

## Physical response timing

IO-Link Interface and System Specification V1.1.4, Annex A.3.5 defines device
response time **t_A** from the last master stop bit to the first device start bit.
The valid interval is **1–10 bit times**. Table 10's **T_REN <= 500 us** instead
describes receive enable after wake-up. It is independent of reply length.

A PHY can provide get_response_timing with fresh timestamps for the latest
request/reply. A blocking send must record its **first start**, before waiting
for UART transmission complete. Return IOLINK_PHY_TIMING_ESTIMATED for software
observations such as interrupt and register-submission timestamps. Use
IOLINK_PHY_TIMING_CAPTURED only for captured wire boundaries. The stack reports
these counts separately. Neither a host test nor a software estimate qualifies
physical timing.

Every Type-0/1/2 response uses the same timing check. If the PHY has no timing
callback, or returns 0 because a fresh sample is unavailable, the stack only
increments `response_timing_unavailable`; it does not report a violation.
With enforcement enabled, invalid fresh samples (a negative callback result,
bad quality, reversed/future timestamps, or an overflowing interval) raise a
timing error. Valid samples outside the 1–10 bit-time interval increment
`t_a_violations`. The limit uses the actual COM rate without rounding bit
times, and `t_a_violations` is independent of `t_ren_violations`. A long
blocking reply does not consume the response-start or wake-enable budget.

The legacy set_t_ren_limit_us API now controls the measured SIO-to-SDCI
receive-enable **processing** budget. Zero restores 500 us. PHY detection and
interrupt latency still require a wire measurement. This setting cannot relax
the 1–10 bit response-start interval.

Source: [IO-Link Interface and System Specification V1.1.4, Table 10 and Annex A.3.5](https://io-link.com/fileadmin/user_upload/Downloads/Package_2024/IOL-Interface-Spec_10002_V114_Jun24.pdf).

## Quick Start

For a runnable counter/button/LED software example, see
[examples/reference_device](examples/reference_device/README.md). For practical
evaluation questions and integration status, see [docs/FAQ.md](docs/FAQ.md) and
[the portable TIOL112 driver](docs/hardware/TIOL112.md). Physical board ports and
commercial-master validation are separate from the host simulation.

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

iolinki includes **51 automated conformance tests** validating compliance with the IO-Link V1.1.5 specification. The suite targets Package 2025 (V1.1.5, October 2025), the latest published package. Package 2026 (D1.1.6) is in public review until 17 December 2026 and will be tracked when ratified. Coverage by category:

- ✅ **State Machine**: DLL transitions, ESTAB_COM, fallback behavior (7 tests)
- ✅ **Timing**: Cycle times, response delays, wake-up timing (5 tests)
- ✅ **ISDU Protocol**: All mandatory indices + error handling (13 tests)
- ✅ **System Commands**: Table B.9 values, resets, Block Parameterization, DS upload flag (11 tests; re-run 2026-11-16 on the virtual PTY, see docs/CONFORMANCE.md)
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
- **[MIGRATION_3.0.md](./docs/MIGRATION_3.0.md)** - v2.2.0 to v3.0.0 wire, ABI,
  COM-rate and timing migration notes
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

The release workflow attaches source and example archives, firmware and host
demos, IODD packages, the STM32G0 IAR project, LabWired recipes, SBOMs, test
evidence and checksums. Its notes include the migration guide and validation
limits. A release preparation PR to `develop` only creates candidate workflow
artifacts; tagging an RC or stable version publishes a GitHub Release. See the
[release strategy](./docs/RELEASE_STRATEGY.md) before preparing a release.

## Development Philosophy

**Test-Driven from Ground Zero**: All development is built on mocks and abstractions. Every component is testable without hardware. Conformance verification runs against a virtual IO-Link Master on each release.

## License

This project is dual-licensed under the **GPLv3** (free, for open-source/GPLv3 use)
and a **commercial license** (for closed-source / proprietary products that cannot
accept the GPLv3 copyleft). Shipping a proprietary product? Email
**andrii@shylenko.com**. See [LICENSE](LICENSE) and [LICENSE.COMMERCIAL](LICENSE.COMMERCIAL).

## Contributing

Contributions are welcome! Please see [ROADMAP.md](./docs/ROADMAP.md) for areas where help is needed.

### Device rate during recovery

Each device keeps its configured COM1, COM2, or COM3 rate across CRC fallback,
inter-byte loss, inactivity, T_DSIO expiry and a FALLBACK MasterCommand. A new
wake-up reuses that rate. The master discovers the device rate; recovery must
not force a COM2/COM3 device to COM1. Applications can select a different fixed
rate through `iolink_dll_set_baudrate()` during initialization. This follows
[IO-Link v1.1.4 section 7.3.2.2](https://io-link.com/fileadmin/user_upload/Downloads/Package_2024/IOL-Interface-Spec_10002_V114_Jun24.pdf).

`test_baud_recovery` uses a synthetic clock and checks 18 recovery paths across
all three rates, including a new wake-up, valid startup messages and cyclic
process data. It does not qualify a physical UART or C/Q waveform.
