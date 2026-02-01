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

### 1.3 Data Link Layer (DLL) - Core
- [x] **State Machine**: Implement "Startup" and "Pre-operate" transitions.
    - [x] Unit tests using `phy_mock` for each state transition.
- [x] **M-Sequence Handling**:
    - [x] M-Type 0 (On-request data) with mock testing.
    - [x] M-Type 1_x & 2_x (Cyclic data) with mock testing.
- [ ] **Timing Control**:
    - [ ] Abstract timer interface for `t_A` enforcement.
    - [ ] Mock timer for deterministic unit testing.
    - [x] Checksum calculation and verification (V1.1 CRC).

### 1.4 Virtual Test Environment
- [ ] **Virtual IO-Link Master**: Implement or integrate open-source master simulator.
- [ ] **Integration Test Suite**: Python-based test runner executing protocol scenarios.
- [x] **Reference Stack Demo**: Host-based executable demonstrating protocol without any hardware.

## Phase 2: Compliance & Feature Completeness

**Goal:** Achieve full compliance with IO-Link Interface Specification V1.1.5.

### 2.1 Application Layer
- [x] **Process Data (PD)**: API for application to update/read cyclic data.
- [x] **Indexed Service Data Unit (ISDU)**:
    - [x] Segmentation and reassembly of large parameters.
    - [x] Standard command implementation (Vendor Name, etc.).

### 2.2 Advanced V1.1 Features
- [x] **Data Storage (DS)**: 
    - [x] Implement parameter checksum generation.
    - [x] "Upload/Download" state machine for automatic parameter server.
- [ ] **Block Parameterization**: Efficient bulk data transfer.
- [x] **Events**: Diagnostic event queue and transmission logic.

### 2.3 Direct Operating Mode (DOM)
- [ ] **SIO Mode**: Fallback to standard digital I/O when validation fails.

## Phase 3: Ecosystem & Verification

**Goal:** Simplify adoption, ensure quality, and prepare for official certification.

### 3.1 Verification Suite
- [ ] **Unit Tests**: 100% coverage of state machines using mocks.
- [ ] **Virtual Conformance Testing**: 
    - [ ] Automated test suite against virtual IO-Link Master.
    - [ ] Run on every release candidate.
    - [ ] Covers all IO-Link specification test cases (V1.1.5).
- [ ] **Timing Analysis**: Virtual timing verification for COM1/COM2/COM3 compliance.

### 3.2 IODD & Tooling
- [ ] **IODD Generator**: Python script to generate XML from C header definitions.
- [ ] **Device Simulator**: pure-software device for simplified testing without hardware.

### 3.3 Certification Readiness
- [ ] **Pre-compliance Testing**: Run against official IO-Link Test Specification using standard testers.
- [ ] **Documentation**: "Certification Guide" for end-users.

## Phase 4: Commercialization & Services

**Goal:** Leverage the stack for business opportunities (as per Business Case).

- [ ] **Consulting Packages**: Define "Integration Support" and "Custom Sensor Development" service offerings.
- [ ] **Training Material**: Create "Zero to IO-Link" workshop content.
- [ ] **Showcases**: Publish case studies of the stack running on diverse hardware (STM32, NXP, Nordic, Espressif).
