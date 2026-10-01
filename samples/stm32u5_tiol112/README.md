# STM32U5 + TIOL112 Zephyr source example

Status: host adapter tests and full Zephyr board ELF build passed on 2026-10-01.
Physical wiring, IO-Link master interoperability, timing on the wire, and the
LabWired Twin (including analog C/Q) remain **unverified**. There is no IAR port.

This target is exactly `nucleo_u575zi_q` (`stm32u575xx`), not an arbitrary STM32U5
SOM. Zephyr is pinned to `c66235fb7346bbe3dbedd1dd76ec5a37a8e8262b`
(3.7.2). Use the modules from that revision's west manifest.

## Reproduce the source build

Install Python, CMake, Ninja, DTC, and an Arm GNU Embedded toolchain. Then, from a
parent directory containing this repository:

```sh
python3 -m venv .venv
. .venv/bin/activate
pip install west
west init -m https://github.com/zephyrproject-rtos/zephyr --mr c66235fb7346bbe3dbedd1dd76ec5a37a8e8262b zephyr-workspace
cd zephyr-workspace
west update
west zephyr-export
pip install -r zephyr/scripts/requirements.txt
export ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb
# Set GNUARM_ROOT to your toolchain prefix containing bin/arm-none-eabi-gcc.
export GNUARMEMB_TOOLCHAIN_PATH="$GNUARM_ROOT"
west build -b nucleo_u575zi_q ../iolinki/samples/stm32u5_tiol112 -d build/u5
```

Outputs: `build/u5/zephyr/zephyr.elf`, `.hex`, and `.bin`. Flash only after reviewing
power, pin exposure, jumper configuration, and TIOL112 electrical requirements;
`west flash -d build/u5` uses the board's configured runner. No flashing or real
board operation was demonstrated by the source build.

The app compiles an explicit list of current core sources and the shared
`examples/reference_device` application. It does not enable the legacy Zephyr
module PHY or millisecond-derived time implementation. Its own strong platform
hooks and TIM2 time implementation satisfy those symbols.

## Proposed wiring (not validated on hardware)

| Signal | MCU pin / peripheral | Behavior |
| --- | --- | --- |
| TIOL112 TX | PD5 / USART2_TX AF7 | UART 8E1; GPIO output in SIO |
| TIOL112 RX | PD6 / USART2_RX AF7 | Interrupt RX with local echo discarded |
| TIOL112 EN | PB0 | Active high; initialized low |
| TIOL112 WAKE | PB1 | Active low input, pull-up, falling-edge latch |
| TIOL112 NFAULT | PB2 | Active low input, pull-up; aggregate fault only |
| Application button | PC13 / board sw0 | Board user button |
| Application LED | PB7 / board led0 | Board blue LED |

