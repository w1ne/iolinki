# Example coverage and release acceptance

Reviewed 2 October 2026. This compares public device-stack example offerings;
it is a development checklist, not a conformance or interoperability claim.

## Public reference packages

- [TEConcept device stack](https://teconcept.de/en/io-link-products/io-link-device-stack/)
  supplies a target sample application and example IODD. Its delivery excludes
  nonvolatile-storage routines and EEPROM drivers.
- [ST STSW-IOD01](https://www.st.com/en/embedded-software/stsw-iod01.html)
  supplies an L6362A/MEMS sample and IODD, with CubeIDE, IAR and Keil toolchain
  support. This is a different board from our STM32G0B1RE/TIOL112 reference.
- [Renesas RX23E-A temperature-sensor application note](https://www.renesas.com/us/en/document/apn/rx23e-group-example-io-link-device-temperature-sensor-application-note?language=en)
  documents a temperature application, two-point/window switching thresholds,
  teach commands and nonvolatile parameter storage.

## Included examples and evidence

| Integration task | Included implementation | Verification |
| --- | --- | --- |
| Process input/output | Counter/button input and LED output in `examples/reference_device` | Executable host demo and payload tests |
| Sensor configuration | `examples/switching_sensor`: measurement, validity, switching state, threshold, hysteresis, inversion and teach | Executable ISDU demo, callback/parameter tests and IODD-to-C payload checks |
| Device description | Three example JSON/XML descriptors; `tools/iodd_cli.py` generate/validate/inspect/pack | CRC, semantic-subset, official external XSD, drift and packaging tests |
| Bare-metal MCU/PHY | STM32G0B1RE/TIOL112 GCC and native IAR project | GCC build; IAR archive paths checked after extraction |
| Other build environments | ESP32-C3/L6362A ESP-IDF and STM32U5/TIOL112 Zephyr | Target firmware build jobs |
| Repeatable firmware execution | `validation/labwired` pinned G0/C3 engine recipes | PR and release jobs execute the freshly built ELFs in both engine modes |
| Reviewable release | Source/examples, standalone IAR project, firmware, IODD packages, recipes and logs | Release workflow retains commit identifiers, JUnit results and SHA-256 checksums |

## Acceptance still needed for broader parity

The sensor example uses volatile parameters. Embedded flash persistence needs
a target-specific implementation and power-loss tests. Native IAR compilation
needs an EWARM installation and version-specific build evidence. Hardware
interoperability needs the actual board, wiring and intended physical master.

The included LabWired recipes test specified firmware startup, UART and wake
behavior; they do not establish complete analog PHY/master behavior, full
protocol interoperability, Smart Sensor Profile compliance or certification.
External XSD and our CRC checks do not replace official IODD Checker approval.
See each example README and `validation/labwired/README.md` for exact assertions.
