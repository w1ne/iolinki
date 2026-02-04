# iolinki Roadmap

This roadmap outlines the development path for `iolinki`, enabling a fully compliant, open-source IO-Link Device Stack (Spec V1.1.5).

## Development Philosophy

**Test-Driven from Ground Zero**: All development is built on mocks and abstractions. Every component is testable without hardware. Conformance verification runs against a virtual IO-Link Master on each release.

## Hardware Bring-Up Track (Real Master Readiness)

**Goal:** Minimum slice that can plug into a real IO-Link Master and survive bring-up.

1. PHY bring-up essentials: `phy_generic`, C/Q control, wake-up detection, SIO/SDCI switching.
2. DLL robustness: Awaiting Comm + ESTAB_COM + Fallback states, retry logic, error counters.
3. Timing enforcement: `t_ren`, `t_cycle`, `t_dwu` compliance and reporting.
4. On-Request Data (OD): negotiation + status/events content for real parameterization.

## Phase 1: Technical Foundation (MVP)

**Goal:** Establish core protocol logic with complete hardware independence and comprehensive test coverage.

### 1.1 Project Bootstrapping
- [x] Directory structure and build system (CMake)
- [x] CI/CD pipeline groundwork (Linting, Doxygen)
- [x] Zephyr module structure definition
- [x] Test framework setup (CMocka)

### 1.2 Physical Layer (PHY) Abstraction
- [x] **PHY Interface Definition**: Define `iolink_phy_api` struct for complete hardware decoupling.
- [x] **Mock PHY Driver**: `phy_mock` for unit testing (built FIRST).
- [x] **Virtual PHY Driver**: `phy_virtual` for integration with virtual IO-Link Master.
- [x] **Generic PHY Template**: `phy_generic` as reference for real hardware ports.
- [x] **SIO Mode Support**: PHY-level SIO/SDCI mode switching.
- [x] **Wake-up Pulse**: Device wake-up request handling (80μs pulse detection).
- [x] **C/Q Line Control**: Pin state management abstraction.
- [ ] **L+ Voltage Monitoring**: Power supply monitoring (18-30V).
- [ ] **Short Circuit Detection**: Pin protection and current limiting.
- [x] **Baudrate Switching Protocol**: Full handshake sequence for baudrate changes.

### 1.3 Data Link Layer (DLL) - Core
- [x] **State Machine**: Implement "Startup" and "Pre-operate" transitions.
    - [x] Unit tests using `phy_mock` for each state transition.
    - [x] **Awaiting Communication State**: Pre-startup state.
    - [x] **ESTAB_COM State**: Communication establishment handshake.
    - [x] **Fallback State**: Error recovery state.
    - [ ] **State Transition Validation**: Guards for illegal transitions.
- [x] **M-Sequence Handling**:
    - [x] M-Type 0 framing (OD payload TBD) with mock testing.
    - [x] M-Type 1_1 (Process Data, 8-bit OD).
    - [x] M-Type 1_2 (Process Data, 8-bit OD with ISDU).
    - [x] M-Type 1_V (Process Data, variable PD, 8-bit OD).
    - [x] M-Type 2_1 (Process Data, 16-bit OD).
    - [x] M-Type 2_2 (Process Data, 16-bit OD with ISDU).
    - [x] M-Type 2_V (Process Data, variable PD, 16-bit OD).
- [ ] **On-Request Data (OD)**:
    - [ ] OD length negotiation (8/16/32 bits).
    - [ ] OD content definition (status, events).
    - [ ] OD consistency mechanism.
- [x] **Timing Control**:
    - [x] Abstract timer interface for `t_A` enforcement.
    - [x] Mock timer for deterministic unit testing.
    - [x] Checksum calculation and verification (V1.1 CRC).
    - [x] **CRC Frame**: 6-bit CRC for M-sequence Type 2_x.
    - [x] `t_ren` (Device response time) enforcement (max 230μs @ COM3).
    - [x] `t_cycle` validation.
    - [ ] `t_byte`, `t_bit`: Inter-byte and bit timing.
    - [x] `t_dwu`: Wake-up delay (80μs).
    - [ ] `t_pd`: Power-on delay.
