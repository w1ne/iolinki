# STM32U5 Zephyr and SOM integration

The buildable source target is [STM32U5 TIOL112](../../samples/stm32u5_tiol112/README.md),
for the exact `nucleo_u575zi_q` board and pinned Zephyr commit documented there.
It uses the shared counter/button/LED reference application and portable TIOL112
PHY; the board adapter owns 8E1 UART IRQ RX, bounded TC-complete TX, GPIO SIO/UART
mux switching, WAKE/NFAULT and a real 1 MHz TIM2 clock.

Host ring/timer tests and an Arm board ELF build are separate from physical proof.
The suggested pins are unvalidated, and no IAR build, SOM firmware binary,
IO-Link conformance result, or successful hardware/master exchange is supplied.
NVM persistence is explicitly unsupported (`-1` strong hooks); do not promise
retained parameters or successful data-storage commits until a durable flash
backend and its failure handling are implemented and tested.

For your SOM, first identify its exact STM32U5 part, package, exposed pins, supply
rails, debugger, boot/security configuration and supported Zephyr board definition.
Port the overlay and verify UART alternate functions against the part datasheet;
reserve TIM2 and preserve the checked clock-tree contract. Retest physical EN-low
startup, SIO output, WAKE latch, 8E1/TC timing, fault reporting and analog C/Q before
claiming the port works. Follow the source example's identity/PD customization
instructions and keep the IODD synchronized.

SOM binary and source licensing are distinct: this repository provides GPL source
(or separately licensed commercial source), while a SOM vendor's firmware,
proprietary drivers and binary redistribution rights require their own terms.
A distributable vendor binary does not imply source availability or adaptation
rights, and this example grants no such rights.

The required LabWired Twin includes analog IO-Link and the TIOL112 transceiver,
not merely an MCU/UART or virtual PHY. Twin integration and analog verification
are pending; the sample README records required assertions and the honest proof
boundary. Hardware validation is also pending.