The STM32U575 datasheet alternate-function table assigns PD5/PD6 to USART2 at AF7;
Zephyr's board pinctrl matches those functions. The overlay disables ADC4, whose
board configuration otherwise reserves PB0. Verify connector access and all
other pin users for your board revision or SOM. Open-drain transceiver outputs
need appropriate external pull-ups; MCU internal pull-ups here are not a validated
replacement. Ensure EN remains low during reset and before firmware GPIO setup
with a suitable hardware pull-down. [STM32U575 datasheet](https://www.st.com/resource/en/datasheet/stm32u575vg.pdf),
[TIOL112 datasheet](https://www.ti.com/lit/ds/symlink/tiol112.pdf).

Use the exact TIOL112 variant's supply, logic voltage, decoupling, ILIM_ADJ and
surge recommendations. C/Q is the industrial IO-Link port: never connect it
directly to an MCU GPIO. Neither a bare MCU UART nor a UART loopback models the
24 V transceiver/line behavior.

## Port behavior and limits

- USART2 uses 8 data bits, even parity, one stop bit and runtime baud changes.
  RX IRQs feed a 127-byte usable ring. Overflow increments `board_rx_dropped`,
  discards the incomplete frame, and returns `-ENOSPC` to the PHY. UART errors
  increment `board_uart_errors` and return `-EIO`; they are not converted to data.
- TX writes only when the hardware TX register/FIFO is ready, drains local RX
  echo during the transfer, and waits for hardware TC (final stop bit). The
  deadline is the 11-bit-per-byte duration plus 2 ms. Timeout disables EN and the
  UART, records `board_adapter_error`, and returns `-ETIMEDOUT`. Latched adapter
  failures prevent subsequent EN-high requests; recovery requires restart/repair.
- SIO disables UART/RX, clears stale echo, and configures PD5 as GPIO at the
  requested raw TX level. Communication reapplies the board UART pinctrl AF7
  state before configuring 8E1. This is runtime mux switching, not just a GPIO
  write while the UART still owns the pin.
- WAKE is latched in its GPIO IRQ and atomically consumed. NFAULT is exposed in
  `board_nfault_asserted` via the portable aggregate-fault getter. It cannot
  distinguish undervoltage, temperature and short circuit. GPIO adapter errors
  remain visible in `board_adapter_error`; void GPIO callbacks cannot return
  errors to the core.
- TIM2 is reserved exclusively for a 1 MHz, 32-bit free-running counter. APB1
  prescaler must be one and its rate divisible by 1 MHz, enforced by build/init
  checks. The wrap extender produces a 64-bit microsecond clock; it must be read
  at least every 71.58 minutes. The continuous protocol loop and TX polls do so.
  Deep sleep/debug halts and changing the clock tree are outside this contract.
- Strong critical hooks use nesting-safe IRQ lock/restore on this single-core
  sample. Strong NVM hooks explicitly return `-1`: persistent parameter writes,
  data-storage persistence, and power-cycle restoration are unsupported. Add an
  owned flash region, atomic commit and endurance policy before claiming them.
  Persistence failures must be surfaced by the core; this sample does not report
  successful writes without storage.
- `iolink_device_process()` spins continuously. Only button/counter/LED sampling
  runs every 10 ms; protocol service is never gated by a 10 ms sleep.

## Host checks

From the repository root:

```sh
cc -std=c99 -Wall -Wextra -Werror samples/stm32u5_tiol112/tests/test_adapter.c -o /tmp/u5-adapter-test
/tmp/u5-adapter-test
```

Tests exercise RX FIFO ordering across ring wraps, overflow visibility and frame
discard/recovery, and monotonic microsecond extension across the hardware counter
wrap. A WAKE merge regression check preserves a completed pulse latched between
IRQ enable and pin sampling, and checks GPIO-read failure propagation. They do
not execute GPIO, UART electrical timing or Zephyr on real hardware.

## Identity, process data and redistribution

Customize the static identity in `examples/reference_device/reference_device.c`
using your assigned VendorID/DeviceID and real product/revision/serial strings.
The shipped IDs and `hardware_revision="simulation"` are demonstration values.
Input process data are three bytes: big-endian 16-bit counter and button byte;
output is one byte with LED at bit zero. Change `reference_device_t`, its input
packing/tick logic and stack PD lengths together. Update your IODD and master
configuration to match; changing only a string or one buffer is insufficient.
The button rising edge emits notification `0x8CA0`.

This is a source integration example under the repository's GPL-3.0-or-later
terms (or a separately obtained commercial license), not a vendor's separately
licensed SOM binary. This repository's license does not grant rights to redistribute
third-party SOM firmware, drivers or proprietary source. Check their independent
license and obtain source rights before adapting a SOM distribution. Publishing
this sample does not establish a certification, support contract or binary license.

## Required LabWired Twin

A Twin must include this MCU execution, GPIO mux changes, UART framing, TIOL112
EN/TX/RX/WAKE/NFAULT, analog 24 V supply and C/Q line, master loading/pulses, current
limit and fault paths. Required assertions cover startup EN-low, SIO high/low,
wake transition, COM-rate 8E1 frames, TC before EN release, echo suppression, RX
overflow, button PD/LED output, NFAULT and line analog behavior. Twin integration
and analog verification are pending. No Twin pass badge or simulation evidence is
claimed here; GPIO-only/virtual-PHY tests cannot satisfy this requirement.