- [ ] **Frame Retry Logic**: Automatic retransmission on CRC errors.
- [ ] **Frame Synchronization**: Bit-level synchronization.
- [x] **Error Counters**: Track CRC, timeout, framing errors.
- [ ] **Communication Quality Metrics**: Link quality and error rate tracking.

### 1.4 Virtual Test Environment
- [x] **Virtual IO-Link Master**: Python-based virtual Master with pty UART.
- [x] **Integration Test Suite**: Full-stack lifecycle testing (`test_integration_full.c`).
- [x] **Reference Stack Demo**: Host-based executable demonstrating protocol without any hardware.
- [x] **Python Virtual Master**: Phase 1 complete (M-sequence Type 0, CRC, UART).
- [x] **Docker Test Suite**: Virtual Master integrated into automated tests.

## Phase 2: Compliance & Feature Completeness

**Goal:** Achieve full compliance with IO-Link Interface Specification V1.1.5.

### 2.1 Application Layer - Process Data
- [x] **Process Data (PD)**: API for application to update/read cyclic data.
- [x] **Variable PD Length**: Support 1-32 byte PD via configuration.
- [ ] **PD Consistency**: Toggle bit mechanism.
- [x] **PD Validity**: Data quality status flags (Valid bit, Qualifier).

### 2.2 Application Layer - ISDU
- [x] **ISDU Framework**:
    - [x] Basic segmentation and reassembly.
    - [x] Basic Read/Write services.
    - [x] **Segmentation Control**: First/Last/Middle segment handling.
    - [ ] **Flow Control**: Busy/Retry mechanisms.
    - [x] **16-bit Index Support**: Support full 16-bit index.
    - [ ] **Write Verification**: Readback after write.
- [ ] **Mandatory ISDU Indices**:
    - [ ] 0x0000: Direct Parameter Page 1
    - [ ] 0x0001: Direct Parameter Page 2
    - [x] 0x0002: System Command (partial - events only)
        - [ ] Subcommand 0x80: Device Reset
        - [ ] Subcommand 0x81: Application Reset
        - [ ] Subcommand 0x82: Restore Factory Settings
        - [ ] Subcommand 0x83: Restore Application Defaults
        - [ ] Subcommand 0x84: Set Communication Mode
        - [ ] Subcommand 0x95: Parameter Upload to Master
        - [ ] Subcommand 0x96: Parameter Download from Master
        - [ ] Subcommand 0x97: Parameter Break
    - [ ] 0x0003: Master Command (optional)
    - [ ] 0x0004: Master Cycle Time
    - [ ] 0x0005: Min Cycle Time
    - [ ] 0x0006: M-sequence Capability
    - [ ] 0x0007: Revision ID
    - [ ] 0x0008: Process Data In
    - [ ] 0x0009: Process Data Out
    - [x] 0x000A: Vendor ID (16-bit)
    - [x] 0x000B: Device ID (32-bit)
    - [x] 0x000C: Device Access Locks (Parameterization, DS, UI, Communication)
    - [x] 0x000D: Profile Characteristic (16-bit)
    - [ ] 0x000E: PDIn Descriptor
    - [ ] 0x000F: PDOut Descriptor
    - [x] 0x0010: Vendor Name
    - [x] 0x0011: Vendor Text
    - [x] 0x0012: Product Name
    - [x] 0x0013: Product ID
    - [x] 0x0014: Product Text
    - [x] 0x0015: Serial Number
    - [x] 0x0016: Hardware Revision
    - [x] 0x0017: Firmware Revision
    - [x] 0x0018: Application-specific Tag
    - [ ] 0x0019: Function Tag
    - [ ] 0x001A: Location Tag
    - [x] 0x001B: Device Status
    - [ ] 0x001C: Detailed Device Status
    - [ ] 0x001D: Process Data Input Descriptor
    - [ ] 0x001E: Process Data Output Descriptor
    - [x] 0x0024: Min Cycle Time
    - [ ] 0x0025-0x0028: Alternate Identification (Vendor/Product Name/Text)

