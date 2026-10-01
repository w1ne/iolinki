# Counter, button and LED reference device

This is runnable software, with a shared C application intended for MCU ports.
The supplied executable uses an in-memory transport and scripted master frames.
It does not exercise a commercial master, transceiver, 24 V line or physical LED.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 4
./build/examples/reference_device/reference_device_demo
ctest --test-dir build --output-on-failure
```

Run from the repository root. Debian/Ubuntu prerequisites: `cmake`,
`build-essential`, `libcmocka-dev`. The executable returns nonzero on any failed
assertion and prints `SIMULATION PASS` after 100 verified exchanges.

| Direction | Bytes | Meaning |
| --- | --- | --- |
| Device to master | 0, 1 | Unsigned counter, most significant byte first |
| Device to master | 2 | Button: 0 released, 1 pressed |
| Master to device | 0 | Bit 0 controls LED; other bits ignored |

`reference_device_tick()` samples the button, increments the counter with wrap,
publishes input and reads the latest LED command. Call it on an application
sampling schedule, and call `iolink_device_process()` much more frequently to
meet communication timing. The LED reflects the latest output at sampling time.
Do not introduce a one-millisecond sleep into a physical response path.

A button rising edge queues example notification `0x8CA0`. Event acknowledgement
is not demonstrated by this executable. Application tag at index `0x18` can be
written/read using the parameter API; the demo checks local readback, not wire
ISDU transfer. Persistence is disabled in the demo and needs a real NVM backend.

`reference_device.c` uses example identity 1234/5678. These numbers are not an
assignment to your product. Replace identity and hardware revision for your
device, and use manufacturer-assigned identifiers for shipping products.

The demo uses Type 2_V with three input bytes and one output byte, verifies
response framing/checksums, alternates LED output and checks the returned counter.
It disables physical timing enforcement for the host simulation. This does not
justify disabling timing enforcement on hardware.

No certified IODD is provided for this new example yet. The existing
`tools/iodd_gen.py` produces draft XML; importing XML successfully is not a
substitute for running the applicable IODD Checker.

See [the TIOL112 integration guide](../../docs/hardware/TIOL112.md) for the reusable
transceiver driver and the remaining MCU work.
