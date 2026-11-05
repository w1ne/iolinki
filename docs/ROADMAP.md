# iolinki Roadmap

This roadmap outlines the development path for `iolinki`, enabling a fully compliant, open-source IO-Link Device Stack (Spec V1.1.5).

## Development Philosophy

**Test-Driven from Ground Zero**: All development is built on mocks and abstractions. Every component is testable without hardware. Conformance verification runs against a virtual IO-Link Master on each release.

## Hardware Bring-Up Track (Real Master Readiness)

**Goal:** Minimum slice that can plug into a real IO-Link Master and survive bring-up.

- [x] PHY bring-up essentials: `phy_generic`, C/Q control, wake-up detection, SIO/SDCI switching.
- [x] DLL robustness: Awaiting Comm + ESTAB_COM + Fallback states, retry logic, error counters.
- [x] Portable timing monitors and reporting for `t_ren`, `t_cycle`, `t_dwu`; wire response-time evidence has the separate scope below.
- [x] On-Request Data (OD): negotiation + status/events content for real parameterization.
- [x] Conformance tests: Wake-up timing path, ESTAB_COM transitions, CRC fallback recovery (36 total tests).

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
    - [x] M-Type 0 framing (OD payload implemented) with mock testing.
    - [x] M-Type 1_1 (Process Data, 8-bit OD).
    - [x] M-Type 1_2 (Process Data, 8-bit OD with ISDU).
    - [x] M-Type 1_V (Process Data, variable PD, 8-bit OD).
    - [x] M-Type 2_1 (Process Data, 16-bit OD).
    - [x] M-Type 2_2 (Process Data, 16-bit OD with ISDU).
    - [x] M-Type 2_V (Process Data, variable PD, 16-bit OD).
- [x] **On-Request Data (OD)**:
    - [x] OD length negotiation (8/16/32 bits).
    - [x] OD content definition (status, events).
    - [x] OD consistency mechanism.
- [x] **Timing Control**:
    - [x] Abstract timer interface for portable DLL timing observations (receive/dequeue timestamps).
    - [x] Mock timer for deterministic unit testing.
    - [x] Checksum calculation and verification (V1.1 CRC).
    - [x] **CRC Frame**: 6-bit CRC for M-sequence Type 2_x.
    - [x] Legacy `t_ren` send-completion watchdog and counters; this is not wire `t_A`.
    - [x] `t_cycle` validation between valid OPERATE request starts.
    - [ ] Physical-target qualification of inter-byte (`t_byte`) and bit timing (`t_bit`).
    - [x] `t_dwu`: Wake-up delay (80μs).
    - [ ] `t_pd`: Power-on delay.
- [ ] **Frame Retry Logic**: Automatic retransmission on CRC errors.
- [ ] **Frame Synchronization**: Bit-level synchronization.
- [x] **Error Counters**: Track CRC, timeout, framing errors.
- [ ] **Communication Quality Metrics**: Link quality and error rate tracking.

#### Response-time scope and recorded evidence

