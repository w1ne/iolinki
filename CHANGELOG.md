# Changelog

All notable changes to the `iolinki` project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **Timing Analysis**: Added `time_utils.h` for protocol timing enforcement and jitter analysis.
- **Error Recovery**: Implemented communication timeout watchdog and CRC frame validation in DLL.
- **IODD Generator**: Python-based tool to generate XML metadata from JSON configurations.
- **Full-Stack Integration Test**: Comprehensive lifecycle simulation from Startup to Operate.
- **Coding Standards**: Formalised MISRA C:2012 orientation and strict Doxygen enforcement.
- **Mock Storage**: Added persistent storage simulation for Data Storage (DS) verification.

### Changed
- **CI/CD Pipeline**: Added Doxygen warning check and enhanced static analysis.
- **Documentation**: Updated ROADMAP, README, and added CODING_STANDARDS.md.

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

[0.2.0]: https://github.com/w1ne/iolinki/compare/v0.1.0...develop
[0.1.0]: https://github.com/w1ne/iolinki/releases/tag/v0.1.0
