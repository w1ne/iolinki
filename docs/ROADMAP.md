# iolinki Roadmap

This roadmap outlines the development path for `iolinki`, enabling a fully compliant, open-source IO-Link Device Stack (Spec V1.1.5).

## Phase 1: Technical Foundation (MVP)

**Goal:** Establish a minimum viable product (MVP) capable of basic IO-Link communication on reference hardware.

### 1.1 Project Bootstrapping
- [x] Directory structure and build system (CMake)
- [x] CI/CD pipeline groundwork (Linting, Doxygen)
- [x] Zephyr module structure definition

### 1.2 Physical Layer (PHY) Abstraction
- [ ] **PHY Interface Definition**: Define `iolink_phy_api` struct for complete hardware decoupling.
- [ ] **Drivers**:
    - [ ] `phy_mock`: Mock driver for unit testing state machines.
    - [ ] `phy_emu`: Emulated PHY for testing against simulated masters.
    - [ ] `phy_generic`: Template driver for standard UART-based integration.

### 1.3 Data Link Layer (DLL) - Core
- [ ] **State Machine**: Implement the "Startup" and "Pre-operate" state transitions.
- [ ] **M-Sequence Handling**:
    - [ ] M-Type 0 (On-request data).
    - [ ] M-Type 1_x & 2_x (Cyclic data exchange).
- [ ] **Timing Control**:
    - [ ] Implement abstract timer interface for `t_A` (Response time) enforcement.
    - [ ] Checksum calculation and verification (V1.1 CRC).

### 1.4 Reference Implementation
- [ ] **Reference Stack**: Core logic decoupled from Zephyr/HW dependencies.
- [ ] **Simulation Environment**:
    - [ ] "Virtual Device" running on Host (Linux/Windows) via validation suite.
    - [ ] Integration with open-source IO-Link Master simulators.

## Phase 2: Compliance & Feature Completeness

**Goal:** Achieve full compliance with IO-Link Interface Specification V1.1.5.

### 2.1 Application Layer
- [ ] **Process Data (PD)**: API for application to update/read cyclic data.
- [ ] **Indexed Service Data Unit (ISDU)**:
    - [ ] Segmentation and reassembly of large parameters.
    - [ ] Standard command implementation (Restore Factory, etc.).

### 2.2 Advanced V1.1 Features
- [ ] **Data Storage (DS)**: 
    - [ ] Implement parameter checksum generation.
    - [ ] "Upload/Download" state machine for automatic parameter server.
- [ ] **Block Parameterization**: Efficient bulk data transfer.
- [ ] **Events**: Diagnostic event queue and transmission logic.

### 2.3 Direct Operating Mode (DOM)
- [ ] **SIO Mode**: Fallback to standard digital I/O when validation fails.

## Phase 3: Ecosystem & Verification

**Goal:** Simplify adoption, ensure quality, and prepare for official certification.

### 3.1 Verification Suite
- [ ] **Unit Tests**: Full coverage of state machines using Unity/CMock.
- [ ] **Integration Tests**: Python-based test runner controlling a master (e.g., via specialized hardware or simulation).
- [ ] **Timing Analysis**: Logic analyzer traces to verify COM3 (230.4 kbit/s) jitter compliance.

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