IO-Link `t_A` is the gap at C/Q from the end of the last master stop bit to the
first device start bit. The requirement is `1 T_BIT <= t_A <= 10 T_BIT`
([Interface Specification V1.1.4, A.3.5](https://io-link.com/fileadmin/user_upload/Downloads/Package_2024/IOL-Interface-Spec_10002_V114_Jun24.pdf#page=240)).
At COM3 (230.4 kbit/s), this is **4.340–43.403 us**. The previous 230 us roadmap
claim was incorrect for COM3 `t_A`.

The STM32G0 reference separately implements a baud-derived response-start floor
from the last RX interrupt observation and a configurable EN-to-first-TDR guard
(6 us by default). The floor protects the minimum response delay; the guard
allows PHY enable time. The [TIOL112 switching characteristics](https://www.ti.com/lit/ds/symlink/tiol112.pdf#page=8)
give 4 us as a typical enable time, so the 6 us guard is an engineering
assumption pending hardware validation. Neither mechanism
guarantees the maximum `t_A` under arbitrary application or interrupt load.
The native LabWired oracle checks both bounds at modeled C/Q on every reply,
including startup; portable DLL dequeue timestamps cannot replace that check.

The legacy `response_time_us`/`t_ren` metric ends when `phy->send` returns. In the
G0 adapter that is UART TC, after the entire reply, so its completion threshold
must not be interpreted as a first-start-bit deadline. The protocol rig uses a
separate 20 ms completion limit. Manual builds default timing enforcement to 0;
the CI protocol artifact and recorded images below explicitly use
`IOLINK_RIG_TIMING_ENFORCE=1`.

Recorded native runs use STM32G0B1RE at a fixed 64 MHz operating point, TIOL112,
and TYPE_2_V with PDout1/PDin32/OD1:

| Image / mode | OPERATE replies checked | Recorded cyclic C/Q `t_A` |
|---|---:|---:|
| GCC/newlib, COM3 | 4 READ + 4 WRITE + 8 application-cadence replies | 29.738203–39.816328 us |
| Clang/LLD, COM3 | 100 READ + 100 WRITE + 8 application-cadence replies | 28.003828–41.300703 us |
| Clang/LLD, COM2 | 100 READ + 100 WRITE + 8 application-cadence replies | 29.132299–31.679174 us |

The [source and exact ELF identities in PR49](https://github.com/w1ne/iolinki/pull/49)
keep the GCC source `807f9ced` separate from the Clang source `10e8196e` and their
different run lengths. These are model results with configured PHY delays,
ideal MCU VDD and uncalibrated silicon/Flash wait-state execution costs.
GCC COM2, IAR 9.70, physical-master interoperability and hardware worst-case
timing remain unverified. A source merge does not change those evidence limits.

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
    - [x] **Transport Flow Control**: Pending-dispatch Busy (`0x01`), duplicate request COUNT handling, response COUNT replay (including the final segment), COUNT wrap and ABORT reset.
    - [ ] **Deferred Application Services**: General asynchronous completion/cancellation API; the vendor-service callback remains synchronous.
    - [x] **Record Boundaries**: 232 application bytes per read/write request; larger images require multiple records.
    - [x] **16-bit Index Support**: Support full 16-bit index.
    - [ ] **Write Verification**: Readback after write.

The master's five-second ISDU acknowledgment watchdog (Interface Specification
V1.1.4, Tables 53 and 102) ends when the first non-Busy response begins, not when
every response segment has transferred.
Applications must provide that response within the master's window. Supported
Busy/retry transport cases do not establish general deferred application
completion. See the [protocol regression scope](../validation/README.md) and
[separate hosted fixture](../validation/labwired-protocol/README.md); their
synthetic clocks are not wire-timing measurements.

- [ ] **Mandatory ISDU Indices**:
    - [x] 0x0000: Direct Parameter Page 1 (read, packed identification/communication page)
    - [x] 0x0001: Direct Parameter Page 2 (read/write, device-specific)
    - [x] 0x0002: System Command
        - [x] Subcommand 0x80: Device Reset (dispatched via iolink_set_reset_handler)
        - [x] Subcommand 0x81: Application Reset (dispatched via iolink_set_reset_handler)
        - [x] Subcommand 0x82: Restore Factory Settings
        - [x] Subcommand 0x83: Restore Application Defaults
        - [ ] Subcommand 0x84: Set Communication Mode (handled by DLL; no-op here)
        - [x] Subcommand 0x95: Parameter Upload to Master
        - [x] Subcommand 0x96: Parameter Download from Master
        - [x] Subcommand 0x97: Parameter Break
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
    - [x] 0x0019: Function Tag
    - [x] 0x001A: Location Tag
    - [x] 0x001B: Device Status
    - [x] 0x001C: Detailed Device Status
    - [x] 0x001D: Process Data Input Descriptor
    - [ ] 0x001E: Process Data Output Descriptor
    - [x] 0x0024: Min Cycle Time
    - [ ] 0x0025-0x0028: Alternate Identification (Vendor/Product Name/Text)

### 2.3 Advanced V1.1 Features
- [x] **Data Storage (DS)**:
    - [x] Implement parameter checksum generation.
    - [x] "Upload/Download" state machine for automatic parameter server.
    - [x] Real parameter serialization/restore over DS_Data index 0x0003.
    - [x] Integration with Device Access Locks (0x000C).
    - [x] DS Commands: Upload Start/End, Download Start/End.
    - [x] Checksum mismatch recovery (device re-serializes actual params; mismatch re-triggers download).
- [ ] **Block Parameterization**: Efficient bulk data transfer.
- [x] **Events**: Diagnostic event queue and transmission logic.
    - [x] **Standard Event Codes** (V1.1.5 Annex D Table D.1 constants + iolink_event_classify):
        - [x] 0x1xxx: Device events (general malfunction)
        - [x] 0x4xxx: Temperature events
        - [x] 0x5xxx: Hardware / power-supply events
        - [x] 0x6xxx: Software / parameter events
        - [x] 0x7xxx: Wiring events (wire break, short, ground)
        - [x] 0x8Cxx: Application / maintenance events
    - [x] Event Qualifier: spec-correct MODE/TYPE/SOURCE/INSTANCE encoding (0x1C).
    - [ ] Event Mode: Single/Multiple event mode.
    - [ ] **Event Instance**: Multiple instances of same event.
    - [ ] **Event Acknowledgment**: Master ACK mechanism.

### 2.4 Communication Modes & Timing
- [ ] **SIO Mode**: Fallback to standard digital I/O when validation fails.
- [ ] **Mode Switching**: Dynamic SIO ↔ SDCI transitions.
- [ ] **AutoComm**: Automatic communication startup.
- [x] Baudrate negotiation with Master.
- [x] Runtime baudrate switching.
- [x] Fallback to COM1 on errors.

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
- [x] **Timing Analysis**: Native C/Q `t_A` assertions for the pinned evaluation images and run lengths documented above.

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
- [x] Implement On-Request Data (OD) mechanism
- [ ] Implement SIO Mode switching logic
- [x] Implement Wake-up pulse handling

### 5.3 Robustness & Standards (Medium Priority)
- [ ] Map Standard Event Codes (0x1xxx-0x8xxx ranges)
- [x] Implement completion-duration and OPERATE cycle monitors; wire `t_A` has the separate response-time scope above.
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
- The STM32G0B1RE example now supplies selectable CMSIS DMA and optional ST HAL
  UART/DMA transports behind the existing synchronous PHY contract. These move
  UART bytes through DMA and wait for USART TC before releasing TIOL112 EN.
  A portable asynchronous/zero-copy PHY API remains separate work; this example
  does not introduce one. See its README and exact-ELF validation evidence.
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

## Release Retrospective

### v2.2.0 — 2026-10-17

Completed in this release (see CHANGELOG for the authoritative list):

- OPERATE M-sequence reply layout corrected to Figures A.9/A.10/A.13/A.14:
  READ replies are `[OD][PD-in] CKS`, WRITE replies are `[PD-in] CKS`, with
  `iolinki-master` 1.1.0 in lockstep.
- EventQualifier INSTANCE corrected to `0x04` (Application, Table A.17) in the
  event memory and the DetailedDeviceStatus builder.
- Device fault events use Table D.1 codes (`0x5110`, `0x5111`, `0x7710`) instead
  of the mislabelled Master/Port codes `0x1805`/`0x1806`; negative PHY voltage
  readings are "measurement unavailable" instead of a fault.
- Preferred Indices `0x0040`-`0x00FE` route to the vendor service (B.2.27).
- GPLv3 grant unrestricted; physical bench runner and evidence bundle added.

Follow-ups split out of this work:

- Decide the operating-baudrate source for recovery; five sites still hard-code
  COM1 in `src/dll.c` (Figure 31 allows one supported rate, but no clause names
  the recovery rate) — maintainer decision needed before changing behavior.
- Physical-master re-validation of the corrected reply layout.
