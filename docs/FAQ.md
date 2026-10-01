# Evaluation FAQ

The [website FAQ](https://iolinki.com/faq.html) covers commercial and evaluation
questions. The authoritative published tier details are in
[LICENSE.COMMERCIAL](../LICENSE.COMMERCIAL); the signed agreement determines a
customer's rights. Single Developer covers one developer and unlimited commercial
deployments. Team includes up to eight hours of integration assistance; confirm
the exact hardware-porting scope in the quote. Perpetual use of the licensed
version continues after the update period.

## What can I run today?

Start with [the reference software device](../examples/reference_device/README.md)
or the existing host tests. These demonstrate protocol/software behavior.
The Nucleo UART sample is a transport starting point, and does not supply a complete
physical transceiver implementation.

## Can I integrate without CMake, using IAR?

The core is C99 and can be added as sources to a native IDE project. There is no
verified EWARM board project yet. Use `include/` as the include path and the core
source list in `zephyr/CMakeLists.txt`, replacing Zephyr time/platform code with
your own. Exclude `phy_virtual.c`, Linux sources and `platform_stubs.c` from a
physical target. If supplying strong platform hooks, exclude `platform.c` too;
this avoids relying on compiler-specific weak-symbol behavior.

The core sources are `device.c`, `crc.c`, `frame.c`, `dll.c`, `isdu.c`, `events.c`,
`data_storage.c`, `params.c`, `device_info.c`; add `phy_tiol112.c` when using the new
TIOL112 driver. Provide all time/critical-section/NVM symbols declared in
`include/iolinki/time_utils.h` and `include/iolinki/platform.h`. Ensure all library
and application units share configuration defines and that startup/linker files
match the actual MCU. A successful GCC build is not evidence of IAR compatibility.

## Is there a TIOL112 implementation?

Yes: [the portable transceiver driver](hardware/TIOL112.md) handles EN, SIO, baud
selection, wake-up consumption and NFAULT through explicit MCU callbacks. A
particular MCU still needs UART/GPIO/timer/NVM integration and physical testing.

## What about ESP32 and STEVAL-IOD003V1?

The board uses L6362A, not TIOL112. The TIOL112 control driver must not be assumed
compatible with it. The ESP32/L6362A implementation remains planned; verify power,
pin mapping and timing against the ST schematic before using it. A working UART
loopback is not a working IO-Link device.

## Have the examples been physically validated or certified?

No physical validation report or official certification record is published with
these new examples. Host unit tests and simulated frame exchanges do not establish
physical interoperability, signal timing, EMC robustness or official certification.
