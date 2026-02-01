# iolinki Roadmap

This roadmap outlines the development path for `iolinki`, enabling a fully compliant, open-source IO-Link Device Stack (Spec V1.1.5).

## Development Philosophy

**Test-Driven from Ground Zero**: All development is built on mocks and abstractions. Every component is testable without hardware. Conformance verification runs against a virtual IO-Link Master on each release.

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
- [ ] **SIO Mode Support**: PHY-level SIO/SDCI mode switching.
- [ ] **Wake-up Pulse**: Device wake-up request handling.
- [ ] **C/Q Line Control**: Pin state management abstraction.

### 1.3 Data Link Layer (DLL) - Core
- [x] **State Machine**: Implement "Startup" and "Pre-operate" transitions.
    - [x] Unit tests using `phy_mock` for each state transition.
    - [ ] **ESTAB_COM State**: Communication establishment handshake.
    - [ ] **State Transition Validation**: Guards for illegal transitions.
- [x] **M-Sequence Handling**:
    - [x] M-Type 0 (On-request data) with mock testing.
    - [ ] M-Type 1_1 (Process Data, 8-bit OD).
    - [ ] M-Type 1_2 (Process Data, 8-bit OD with ISDU).
    - [ ] M-Type 1_V (Process Data, variable OD).
    - [ ] M-Type 2_1 (Process Data, 16/32-bit OD).
    - [ ] M-Type 2_2 (Process Data, 16/32-bit OD with ISDU).
    - [ ] M-Type 2_V (Process Data, variable OD with ISDU).
- [ ] **On-Request Data (OD)**:
    - [ ] OD length negotiation (8/16/32 bits).
    - [ ] OD content definition (status, events).
    - [ ] OD consistency mechanism.
- [x] **Timing Control**:
    - [x] Abstract timer interface for `t_A` enforcement.
    - [x] Mock timer for deterministic unit testing.
    - [x] Checksum calculation and verification (V1.1 CRC).
    - [ ] `t_ren` (Device response time) enforcement (max 230μs @ COM3).
    - [ ] `t_cycle` validation.
- [ ] **Frame Retry Logic**: Automatic retransmission on CRC errors.
- [ ] **Error Counters**: Track CRC, timeout, framing errors.

### 1.4 Virtual Test Environment
- [x] **Virtual IO-Link Master**: Mock-based master simulation in integration tests.
- [x] **Integration Test Suite**: Full-stack lifecycle testing (`test_integration_full.c`).
- [x] **Reference Stack Demo**: Host-based executable demonstrating protocol without any hardware.

## Phase 2: Compliance & Feature Completeness

**Goal:** Achieve full compliance with IO-Link Interface Specification V1.1.5.

### 2.1 Application Layer - Process Data
- [x] **Process Data (PD)**: API for application to update/read cyclic data.
- [ ] **Variable PD Length**: Support 2-32 byte PD (currently fixed 1-byte).
- [ ] **PD Consistency**: Toggle bit mechanism.
- [ ] **PD Validity**: Data quality status flags (Valid bit, Qualifier).

### 2.2 Application Layer - ISDU
- [x] **ISDU Framework**:
    - [x] Basic segmentation and reassembly.
    - [x] Basic Read/Write services.
    - [ ] **Segmentation Control**: First/Last/Middle segment handling.
    - [ ] **Flow Control**: Busy/Retry mechanisms.
    - [ ] **16-bit Index Support**: Currently limited to 8-bit.
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
    - [ ] 0x000C: Device Access Locks (Parameterization, DS, UI, Communication)
    - [x] 0x0010: Vendor Name
    - [ ] 0x0011: Vendor Text
    - [ ] 0x0012: Product Name
    - [ ] 0x0013: Product ID
    - [ ] 0x0014: Product Text
    - [ ] 0x0015: Serial Number
    - [ ] 0x0016: Hardware Revision
    - [ ] 0x0017: Firmware Revision
    - [ ] 0x0018: Application-specific Tag
    - [ ] 0x0019: Function Tag
    - [ ] 0x001A: Location Tag
    - [ ] 0x001B: Device Status
    - [ ] 0x001C: Detailed Device Status
    - [ ] 0x001D: Process Data Input Descriptor
    - [ ] 0x001E: Process Data Output Descriptor
    - [ ] 0x0024: Min Cycle Time

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
    - [ ] Event Mode: Single/Multiple event mode.
    - [ ] Event Qualifier: Additional event context.

