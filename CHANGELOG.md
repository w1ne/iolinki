# Changelog

All notable changes to the `iolinki` project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.4.0] — 2026-02-02

### Added
- **Variable Process Data**: Full support for IO-Link V1.1.5 frame types (Type 1_V, 2_V) supporting 2-32 bytes.
- **Data Validity**: Added mandatory `valid` flag to `iolink_pd_input_update` and `PDStatus` bit propagation in status byte.
- **Thread Safety**: Wrapped all Process Data input/output in platform critical sections.
- **Extended M-Sequences**: Supported Type 1_V and Type 2_V in DLL.
- **New Tests**: Added `tests/test_pd_variable.c` for multi-byte PD exchange validation.

### Changed
- **API (BREAKING)**: `iolink_pd_input_update` now requires a third `bool valid` argument.

## [0.3.0] — 2026-02-02

### Added
- **Core Portability**: Refactored entire stack to use Context-Based API (no global state).
- **Embedded Config**: Centralized `iolink_config.h` for tuning buffer sizes and memory usage.
- **RTOS Integration**: Added `platform.h` with critical section hooks (`iolink_critical_enter/exit`) for thread safety.
- **FreeRTOS Support**: Added `examples/freertos_app` demonstrating multi-task usage.
- **Zephyr Support**: Native simulation integration (`test_zephyr.sh`) and module manifest (`west.yml`).
- **Docker Environment**: Unified build/test container for Linux, Bare Metal, and Zephyr.
- **Strict Quality**: Enforced `-Werror`, `-Wconversion` across codebase.
- **Documentation**: Added `MEMORY_GUIDE.md` and `SECURITY.md`.

### Changed
- **Logging**: Removed `printf` from core logic; introduced logging abstraction.
- **CI/CD**: Added locking verification tests and static analysis enhancements.

## [0.2.0] — 2026-02-01

### Added
- **Application Layer API**: Introduced `iolink_pd_input_update` and `iolink_pd_output_read`.
- **Process Data (PD)**: Cyclic exchange support in `OPERATE` state.
- **ISDU Support**: Acyclic messaging engine with segmentation/reassembly.
- **Standard Services**: Implementation of Vendor Name (Index 0x10) reading.
- **Events**: Diagnostic signaling mechanism with FIFO queue and ISDU Index 2 integration.
- **Data Storage (DS)**: Parameter backup/restore state machine with storage abstraction.
- **Architecture Documentation**: Added `docs/ARCHITECTURE.md` detailing the layered design.
- **Unit Tests**: Added `test_pd.c` and `test_isdu.c`.

### Changed
- **MISRA Audit**: Improved code quality, fixed unused parameters, and ensured strict typing.
- **DLL State Machine**: Transition logic from Preoperate to Operate based on protocol events.

## [0.1.0] — 2026-02-01

### Added
- **Foundational Architecture**: Layered design with hardware abstraction.
- **PHY Abstraction**: `iolink_phy_api_t` interface for portability.
- **DLL Core**: Initial state machine (Startup, Preoperate) and CRC6 calculation.
- **Simulation Drivers**: `phy_virtual` for host-side execution and `phy_mock` for TDD.
- **Automated CI/CD**: GitHub Actions for linting, testing, and releases.
- **Host Demo**: `examples/host_demo` showing stack execution without hardware.
- **Documentation**: Initial README, ROADMAP, VISION, and RELEASE_STRATEGY.

[0.4.0]: https://github.com/w1ne/iolinki/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/w1ne/iolinki/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/w1ne/iolinki/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/w1ne/iolinki/releases/tag/v0.1.0
