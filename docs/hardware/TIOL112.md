# TIOL112 integration

**Status:** Portable C driver implemented and host tested. No STM32G0/U5 board
adapter, IAR project, or physical-master validation is included in this release.

The [driver](../../src/phy_tiol112.c) implements the current context-based
`iolink_phy_api_t`. The [MCU callback contract](../../include/iolinki/phy_tiol112.h)
lets the same driver work with bare metal or an RTOS, without compiler extensions.

| Signal | MCU function | Behavior |
| --- | --- | --- |
| TX | UART TX / GPIO output | UART while communicating; GPIO for SIO |
| RX | Interrupt-buffered UART RX | Nonblocking byte reads, correct UART framing |
| EN | GPIO output | Low for receive/inactive; high only while transmitting or driving SIO |
| WAKE | GPIO interrupt, external pull-up | Active-low pulse, atomically latched/consumed |
| NFAULT | GPIO input, pull-up | Optional active-low fault indicator |
| L+, L-, C/Q | IO-Link connector | Industrial supply/ground/data, through transceiver |

Actual header/pin assignments depend on the exact MCU and transceiver board.
Use its schematic and configured jumpers; this table is a signal map, not a
validated connector drawing. Do not connect a 24 V C/Q line to MCU UART pins.

The implementation follows TI's [driver/receiver function tables](https://www.ti.com/document-viewer/TIOL112/datasheet/GUID-AFC325C2-0139-440B-9158-1BDD7653E26C):
TX low sources C/Q high, TX high sinks C/Q low. EN low releases the line for
receive. WAKE and NFAULT have separate roles; a fault must not be treated as a
wake-up event.

Provide every required callback in `iolink_tiol112_io_t`, then call
`iolink_phy_tiol112_init()` and pass `*iolink_phy_tiol112_get()` to the device
configuration. Driver storage, MCU callback user storage and device configuration
must all outlive the device context. `reference_device_init()` shows configuration
lifetime and the device application layer.

The MCU adapter must implement:

- UART framing: eight data bits, even parity, one stop bit; COM1/2/3 map to
  4800/38400/230400 baud. On STM32, account for whether the hardware word-length
  field includes the parity bit.
- IRQ receive buffering with a visible overflow/error policy, and suppression of
  local transmit echo without losing a following master frame.
- A bounded `uart_send_complete()` that waits for the last stop bit, rather than
  only filling the FIFO or completing a DMA buffer transfer. The driver drops EN
  after success, partial write or failure.
- Safe UART/GPIO pin-mux switching, initialized pin levels, and interrupt-latched
  wake-up consumption. Polling a short pulse from a slow application loop is
  insufficient.
- Hardware-backed monotonic `iolink_time_get_us()` and millisecond clock,
  critical-section hooks and persistence if advertised. Current global stack
  timing calls still require these functions; a device configuration callback
  alone does not replace them throughout the DLL.

TI's [wake-up description](https://www.ti.com/document-viewer/TIOL112/datasheet/GUID-589F287B-FD11-4AB2-8375-3143E3363873)
requires timely release into receive after the indication. Measure the full
interrupt-to-main-loop-to-EN path with a scope before declaring it validated.

The driver supports `set_cq_line()` in SIO and receive/transmit switching in SDCI.
It exposes NFAULT only when the callback is supplied; it does not fabricate supply
voltage readings. Initialization and UART configuration failures must leave the
line released.

Consult the [exact TIOL112 variant datasheet](https://www.ti.com/product/TIOL112)
for logic voltage, external supply versus integrated LDO, pull-ups and load
limits. The small PHY LDO is not a general-purpose MCU development-board supply.

Validate cold startup, cyclic PD, ISDU, event acknowledgement, fallback, all
advertised speeds, timing and disconnect recovery with a commercial master.
Record MCU/PHY/master versions, stack commit, wiring, IODD and captured results.
