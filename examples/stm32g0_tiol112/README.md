# STM32G0B1RE + TIOL112 reference device

Buildable bare-metal integration of the shared counter/button/LED device. This
is a proposed wiring reference, **not a tested board or certified IO-Link device**.
The experimental shared identity must be replaced before product use.

## Target and wiring

The [ST STM32G0B1 datasheet](https://www.st.com/resource/en/datasheet/stm32g0b1re.pdf)
identifies the RE variant as 512KiB flash and lists USART1 TX/RX on PA9/PA10 AF1
(table 13), and TIM2 as 32-bit. Linkers deliberately use the 128KiB SRAM region;
the extra 16KiB requires repurposing parity storage. These settings assume reset
entry, 16MHz HSI, and undivided AHB/APB. Do not enter from a bootloader with live
peripherals without adding a handover/deinitialization sequence.

| STM32 pin | TIOL112 signal / application |
| --- | --- |
| PA9, USART1 TX AF1 or SIO GPIO | TX |
| PA10, USART1 RX AF1 | RX |
| PA8 output, reset low | EN |
| PB0 input, falling-edge EXTI0, pull-up | WAKE (active low) |
| PB1 input, pull-up | NFAULT (active low) |
| PA5 output, active high | application LED, suitable resistor required |
| PC13 input, pull-up, active low | application button to logic ground |

These are MCU pins, not verified development-board header positions. Check the
chosen package, board schematic, solder bridges and shared pin functions. TIOL112
TX/RX/EN/WAKE/NFAULT belong to its logic interface with compatible 3.3V logic and
appropriate pull-ups. Follow the [TI TIOL112 datasheet](https://www.ti.com/lit/ds/symlink/tiol112.pdf)
for VCC, L+, L-, CQ, decoupling, protection, thermal and power design. Connect
24V L+/L-/CQ only to the transceiver field side; never to MCU UART/GPIO pins.
Check shared ground or isolation design before connecting a master.

## GCC build

Install Arm GNU bare-metal tools (including newlib), the ST
[cmsis-device-g0](https://github.com/STMicroelectronics/cmsis-device-g0) package and
[ARM CMSIS core](https://github.com/ARM-software/CMSIS_5). Neither vendor tree is
copied into this repository. For example, obtain the vendor packages separately:

```sh
git clone https://github.com/STMicroelectronics/cmsis-device-g0.git /tmp/cmsis-device-g0
git -C /tmp/cmsis-device-g0 checkout f576c24e123edf3332988ecd49512c0f35f85186
git clone --depth 1 --branch 5.9.0 https://github.com/ARM-software/CMSIS_5.git /tmp/CMSIS_5
export STM32_CMSIS_ROOT=/tmp/cmsis-device-g0
export CMSIS_CORE_ROOT=/tmp/CMSIS_5/CMSIS/Core
bash examples/stm32g0_tiol112/build.sh
```

An STM32CubeG0 installation also works: set `STM32_CMSIS_ROOT` to
`Drivers/CMSIS/Device/ST/STM32G0xx` and `CMSIS_CORE_ROOT` to `Drivers/CMSIS` (its
`Include` must contain `core_cm0plus.h`). Outputs are `build/reference-device.elf`,
`.hex`, and `.map`. `BUILD_DIR` and `CC_ARM` optionally override the build directory
and compiler. ST's matching GCC startup is linked, all board hooks are strong,
and the script rejects unresolved ELF symbols. No Linux, virtual PHY, weak
platform stubs, or approximate bare-metal time implementation is linked.

The `stm32g0-example` CI job uses that ST commit and CMSIS 5.9.0
(`61e36449f53c25ef7825c40f7dd93685736f457f`), builds the complete firmware and
archives ELF, HEX and memory map. A local build with the same vendor revisions
and Arm GCC 13.2.1 uses 13,984 bytes of text, 4 bytes of initialized data and
6,736 bytes of BSS including the reserved stack. This is compilation evidence.

## Native IAR EWARM project

Open `iar/reference-device.ewp` in EWARM with STM32G0B1 device support. In **Tools >
Configure Custom Argument Variables**, define `STM32_CMSIS_ROOT` and
`CMSIS_CORE_ROOT` to the same vendor roots described above, using native Windows
paths. These custom argument variables are separate from shell environment
variables. Build the `Debug` configuration. The project supplies application,
stack and vendor CMSIS source groups, explicit C99, Cortex-M0+ device selection,
ST's **IAR** startup (not GCC assembly), and `stm32g0b1re.icf`. Device/core and C99
option values were checked against
[ST's native G0B1 EWARM example](https://github.com/STMicroelectronics/STM32CubeG0/blob/master/Projects/NUCLEO-G0B1RE/Examples/GPIO/GPIO_IOToggle/EWARM/GPIO_IOToggle.ewp).
`gcc_runtime.c` is GCC-only and excluded. Native IAR build has **not** been run;
this environment has no EWARM executable/license. Confirm compiler, debugger,
probe and device settings on the installed EWARM version before flashing.
Some Arduino/PlatformIO vendor distributions omit `Source/Templates/iar`; use
the complete ST component package for EWARM, with the matching IAR startup file.

## Flashing and validation

With an ST-LINK probe, reset-start wiring and a configured OpenOCD installation:

```sh
openocd -f interface/stlink.cfg -f target/stm32g0x.cfg \
  -c "program examples/stm32g0_tiol112/build/reference-device.elf verify reset exit"
```

Use the adapter file for the actual probe, connect SWDIO/SWCLK/GND and target
reference voltage correctly, and check the programmer's device identification
before programming. In EWARM, select the installed probe/debug driver and target
in project debugger options, then Download and Debug. These recipes have not
been exercised with a connected board. A matching IODD, hardware measurements
and real-master validation are still required; no certified IODD is supplied by
this target example.

## Runtime behavior and limits

USART uses 8 data bits, even parity and one stop bit: STM32 M0 selects the nine-bit
word containing eight payload bits plus parity, M1 and PS stay clear. COM1/2/3
baud divisors derive from 16MHz. Actual HSI tolerance, UART polarity, master timing
and transceiver turnaround must be measured on hardware.

The ISR buffers RX in a 127-byte usable queue. Parity/framing/noise/overrun and
queue overflow increment debugger-visible `board_rx.errors`/`overflows`; receive
returns -1 and flushes the damaged buffer rather than silently using lost data.
TX disables RX to suppress local echo, waits for TC after the final stop bit, and
uses a baud/length-based bounded deadline. Timeout increments
`board_tx_timeouts` and releases EN low; the PHY also releases EN on every exit.
WAKE is latched by EXTI and consumed atomically. Critical sections nest and
restore prior PRIMASK.

TIM2 counts microseconds and its overflow ISR extends timestamps to 64 bits. A
pending wrap is handled atomically when reading. Interrupts must not remain
masked for an entire timer period (about 71 minutes). Protocol processing spins
without a millisecond sleep; application sampling alone runs at 10ms cadence.
This example deliberately implements **no persistence**: both NVM hooks return
-1, so flash erase/program and data-storage retention are unsupported.
Persistent tag writes return an ISDU error and retain their previous values.
The legacy void factory-reset API cannot report a failed storage write.
`board_phy_fault` exposes aggregate NFAULT in the debugger; it does not infer
whether the cause is a short circuit, undervoltage or temperature.
Persistent tag setters report failure and preserve the previous RAM/device-info
values when NVM rejects a write. The legacy `void` factory-reset API clears RAM
but cannot report an NVM failure; a reset must not be treated as retained across
power loss on this target.

Host-only buffer verification:

```sh
cd examples/stm32g0_tiol112
cc -std=c99 -Wall -Wextra -Werror -I. rx_queue.c tests/test_rx_queue.c -o /tmp/stm32-rx-test
/tmp/stm32-rx-test
```

GCC compile/link and queue tests do not prove electrical signaling, wake-up,
master interoperability, response-time compliance, or hardware operation. No
physical board/master or native IAR execution was available for this example.

Local verification used Arm GNU GCC 13.2.1, ST CMSIS device headers reporting
1.4.4, and CMSIS core headers reporting 6.1.0 (PlatformIO framework-cmsis package
2.60300.0). Those are the tested installed revisions, not a claim that all vendor
versions or the illustrative fetch commands above have been verified.
