# iolinki Vision

## Mission
To provide a professional-grade, open-source **IO-Link Device Stack** that empowers engineers to build robust industrial sensors and actuators on **Zephyr RTOS** and bare-metal platforms.

## Industry Challenges
- **Complexity**: IO-Link timing (COM3) and state machines are difficult to implement correctly from scratch.
- **Vendor Lock-in**: Many commercial stacks are tied to specific hardware or expensive licensing models.
- **Integration**: Integrating industrial protocols with modern RTOS workflows (like Zephyr) is often non-trivial.
- **Compliance**: Passing protocol conformance tests requires rigorous engineering.

## Approach

### 1. Zephyr-Native yet Portable
The stack is designed with Zephyr in mind (Device Tree, Kconfig, Logging) but keeps the core logic separated from the OS abstraction layer (OSAL). This allows for:
- Native integration in modern Zephyr-based projects (nRF Connect SDK).
- Portability to bare-metal systems (STM32 HAL, etc.) via a thin adaptation layer.

### 2. Hardware Abstraction
We support various transceiver architectures:
- **SPI/UART**: For standard transceivers (TI, ST, Maxim) where the stack handles the Data Link Layer.
- **Frame Handlers**: Ready for advanced transceivers that offload timing-critical tasks.

### 3. Developer Experience
- **Host-Based Testing**: Extensive logic verification on PC before flashing hardware.
- **Code Quality**: Strict linting, static analysis, and clean API design.
- **Open Documentation**: Clear guides on how to implement, port, and certify devices.

## Market Position
`iolinki` fills the gap between hobbyist implementations and expensive commercial closed-source stacks. It serves as a reliable foundation for professional embedded engineers and a "lead generation" tool for high-end integration services.
