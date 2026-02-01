# Changelog

All notable changes to the `iolinki` project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