### 2.3 Advanced V1.1 Features
- [x] **Data Storage (DS)**: 
    - [x] Implement parameter checksum generation.
    - [x] "Upload/Download" state machine for automatic parameter server.
    - [ ] Integration with Device Access Locks (0x000C).
    - [ ] DS Commands: Upload Start/End, Download Start/End.
    - [ ] Checksum mismatch recovery.
- [ ] **Block Parameterization**: Efficient bulk data transfer.
- [x] **Events**: Diagnostic event queue and transmission logic.
    - [ ] **Standard Event Codes**:
        - [ ] 0x1xxx: Device events
        - [ ] 0x2xxx: Communication events
        - [ ] 0x4xxx: Process events
        - [ ] 0x5xxx: Application events
        - [ ] 0x6xxx: Reserved
        - [ ] 0x8xxx: Manufacturer-specific
    - [ ] Event Mode: Single/Multiple event mode.
    - [ ] Event Qualifier: Additional event context.
    - [ ] **Event Instance**: Multiple instances of same event.
    - [ ] **Event Acknowledgment**: Master ACK mechanism.

### 2.4 Communication Modes & Timing
- [ ] **SIO Mode**: Fallback to standard digital I/O when validation fails.
- [ ] **Mode Switching**: Dynamic SIO ↔ SDCI transitions.
- [ ] **AutoComm**: Automatic communication startup.
- [x] Baudrate negotiation with Master.
- [x] Runtime baudrate switching.
- [ ] Fallback to COM1 on errors.

### 2.5 Error Handling & Robustness
- [ ] **Error Events**: Trigger events on CRC, timeout, framing errors.
- [ ] **Error Recovery**: Retry logic, state reset mechanisms.
- [ ] **Error Reporting**: ISDU index for error statistics.
- [ ] **Timing Violation Events**: Trigger on t_ren, t_cycle violations.

## Phase 3: Ecosystem & Verification

**Goal:** Simplify adoption, ensure quality, and prepare for official certification.

### 3.1 Verification Suite
- [x] **Unit Tests**: Comprehensive coverage of state machines using mocks.
- [x] **Virtual Conformance Testing**: 
    - [x] Automated integration test suite.
    - [x] Protocol sequence verification (Startup -> Operate).
- [x] **Timing Analysis**: Virtual timing verification for `t_A` compliance.

### 3.2 IODD & Tooling
- [x] **IODD Generator**: Python script to generate XML from JSON/C metadata.
- [x] **Device Simulator**: pure-software host demo for simplified testing.

### 3.3 Certification Readiness
- [ ] **Pre-compliance Testing**: Run against official IO-Link Test Specification using standard testers.
- [ ] **Documentation**: "Certification Guide" for end-users.

## Phase 4: Platform Support

**Goal:** Enable deployment across diverse hardware and RTOS environments.

- [x] **Zephyr RTOS Integration**: Module structure, Kconfig, example app.
- [x] **Bare Metal Support**: Platform abstraction for no-OS environments.
- [x] **Linux Host Simulation**: POSIX-based testing environment.
- [x] **Docker Test Environment**: Automated testing with CMocka and Virtual Master.
- [ ] **Hardware Ports**: Reference implementations for STM32, NXP, Nordic, Espressif.

## Phase 5: V1.1.5 Full Compliance

**Goal:** Close all gaps identified in compliance analysis to achieve certification readiness.

