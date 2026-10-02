# Switching sensor example

Portable application for an unsigned measurement in hundredths (0..655.35),
with switching threshold, hysteresis, inversion and a teach command. This is
an application example, not an implementation of the standardized Smart Sensor
Profile. It shares the released stack's per-device vendor ISDU callback.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 4
./build/examples/switching_sensor/switching_sensor_demo
ctest --test-dir build --output-on-failure
```

The executable initializes the actual device stack and publishes three input
bytes, queues an event on a rising switching output, writes teach through the
ISDU OD transport and verifies threshold readback. Its input scenes and PHY are
host fixtures; MCU UART/24 V line execution belongs to the separate hardware
references and LabWired recipes.

| Direction | Layout |
|---|---|
| Input bytes 0..1 | Unsigned measurement ×100, big endian |
| Input byte 2 | Bit 0 switching output; bit 1 reading valid; other bits zero |
| Output | None |

| ISDU index | Access, subindex zero | Encoding |
|---|---|---|
| 0x0100 | Read/write | SP1 switch-on threshold, uint16 big endian; default 5000 |
| 0x0101 | Read/write | Hysteresis, uint16 big endian; default 200 |
| 0x0102 | Read/write | Inversion, uint8 0 or 1; default 0 |
| 0x0103 | Write only | Write uint8 1 to teach SP1 from latest valid sample |

At or above SP1 the hysteresis latch turns on; at or below SP1−hysteresis it
turns off. Between boundaries it retains state. Inversion affects the output,
not the latch. Invalid samples clear output/validity, including when inverted.
SP1 must be at least hysteresis. Invalid sizes, subindices, values and teaching
an invalid sample return negative ISDU responses without changing settings.
Event 0x8CA0 is an example vendor notification on a rising output.

`device.json` and its generated `device.xml` describe this exact PD/parameter
map using experimental identity 1234/5679. These example IODDs pass the official XSD and the CLI integrity/consistency
checks. Apply your assigned identity and run the applicable official IODD
Checker for your product definition. Schema/CLI validation does not supply
official Checker approval. See `tools/IODD_GEN.md` for generate/inspect/validate/pack.

For an MCU integration, keep the sensor/config/device contexts alive, assign
`config.user = &sensor`, `config.vendor_service = switching_sensor_service`,
configure Type 2.V with three input bytes and no output bytes, and publish each
sample using `iolink_device_pd_input_update`. Sample at application cadence while
servicing `iolink_device_process` fast enough for the link. Supply the MCU/PHY,
clock, locks and sensor acquisition as in the matching hardware reference.

These settings are volatile. No successful retained-setting or flash-write claim
is made by this application. The stack's Linux tag backend demonstrates actual
file-backed persistence in `test_linux_nvm`; parameter rejection/retention tests
exercise failures in `test_params_persistence`. Embedded flash retention needs
a durable board backend and power-cycle tests before enabled write acknowledgments.

The released description also passes genuine IODD Checker 1.1.4 business-rule
validation. That older Checker result is separate from current Checker validation,
product certification and physical-master testing. Release ZIPs use canonical IODD
filenames; identity and application process/parameter mappings remain unchanged.
