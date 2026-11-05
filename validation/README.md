# Exact-frame protocol regression rig

This rig runs the actual iolinki C stack through a queued-byte PHY. It checks
protocol behavior and negative controls. It does not measure UART/CQ timing,
prove a particular MCU/BSP, or replace physical-master interoperability tests.

```sh
bash validation/run_protocol_rig.sh
python3 validation/run_upstream_regressions.py
```

`test_protocol_rig` is also a normal CTest target, including in the existing
required host-validation job. The `protocol-rig` CI job archives its logs and
sanitizer output. The existing STM32G0 job separately cross-compiles and
archives the actual stack in the G0/TIOL112 reference firmware.

## Covered behavior

- TYPE_2_V, PDout1, PDin32, OD1, OPERATE code4; legacy OD2/code5 default retained.
- 340 distinct current-cycle READ requests and an exact WRITE reply.
- A fragmented request, bad CKT checksum, and an invalid selector.
- Full 16-bit index parsing, reserved0x00FF, and rejected0xFFFF without aliasing.
- ISDU232-byte record boundary, 233-byte rejection, and invalid ExtLength.
- Request COUNT retries and wrap; response COUNT retries, including the final
  segment; wrong COUNT and ABORT handling.
- Pending-dispatch Busy0x01, then a positive response.
- 1024bytes reconstructed through five application-defined records.

## Current-cycle input selection

The optional `iolink_device_config_t.pd_input_select` callback runs after a
complete request has passed checksum/type checks and before reply assembly.
It receives current PDout and returns a prepared PDin pointer. The application
can select a buffer without copying data itself. The stack copies it into the
reply and records the published snapshot after synchronous transmission.

Set `config.stack.od_len = 1` for the OD1 TYPE_2_V variant. Zero preserves the
existing default. The selected OD width is reflected in M-sequenceCapability.

The callback must be bounded and nonblocking, must not re-enter the stack, and
must keep its returned buffer valid until `iolink_device_process` returns.
ReturningNULL retains cached PDin; the callback can set `*valid=false`.

## Remaining limits

- The existing `response_time_us` statistic includes synchronous send completion;
  it is not the last-master-stop to first-device-start tA measurement.
- The G0 reference uses HSI16MHz, RX interrupts, and polled synchronous TX. A
  64MHz application BSP and DMA port require separate verification.
- The native IAR project exists, but structural project tests are not evidence
  of an IAR compiler run.
- Busy is implemented while synchronous dispatch is pending. There is no
  asynchronous application completion API or five-second ISDU service timeout.
- The five-record1KiB test defines its own application record scheme; actual
  products must specify record identity and cross-record snapshot consistency.

The dependency-free regression runner intentionally does not replace or emulate
the CMocka suite. Existing full CI remains responsible for that suite.
