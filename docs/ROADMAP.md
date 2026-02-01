# iolinki Roadmap

This roadmap outlines the development path for `iolinki`, enabling a fully compliant, open-source IO-Link Device Stack (Spec V1.1.5).

## Phase 1: Technical Foundation (Current Focus)

**Goal:** Establish a minimum viable product (MVP) capable of basic IO-Link communication on reference hardware.

### Milestones
- [x] **Project Bootstrapping**
    - [x] Directory structure and build system (CMake)
    - [x] CI/CD pipeline groundwork (Linting, Doxygen)
    - [x] Zephyr module structure
- [ ] **Physical Layer (PHY) Abstraction**
    - [ ] Define abstract PHY interface (UART/SPI/GPIO)
    - [ ] Implement driver for Maxim MAX22513 (Dual driver) or MAX22516 (with integrated state machine context)
    - [ ] Implement generic UART-based PHY driver (for discrete transceivers)
- [ ] **Data Link Layer (DLL) - Core**
    - [ ] Implement M-sequence handling (Type 0, Type 2_x)
    - [ ] Implement startup state machine (WURQ detection, Baud rate alignment)
    - [ ] Verify `check_checksum` and `build_checksum` logic
- [ ] **Zephyr Integration**
    - [ ] Create Zephyr Device Driver API for IO-Link
    - [ ] Implement Device Tree bindings for IO-Link devices
- [ ] **Reference Implementation**
    - [ ] "Blinky" IO-Link device on STM32 or nRF52

## Phase 2: Compliance & Feature Completeness

**Goal:** Achieve full compliance with IO-Link Interface Specification V1.1.5.

### Milestones
- [ ] **Application Layer & ISDU**
    - [ ] Implement Indexed Service Data Unit (ISDU) protocol
    - [ ] Support for Block Parameters and Data Storage (DS)
- [ ] **Advanced Features**
    - [ ] Event handling (Diagnostics)
    - [ ] Direct Parameter Page 2 implementation
- [ ] **Direct Operating Mode (DOM)**
    - [ ] SIO mode support
- [ ] **Verification & Testing**
    - [ ] Unit tests for all DLL state transitions
    - [ ] Timing verification for COM3 (230.4 kbit/s)
    - [ ] Integration with open-source test frameworks

## Phase 3: Ecosystem & Certification

**Goal:** Simplify adoption and prepare for official certification.

### Milestones
- [ ] **IODD Generation**
    - [ ] Create templates/scripts to generate valid IODD XML files from C headers
- [ ] **Certification Readiness**
    - [ ] Pre-compliance testing against IO-Link Test Specification
    - [ ] Create "Certification Checklist" guide
- [ ] **Community & Marketing**
    - [ ] Publish technical articles (e.g., "IO-Link on Zephyr")
    - [ ] Propose as upstream Zephyr subsystem (RFC)

## Future / Exploration
- [ ] Wireless IO-Link (IO-Link Wireless)
- [ ] Safety extensions (IO-Link Safety)
