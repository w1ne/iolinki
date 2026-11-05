# STM32G0B1RE + TIOL112 reference device

Buildable bare-metal integration of the shared counter/button/LED device. This
is a proposed wiring reference, **not a tested board or certified IO-Link device**.
The experimental shared identity must be replaced before product use.

## Target and wiring

The [ST STM32G0B1 datasheet](https://www.st.com/resource/en/datasheet/stm32g0b1re.pdf)
identifies the RE variant as 512KiB flash and lists USART1 TX/RX on PA9/PA10 AF1
(table 13), and TIM2 as 32-bit. Linkers deliberately use the 128KiB SRAM region;
the extra 16KiB requires repurposing parity storage. These settings assume reset
entry and undivided AHB/APB. The default uses 16MHz HSI; an optional 64MHz PLL
configuration is described below. Do not enter from a bootloader with live
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

## 64MHz protocol profile

The default reference firmware remains at 16MHz. Select
`IOLINK_G0_SYSCLK_HZ=64000000` to use HSI16 with PLL M=1, N=8, R=2. The BSP sets
voltage range 1 and two Flash wait states before switching to PLLRCLK, then
derives TIM2's 1MHz prescaler, UART BRR and TX timeout baud from
`SystemCoreClock`. AHB and APB remain undivided. This uses CMSIS registers,
without linking ST HAL. The clock parameters match ST's
[NUCLEO-G0B1RE 64MHz example](https://github.com/STMicroelectronics/STM32CubeG0/blob/master/Projects/NUCLEO-G0B1RE/Examples/PWR/PWR_SLEEP/Src/main.c).

For the real UART/TIOL112 protocol application, build:

```sh
IOLINK_G0_SYSCLK_HZ=64000000 IOLINK_G0_PROTOCOL_PROFILE=1 \
  BUILD_DIR="$PWD/examples/stm32g0_tiol112/build-protocol64" \
  bash examples/stm32g0_tiol112/build.sh
```

This selects `protocol_rig_main.c` and produces `protocol-rig-64mhz.elf`, `.hex`
and `.map`. The application uses TYPE_2_V, PDout1, PDin32, OD1/code4, current-cycle
prepared-buffer selection and an application-defined segmented ISDU record.
COM3 is the default; `IOLINK_RIG_COM=2` selects COM2 for a separate build.
It uses the actual board UART, RX ISR, TIOL112 driver and timer; it does not
inject virtual PHY bytes or force the DLL to OPERATE. The master must perform
the wake-up and startup sequence. The CI job archives this firmware separately
as `stm32g0-tiol112-protocol-64mhz-gcc`.

The immutable 1024-byte image is exposed read-only at indices 0x0100 through
0x0104 as four 232-byte records and one 96-byte record. PDin byte 0 echoes the
current selector. Remaining PDin bytes and the image use the deterministic
formula in `protocol_rig_main.c`. Configured minimum cycle times are 3ms for
COM3 and 15ms for COM2; the Direct Parameter encoding rounds COM2 up to 15.2ms.
The 34 application blocks per second are independent of the IO-Link cycle rate.
A 232-byte ISDU read requires five request and 235 response OD1 cycles: at
34 cycles/s this is about 7.1s of transfer time. This is a throughput figure,
not a five-second timeout violation: the ISDU acknowledgment interval ends at
the **beginning** of the response, not after every response segment has been
transferred (IO-Link V1.1.4, Tables 53 and 102). Choose cycle frequency and
record size for the required latency. The stack's asynchronous completion API
and acknowledgment-timeout handling remain open work, as documented in the
protocol rig README.

The profile explicitly defaults `IOLINK_RIG_TIMING_ENFORCE=0`. Internal DLL
timing checks are disabled in that build; setting a response-time limit does
not enable them. Build a separate image with `IOLINK_RIG_TIMING_ENFORCE=1` to
evaluate the internal inter-byte, cycle and completion checks. The CI 64MHz
protocol artifact explicitly selects this enabled option. The exported
`rig_timing_enforced` symbol records the selected state. The legacy completion
threshold is 20ms and applies only when internal checks are enabled; it is not
tA. The external rig must independently measure the actual last-master-stop to
first-device-start gap and byte gaps in either build. Receive-pop timestamps
are not pin timestamps, so internal checks need their own integration result.
An OPERATE LED indicates state, not conformance.

The internal cycle monitor applies MinCycleTime only in OPERATE and compares
observed first-byte dequeue timestamps, separately from its legacy
complete-request-to-end-TX duration. STARTUP/PREOPERATE retain their distinct
recovery rules. Queue observation can jitter, so the external wire oracle is
still required even when internal checks are enabled.

A separate board-local minimum response floor starts at the last valid RX IRQ
observation. It waits for `ceil(1.5 * Tbit) + 1us` before the first TDR write,
using wrapping TIM2 arithmetic; this covers the remaining half stop bit and the
minimum one-bit response interval, with timer quantization margin. Baud-derived
values are cached during UART configuration. This floor is independent of the
TIOL112 enable guard below; neither is a substitute for measuring CQ edges on
the actual hardware. No new receive-timestamp PHY API is introduced.

`IOLINK_G0_TIOL112_ENABLE_GUARD_US` defaults to 6us between entering the
EN-asserted send callback and permitting the first UART data write. This guard
is part of the measured tA path. TIOL112 specifies a **typical** 4us driver
enable delay in datasheet section 6.7; it does not establish a guaranteed
maximum. The 6us setting is an engineering assumption, including timer
quantization margin, that requires validation across the actual operating
conditions. A simulated 4us enable-delay scenario is not worst-case electrical
certification. Override the setting deliberately when evaluating another
measured bound.

A successful cross-build proves compilation of this CMSIS/GCC reference. A
simulator run must establish its own instruction/peripheral-model accuracy.
Neither is an IAR build, a customer BSP sign-off, a measured electrical tA result,
or a physical-master interoperability result.

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
