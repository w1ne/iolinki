# LabWired actual-firmware tests

The release includes executable recipes for the same G0 and C3 firmware source
used by its build artifacts. These run the MCU instructions and firmware ISR;
they do not replace firmware with a host protocol implementation.

| Target | Engine pin | Assertions |
|---|---|---|
| STM32G0B1RE/TIOL112 | `4ff8cbaaa87418ace5e85be45c220b371f8c5b6f` (merged PR1294) | Startup, 1 MHz timer, GPIO UART mux and firmware WAKE interrupt/COM2 setup; CLI startup witness |
| ESP32-C3/L6362A | `d528c78e660530e76164cd99003a1d03e3b861ac` (published PR1295 branch) | Genuine ROM/second-stage boot, application execution, GPIO ISR, UART GPIO-matrix routing, COM2 8E1 |
| Nucleo-U575ZI-Q/TIOL112 | Build-only in this release recipe | Zephyr firmware compilation and host adapter tests |

The C3 engine pin is experimental, published source; it is not a merged stable
LabWired release. Its focused firmware CI passed. G0 and C3 focused tests run in
both execution modes. A prior focused pass does not mean every engine-wide
check passed. New release CI rebuilds the firmware from its exact source and
archives the simulator log; use that log for release-specific evidence.

Install the engine's pinned Rust toolchain (`rust-toolchain.toml`), native build
dependencies and firmware prerequisites described by each example README.
Clone the engine separately and check out the indicated target's exact pin:

```sh
git clone https://github.com/w1ne/labwired-core.git /tmp/labwired-iolinki-g0
git -C /tmp/labwired-iolinki-g0 checkout 4ff8cbaaa87418ace5e85be45c220b371f8c5b6f
LABWIRED_ROOT=/tmp/labwired-iolinki-g0 IOLINKI_FIRMWARE=/absolute/path/reference-device.elf bash validation/labwired/run.sh g0
```

For C3, use a separate checkout at its pin and `run.sh c3`. Place `firmware.bin`,
`bootloader.bin` and `partitions.bin` beside `firmware.elf`, as produced by
PlatformIO. The engine verifies application image segments against ELF bytes
before ROM boot. Genuine C3 ROM provisioning follows the engine's documented
images/toolchain setup. The script rejects an unexpected engine commit or
missing firmware; there is no fallback to canned logs.

The UART/interrupt tests apply WAKE/OL edges as explicit inputs. Complete analog
TIOL112/L6362A transceiver behavior, cable/supply fault recovery and full
master/device operation are separate work. No physical-master, IAR compilation
or certification result is implied by these recipes. The repaired thermal
application is separately maintained at
https://github.com/w1ne/thermal-io-link-condition-sensor; its host/build evidence
is not the same as these counter firmware tests.