### 2.4 Communication Modes & Timing
- [ ] **SIO Mode**: Fallback to standard digital I/O when validation fails.
- [ ] **Mode Switching**: Dynamic SIO ↔ SDCI transitions.
- [ ] **AutoComm**: Automatic communication startup.
- [ ] **Baudrate Management**:
    - [ ] Baudrate negotiation with Master.
    - [ ] Runtime baudrate switching.
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
- [ ] **Hardware Ports**: Reference implementations for STM32, NXP, Nordic, Espressif.

## Phase 5: V1.1.5 Full Compliance

**Goal:** Close all gaps identified in compliance analysis to achieve certification readiness.

### 5.1 Mandatory Commands (High Priority)
- [ ] Implement System Command handlers (0x0002: Reset, Factory Restore, Mode Set)
- [ ] Implement Mandatory ID Indices (0x11-0x1E: Vendor Text, Product Name, Serial, etc.)
- [ ] Implement Device Access Locks (Index 0x000C)

### 5.2 Protocol Completion (High Priority)
- [ ] Implement M-sequence Type 1_x (8-bit OD variants)
- [ ] Implement M-sequence Type 2_x (ISDU + PD combined)
- [ ] Implement On-Request Data (OD) mechanism
- [ ] Implement SIO Mode switching logic
- [ ] Implement Wake-up pulse handling

### 5.3 Robustness & Standards (Medium Priority)
- [ ] Map Standard Event Codes (0x1xxx-0x8xxx ranges)
- [ ] Implement timing enforcement (t_ren response time)
- [ ] Add frame retry logic for communication errors
- [ ] Implement error counters and reporting

### 5.4 Optional Features (Low Priority)
- [ ] Implement Block Parameterization
- [ ] Implement AutoComm feature
- [ ] Support variable PD lengths (2-32 bytes)
- [ ] Implement Diagnosis features

## Phase 6: Embedded System Portability

**Goal:** Make stack production-ready for resource-constrained embedded systems.

### 6.1 Core Portability (CRITICAL - Blockers)
- [ ] **Context-Based API**: Remove all global state (`g_dll_ctx`, `g_isdu`, etc.), pass context pointers to all functions.
- [ ] **Logging Abstraction**: Replace `printf()` calls with configurable logging hooks or compile-time disable.
- [ ] **Configuration System**: Create `iolink_config.h` for compile-time tuning (buffer sizes, queue depths).
- [ ] **Memory Documentation**: Document RAM/ROM budgets and provide memory calculator tool.

### 6.2 RTOS Integration
- [ ] **Critical Sections**: Add `enter_critical()` / `exit_critical()` hooks for thread safety.
- [ ] **Reentrancy**: Eliminate static locals in functions, make all APIs reentrant.
- [ ] **FreeRTOS Example**: Demonstrate multi-task integration with proper synchronization.
- [ ] **ISR Safety**: Document which APIs are interrupt-safe vs require task context.

### 6.3 Hardware Abstraction
- [ ] **DMA Support**: Extend PHY API for DMA-based transfers (zero-copy).
- [ ] **IRQ Mode**: Add interrupt-driven receive mode to PHY abstraction.
- [ ] **GPIO Control**: Abstract C/Q line control for PHY implementations.
- [ ] **Timer Abstraction**: Formalize timer requirements beyond time_utils.

### 6.4 Production Hardening
- [ ] **Error Callbacks**: Implement `iolink_error_t` enum and user-provided error callback system.
- [ ] **Power Management**: Add `iolink_suspend()` / `iolink_resume()` APIs for low-power modes.
- [ ] **Watchdog Integration**: Define watchdog kick points in processing loop.
- [ ] **MISRA Audit**: Full MISRA C:2012 compliance check and fixes.
- [ ] **Stack Analysis**: Document worst-case stack depth for each API call.

## Phase 7: Commercialization & Services

**Goal:** Leverage the stack for business opportunities (as per Business Case).

- [ ] **Consulting Packages**: Define "Integration Support" and "Custom Sensor Development" service offerings.
- [ ] **Training Material**: Create "Zero to IO-Link" workshop content.
- [ ] **Showcases**: Publish case studies of the stack running on diverse hardware (STM32, NXP, Nordic, Espressif).