### 5.1 Mandatory Commands (High Priority)
- [ ] Complete System Command handlers (0x0002 remaining: 0x80-0x84, 0x95-0x97)
- [ ] Complete remaining mandatory ID indices (0x0019, 0x001A, 0x001C-0x001E)
- [x] Implement Device Access Locks (Index 0x000C)

### 5.2 Protocol Completion (High Priority)
- [x] Implement M-sequence Type 1_x (8-bit OD variants)
- [x] Implement M-sequence Type 2_x (ISDU + PD combined)
- [ ] Implement On-Request Data (OD) mechanism
- [ ] Implement SIO Mode switching logic
- [x] Implement Wake-up pulse handling

### 5.3 Robustness & Standards (Medium Priority)
- [ ] Map Standard Event Codes (0x1xxx-0x8xxx ranges)
- [x] Implement timing enforcement (t_ren response time)
- [ ] Add frame retry logic for communication errors
- [x] Implement error counters and reporting

### 5.4 Optional Features (Low Priority)
- [ ] **Device Profiles**: Generic Device, Smart Sensor, IO-Link Safety
- [ ] **Isochrone Mode**: Synchronized operation with Master
- [ ] **Device Variant**: Multiple device configurations
- [ ] **Condition Monitoring**: Advanced diagnostics and predictive maintenance
- [ ] **Backup/Restore**: Full parameter backup and restore
- [ ] **Test Mode**: Factory test and loopback support
- [ ] **Operating Hours**: Device runtime tracking
- [ ] Implement Diagnosis features

## Phase 6: Embedded System Portability

**Goal:** Make stack production-ready for resource-constrained embedded systems.

### 6.1 Core Portability (CRITICAL - Blockers)
- [ ] **Context-Based API**: Remove all global state (`g_dll_ctx`, `g_isdu`, etc.), pass context pointers to all functions.
- [ ] **Logging Abstraction**: Replace `printf()` calls with configurable logging hooks or compile-time disable.
- [x] **Configuration System**: Create `iolink_config.h` for compile-time tuning (buffer sizes, queue depths).
- [x] **Memory Documentation**: Document RAM/ROM budgets and provide memory calculator tool.

### 6.2 RTOS Integration
- [x] **Critical Sections**: Add `enter_critical()` / `exit_critical()` hooks for thread safety.
- [x] **Reentrancy**: Eliminate static locals in functions, make all APIs reentrant.
- [x] **FreeRTOS Example**: Demonstrate multi-task integration with proper synchronization.
- [x] **ISR Safety**: Document which APIs are interrupt-safe vs require task context.

### 6.3 Hardware Abstraction
- [ ] **DMA Support**: Extend PHY API for DMA-based transfers (zero-copy).
- [ ] **IRQ Mode**: Add interrupt-driven receive mode to PHY abstraction.
- [ ] **GPIO Control**: Abstract C/Q line control for PHY implementations.
- [ ] **Timer Abstraction**: Formalize timer requirements beyond time_utils.

### 6.4 Production Hardening
- [ ] **Error Callbacks**: Implement `iolink_error_t` enum and user-provided error callback system.
- [ ] **Power Management**: Add `iolink_suspend()` / `iolink_resume()` APIs for low-power modes.
- [ ] **Watchdog Integration**: Define watchdog kick points in processing loop.
- [x] MISRA Audit: Initial cleanup of NULL checks and unsigned literals (v0.11.0).
- [ ] **Stack Analysis**: Document worst-case stack depth for each API call.

## Phase 7: Commercialization & Services

**Goal:** Leverage the stack for business opportunities (as per Business Case).

- [ ] **Consulting Packages**: Define "Integration Support" and "Custom Sensor Development" service offerings.
- [ ] **Training Material**: Create "Zero to IO-Link" workshop content.
- [ ] **Showcases**: Publish case studies of the stack running on diverse hardware (STM32, NXP, Nordic, Espressif).
